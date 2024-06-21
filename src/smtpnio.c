#include "smtpnio.h"

#include "buffer.h"
#include "request.h"
#include "selector.h"
#include "stm.h"

#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define N(x) (sizeof(x) / sizeof((x)[0]))

/** obtiene el struct (smtp *) desde la llave de selección  */
#define ATTACHMENT(key) ((struct smtp*)(key)->data)

enum smtp_state
{
	/**
	 * enviar mensaje de `HELLO` al cliente
	 *
	 * Intereses:
	 *  - OP_WRITE sobre client_fd
	 *
	 * Transiciones:
	 *  - RESPONSE_WRITE  mientras queden bytes por enviar
	 *  - REQUEST_READ    cuando se enviaron todos los bytes
	 *  - ERROR           ante cualquier error
	 */
	RESPONSE_WRITE,

	/**
	 * leer respuesta del cliente al mensaje `HELLO`
	 *
	 * Intereses:
	 *  - OP_READ sobre client_fd
	 *
	 * Transiciones:
	 *  - REQUEST_READ   mientras el mensaje no esté completo
	 *  - ERROR 		 ante cualquier error
	 */
	REQUEST_READ,
	DONE,
	ERROR
};

struct smtp
{
	/** inforamción del cliente */
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len;

	/** máquina de estados */
	struct state_machine stm;

	/** parser */
	struct request request;
	struct request_parser request_parser;

	/** buffers */
	uint8_t raw_buff_read[2048], raw_buff_write[2048];
	buffer read_buffer, write_buffer;
};

static void
request_read_init(const unsigned st, struct selector_key* key)
{
	struct request_parser* p = &ATTACHMENT(key)->request_parser;
	p->request = &ATTACHMENT(key)->request;
	request_parser_init(p);
}

static void
request_read_close(const unsigned state, struct selector_key* key)
{
	request_close(&ATTACHMENT(key)->request_parser);
}

static unsigned
request_read(struct selector_key* key)
{
	unsigned ret = REQUEST_READ;
	struct smtp* s = ATTACHMENT(key);

	size_t count;
	uint8_t* ptr = buffer_write_ptr(&s->read_buffer, &count);
	ssize_t n = recv(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_write_adv(&s->read_buffer, n);

		bool error = false;
		int st = request_consume(&s->read_buffer, &s->request_parser, &error);

		if (request_is_done(st, 0)) {
			if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
				ret = RESPONSE_WRITE;
				ptr = buffer_write_ptr(&s->write_buffer, &count);

				// TODO: check count with n (min(n, count))

				memcpy(ptr, "200\r\n", 5);
				buffer_write_adv(&s->write_buffer, n);
			} else {
				ret = ERROR;
			}
		}
	} else {
		ret = ERROR;
	}

	return ret;
}

static unsigned
response_write(struct selector_key* key)
{
	unsigned ret = RESPONSE_WRITE;
	struct smtp* s = ATTACHMENT(key);

	size_t count;
	uint8_t* ptr = buffer_read_ptr(&s->write_buffer, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(&s->write_buffer, n);

		if (buffer_can_read(&s->write_buffer) == 0) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = REQUEST_READ;
			} else {
				ret = ERROR;
			}
		}
	} else {
		ret = ERROR;
	}

	return ret;
}

static const struct state_definition client_statbl[] = {
	{
	    .state = RESPONSE_WRITE,
	    .on_write_ready = response_write,
	},
	{
	    .state = REQUEST_READ,
	    .on_arrival = request_read_init,
	    .on_departure = request_read_close,
	    .on_read_ready = request_read,
	},
	{
	    .state = DONE,
	},
	{
	    .state = ERROR,
	},
};

/**
 * declaración forward de los handlers de selección de una conexión
 * establecida entre un cliente y el servidor.
 */
static void smtp_read(struct selector_key* key);
static void smtp_write(struct selector_key* key);
static void smtp_close(struct selector_key* key);
static void smtp_done(struct selector_key* key);

static const struct fd_handler smtp_handler = {
	.handle_read = smtp_read,
	.handle_write = smtp_write,
	.handle_close = smtp_close,
};

/**
 * handlers top level de la conexión pasiva.
 * son los que emiten los eventos a la maquina de estados.
 */
static void
smtp_read(struct selector_key* key)
{
	struct smtp* s = ATTACHMENT(key);
	const enum smtp_state st = stm_handler_read(&s->stm, key);

	if (st == ERROR || st == DONE)
		smtp_done(key);
}

static void
smtp_write(struct selector_key* key)
{
	struct smtp* s = ATTACHMENT(key);
	const enum smtp_state st = stm_handler_write(&s->stm, key);

	if (st == ERROR || st == DONE)
		smtp_done(key);
}

static void
smtp_close(struct selector_key* key)
{
	return;
}

static void
smtp_done(struct selector_key* key)
{
	if (key->fd != -1) {
		if (selector_unregister_fd(key->s, key->fd) != SELECTOR_SUCCESS)
			abort();
		close(key->fd);
	}
}

static void
smtp_destroy(struct smtp* s)
{
	free(s);
}

/**
 * Intenta aceptar la nueva conexión
 */
void
smtp_passive_accept(struct selector_key* key)
{
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	struct smtp* state = NULL;

	const int client = accept(key->fd, (struct sockaddr*)&client_addr, &client_addr_len);

	if (client == -1 || selector_fd_set_nio(client) == -1)
		goto fail;

	state = malloc(sizeof(*state));

	if (state == NULL)
		goto fail;

	memset(state, 0, sizeof(*state));
	memcpy(&state->client_addr, &client_addr, client_addr_len);
	state->client_addr_len = client_addr_len;

	state->stm.initial = RESPONSE_WRITE;
	state->stm.max_state = ERROR;
	state->stm.states = client_statbl;
	stm_init(&state->stm);

	buffer_init(&state->read_buffer, N(state->raw_buff_read), state->raw_buff_read);
	buffer_init(&state->write_buffer, N(state->raw_buff_write), state->raw_buff_write);

	char* hello = "220 localhost SMTP\n";
	memcpy(&state->raw_buff_write, hello, strlen(hello));
	buffer_write_adv(&state->write_buffer, strlen(hello));

	state->request_parser.request = &state->request;
	request_parser_init(&state->request_parser);

	if (selector_register(key->s, client, &smtp_handler, OP_WRITE, state) != SELECTOR_SUCCESS)
		goto fail;
	return;

fail:
	if (client != -1)
		close(client);

	smtp_destroy(state);
}
