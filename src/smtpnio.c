#include "smtpnio.h"

#include "buffer.h"
#include "data.h"
#include "request.h"
#include "selector.h"
#include "stm.h"

#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define N(x) (sizeof(x) / sizeof((x)[0]))

/** obtiene el struct (smtp *) desde la llave de selección  */
#define ATTACHMENT(key) ((struct smtp*)(key)->data)

enum smtp_state
{
	GREETING_WRITE,
	EHLO_READ,
	EHLO_WRITE,
	MAIL_FROM_READ,
	MAIL_FROM_WRITE,
	RCPT_TO_READ,
	RCPT_TO_WRITE,
	DATA_READ,
	DATA_WRITE,
	/* MAIL_FROM_RESPONSE_WRITE,
	RCPT_TO_READ,
	RCPT_TO_RESPONSE_WRITE,
	DATA_READ,
	DATA_RESPONSE_WRITE, */

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
	// RESPONSE_WRITE,

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
	// REQUEST_READ,

	/**
	 * lee la data del cliente.
	 *
	 * Intereses:
	 *  - OP_READ sobre client_fd
	 */
	// DATA_READ,

	/**
	 * escribe la data del cliente.
	 *
	 * Intereses:
	 *     - NOP 	   sobre client_fd
	 *     - OP_WRITE  sobre archivo_fd
	 *
	 * Transiciones:
	 *  - DATA_WRITE    mientras tenga cosas para escribir
	 *  - DATA_READ     cuando se me vacio el buffer
	 *  - ERROR         ante cualquier error (IO/parseo)
	 */
	// DATA_WRITE,

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
	struct data_parser data_parser;

	/** buffers */
	uint8_t raw_buff_read[2048], raw_buff_write[2048], raw_buff_file[2048];  // TODO: Fix this
	buffer read_buffer, write_buffer, file_buffer;

	bool is_data;

	char mailfrom[255];

	int file_fd;
};

struct status
{
	int historic_connections, concurrent_connections, bytes_transfered, mails_sent;
};

struct status global_status = { 0 };

static int historic_users = 0;
static int current_users = 0;

static unsigned
greeting_write(struct selector_key* key)
{
	unsigned ret = GREETING_WRITE;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	char* greeting = "220 localhost SMTP\r\n";
	memcpy(&state->raw_buff_write, greeting, strlen(greeting));
	buffer_write_adv(&state->write_buffer, strlen(greeting));

	uint8_t* ptr = buffer_read_ptr(wb, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(wb, n);
		if (!buffer_can_read(wb)) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = EHLO_READ;
			} else {
				ret = ERROR;
			}
		}
	} else {
		ret = ERROR;
	}

	return ret;
}

static void
read_init(const unsigned state, struct selector_key* key)
{
	struct request_parser* p = &ATTACHMENT(key)->request_parser;
	p->request = &ATTACHMENT(key)->request;
	request_parser_init(p);
}

static unsigned
ehlo_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = EHLO_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			if (strcasecmp(state->request_parser.request->verb, "ehlo") == 0) {
				ret = EHLO_WRITE;
				strcpy((char*)ptr, "250 EHLO received\r\n");
				buffer_write_adv(&state->write_buffer, 19);
			} else {
				ret = EHLO_WRITE;
				strcpy((char*)ptr, "250 Ok\r\n");
				buffer_write_adv(&state->write_buffer, 8);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
ehlo_read(struct selector_key* key)
{
	unsigned ret = EHLO_READ;
	struct smtp* state = ATTACHMENT(key);

	if (buffer_can_read(&state->read_buffer)) {
		ret = ehlo_read_process(key, state);
	} else {
		size_t count;
		uint8_t* ptr = buffer_write_ptr(&state->read_buffer, &count);
		ssize_t n = recv(key->fd, ptr, count, 0);

		if (n > 0) {
			buffer_write_adv(&state->read_buffer, n);
			ret = ehlo_read_process(key, state);
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
ehlo_write(struct selector_key* key)
{
	unsigned ret = EHLO_WRITE;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	uint8_t* ptr = buffer_read_ptr(wb, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(wb, n);
		if (!buffer_can_read(wb)) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = MAIL_FROM_READ;
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
mail_from_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = MAIL_FROM_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			if (strcasecmp(state->request_parser.request->verb, "mail from:") == 0) {
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "250 Mail from received\r\n");
				buffer_write_adv(&state->write_buffer, 45);
			} else {
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 8);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
mail_from_read(struct selector_key* key)
{
	unsigned ret = MAIL_FROM_READ;
	struct smtp* state = ATTACHMENT(key);

	if (buffer_can_read(&state->read_buffer)) {
		ret = mail_from_read_process(key, state);
	} else {
		size_t count;
		uint8_t* ptr = buffer_write_ptr(&state->read_buffer, &count);
		ssize_t n = recv(key->fd, ptr, count, 0);

		if (n > 0) {
			buffer_write_adv(&state->read_buffer, n);
			ret = mail_from_read_process(key, state);
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
mail_from_write(struct selector_key* key)
{
	unsigned ret = MAIL_FROM_WRITE;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	uint8_t* ptr = buffer_read_ptr(wb, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(wb, n);
		if (!buffer_can_read(wb)) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = RCPT_TO_READ;
			} else {
				ret = ERROR;
			}
		}
	} else {
		ret = ERROR;
	}

	return ret;
}

// static void
// request_read_init(const unsigned st, struct selector_key* key)
// {
// 	struct request_parser* p = &ATTACHMENT(key)->request_parser;
// 	p->request = &ATTACHMENT(key)->request;
// 	request_parser_init(p);
// }
//
// static void
// request_read_close(const unsigned state, struct selector_key* key)
// {
// 	request_close(&ATTACHMENT(key)->request_parser);
// }
//
// static enum smtp_state
// request_process(struct smtp* state)
// {
// 	if (strcasecmp(state->request_parser.request->verb, "data") == 0) {
// 		state->is_data = true;
// 		return RESPONSE_WRITE;
// 	}
//
// 	if (strcasecmp(state->request_parser.request->verb, "mail from") == 0) {
// 		// TODO: Check arg1
// 		strcpy(state->mailfrom, state->request_parser.request->arg1);
//
// 		size_t count;
// 		uint8_t* ptr;
//
// 		// Generate response
// 		ptr = buffer_write_ptr(&state->write_buffer, &count);
//
// 		// TODO: Check count with n (min(n,count))
// 		strcpy((char*)ptr, "250 Ok\r\n");
// 		buffer_write_adv(&state->write_buffer, 8);
//
// 		return RESPONSE_WRITE;
// 	}
//
// 	if (strcasecmp(state->request_parser.request->verb, "ehlo") == 0) {
// 		/*
// 		 *  250-emilio
// 		    250-PIPELINING
// 		    250 SIZE 10240000
// 		 * */
// 		return RESPONSE_WRITE;
// 	}
//
// 	size_t count;
// 	uint8_t* ptr;
//
// 	// Generate response
// 	ptr = buffer_write_ptr(&state->write_buffer, &count);
//
// 	// TODO: Check count with n (min(n,count))
// 	strcpy((char*)ptr, "250 Ok\r\n");
// 	buffer_write_adv(&state->write_buffer, 8);
//
// 	return RESPONSE_WRITE;
// }
//
// static unsigned int
// request_read_posta(struct selector_key* key, struct smtp* state)
// {
// 	unsigned int ret = REQUEST_READ;
// 	;
// 	bool error = false;
// 	int st = request_consume(&state->read_buffer, &state->request_parser, &error);
// 	if (request_is_done(st, 0)) {
// 		if (SELECTOR_SUCCESS == selector_set_interest_key(key, OP_WRITE)) {
// 			// Procesamiento
// 			ret = request_process(state);  // tengo todo completo
// 		} else {
// 			ret = ERROR;
// 		}
// 	}
// 	return ret;
// }
//
// static unsigned
// request_read(struct selector_key* key)
// {
// 	unsigned ret;
// 	struct smtp* s = ATTACHMENT(key);
//
// 	if (buffer_can_read(&s->read_buffer)) {
// 		ret = request_read_posta(key, s);
// 	} else {
// 		size_t count;
// 		uint8_t* ptr = buffer_write_ptr(&s->read_buffer, &count);
// 		ssize_t n = recv(key->fd, ptr, count, 0);
//
// 		if (n > 0) {
// 			buffer_write_adv(&s->read_buffer, n);
// 			ret = request_read_posta(key, s);
// 		} else {
// 			ret = ERROR;
// 		}
// 	}
//
// 	return ret;
// }
//
// static unsigned
// response_write(struct selector_key* key)
// {
// 	unsigned ret = RESPONSE_WRITE;
//
// 	size_t count;
// 	buffer* wb = &ATTACHMENT(key)->write_buffer;
//
// 	uint8_t* ptr = buffer_read_ptr(wb, &count);
// 	ssize_t n = send(key->fd, ptr, count, 0);
//
// 	if (n > 0) {
// 		buffer_read_adv(wb, n);
// 		if (!buffer_can_read(wb)) {
// 			// TODO: Ver si voy para data o request
// 			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
// 				ret = ATTACHMENT(key)->is_data ? DATA_READ : REQUEST_READ;
// 			} else {
// 				ret = ERROR;
// 			}
// 		}
// 	} else {
// 		ret = ERROR;
// 	}
//
// 	return ret;
// }
//
// static unsigned int
// data_read_posta(struct selector_key* key, struct smtp* state)
// {
// 	unsigned int ret = DATA_READ;
// 	bool error = false;
//
// 	buffer* b = &state->read_buffer;
// 	enum data_state st = state->data_parser.state;
//
// 	while (buffer_can_read(b)) {
// 		const uint8_t c = buffer_read(b);
// 		st = data_parser_feed(&state->data_parser, c);
// 		if (data_is_done(st)) {
// 			break;
// 		}
// 	}
//
// 	struct selector_key key_file;  // TODO: arreglar esto
//
// 	// write to file from buffer if is not empty
// 	if (selector_set_interest_key(key, OP_NOOP) == SELECTOR_SUCCESS) {
// 		if (selector_set_interest_key(&key_file, OP_WRITE) == SELECTOR_SUCCESS)
// 			ret = DATA_WRITE;  // Vuelvo a request_read
// 	} else {
// 		ret = ERROR;
// 	}
//
// 	return ret;
// }
//
// static unsigned
// data_read(struct selector_key* key)
// {
// 	unsigned ret;
// 	struct smtp* state = ATTACHMENT(key);
//
// 	if (buffer_can_read(&state->read_buffer)) {
// 		ret = data_read_posta(key, state);
// 	} else {
// 		size_t count;
// 		uint8_t* ptr = buffer_write_ptr(&state->read_buffer, &count);
// 		ssize_t n = recv(key->fd, ptr, count, 0);
//
// 		if (n > 0) {
// 			buffer_write_adv(&state->read_buffer, n);
// 			ret = data_read_posta(key, state);
// 		} else {
// 			ret = ERROR;
// 		}
// 	}
//
// 	return ret;
// }
//
// static unsigned
// data_write(struct selector_key* key)
// {
// 	return REQUEST_READ;
// }

static const struct state_definition client_statbl[] = {
	{
	    .state = GREETING_WRITE,
	    .on_write_ready = greeting_write,
	},
	{
	    .state = EHLO_READ,
	    .on_arrival = read_init,
	    .on_read_ready = ehlo_read,
	},
	{
	    .state = EHLO_WRITE,
	    .on_write_ready = ehlo_write,
	},
	{
	    .state = MAIL_FROM_READ,
	    .on_arrival = read_init,
	    .on_read_ready = mail_from_read,
	},
	{
	    .state = MAIL_FROM_WRITE,
	    .on_write_ready = mail_from_write,
	},
	// {
	//     .state = RCPT_TO_READ,
	//     .on_arrival = read_init,
	//     .on_read_ready = rcpt_to_read,
	// },
	// {
	//     .state = RCPT_TO_WRITE,
	//     .on_write_ready = rcpt_to_write,
	// },
	// {
	//     .state = DATA_READ,
	//     .on_arrival = read_init,
	//     .on_read_ready = data_read,
	// },
	// {
	//     .state = DATA_WRITE,
	//     .on_write_ready = data_write,
	// },
	// {
	//     .state = RESPONSE_WRITE,
	//     .on_write_ready = response_write,
	// },
	// {
	//     .state = REQUEST_READ,
	//     .on_arrival = request_read_init,
	//     .on_departure = request_read_close,
	//     .on_read_ready = request_read,
	// },
	// {
	//     .state = DATA_READ,
	//     .on_read_ready = data_read,
	// },
	// {
	//     .state = DATA_WRITE,
	//     .on_read_ready = data_write,
	// },
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
	struct state_machine* stm = &ATTACHMENT(key)->stm;
	const enum smtp_state st = stm_handler_write(stm, key);

	if (st == ERROR || st == DONE) {
		smtp_done(key);
	} /*  else if (st == REQUEST_READ || st == DATA_READ) {
	     buffer* rb = &ATTACHMENT(key)->read_buffer;
	     if (buffer_can_read(rb))
	         smtp_read(key);
	 } */
}

static void
smtp_destroy(struct smtp* s)
{
	free(s);
}

static void
smtp_close(struct selector_key* key)
{
	smtp_destroy(ATTACHMENT(key));
}

static void
smtp_done(struct selector_key* key)
{
	if (key->fd != -1) {
		if (selector_unregister_fd(key->s, key->fd) != SELECTOR_SUCCESS)
			abort();
		close(key->fd);
		fprintf(stdout, "User diconnected\n");
		fprintf(stdout, "Current users: %d\n", --current_users);
		fprintf(stdout, "Historic users: %d\n\n", historic_users);
	}
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

	state->stm.initial = GREETING_WRITE;
	state->stm.max_state = ERROR;
	state->stm.states = client_statbl;
	stm_init(&state->stm);

	buffer_init(&state->read_buffer, N(state->raw_buff_read), state->raw_buff_read);
	buffer_init(&state->write_buffer, N(state->raw_buff_write), state->raw_buff_write);

	/* char* hello = "220 localhost SMTP\n";
	memcpy(&state->raw_buff_write, hello, strlen(hello));
	buffer_write_adv(&state->write_buffer, strlen(hello)); */

	fprintf(stdout, "New user connected\n");
	fprintf(stdout, "Current users: %d\n", ++current_users);
	fprintf(stdout, "Historic users: %d\n\n", ++historic_users);

	state->request_parser.request = &state->request;
	request_parser_init(&state->request_parser);

	data_parser_init(&state->data_parser);

	if (selector_register(key->s, client, &smtp_handler, OP_WRITE, state) != SELECTOR_SUCCESS)
		goto fail;
	return;

fail:
	if (client != -1)
		close(client);

	smtp_destroy(state);
}
