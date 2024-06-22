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

#define N(x)      (sizeof(x) / sizeof((x)[0]))
#define MAX_USERS 500

/** obtiene el struct (smtp *) desde la llave de selección  */
#define ATTACHMENT(key) ((struct smtp*)(key)->data)

enum smtp_state
{
	GREETING_WRITE,
	FAILED_CONNECTION_READ,
	FAILED_CONNECTION_WRITE,
	EHLO_READ,
	EHLO_WRITE,
	MAIL_FROM_READ,
	MAIL_FROM_WRITE,
	RCPT_TO_READ,
	RCPT_TO_WRITE,
	DATA_READ,
	DATA_WRITE,
	MAIL_INFO_READ,
	MAIL_INFO_WRITE,
	DONE,
	ERROR
};

struct smtp
{
	/** información del cliente */
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
	char rcptto[255];
	char data[2048];

	int file_fd;
};

struct status
{
	int historic_connections, concurrent_connections, bytes_transfered, mails_sent;
};

static unsigned write_status(struct selector_key* key, unsigned current_state, unsigned next_state);
static unsigned read_status(struct selector_key* key,
                            unsigned current_state,
                            unsigned (*read_process)(struct selector_key* key, struct smtp* state));
static void read_init(const unsigned state, struct selector_key* key);

static unsigned greeting_write(struct selector_key* key);
static unsigned failed_connection_write(struct selector_key* key);
static unsigned failed_connection_read_process(struct selector_key* key, struct smtp* state);
static unsigned failed_connection_read(struct selector_key* key);
static unsigned ehlo_read_process(struct selector_key* key, struct smtp* state);
static unsigned ehlo_read(struct selector_key* key);
static unsigned ehlo_write(struct selector_key* key);
static unsigned mail_from_read_process(struct selector_key* key, struct smtp* state);
static unsigned mail_from_read(struct selector_key* key);
static unsigned mail_from_write(struct selector_key* key);
static unsigned rcpt_to_read_process(struct selector_key* key, struct smtp* state);
static unsigned rcpt_to_read(struct selector_key* key);
static unsigned rcpt_to_write(struct selector_key* key);
static unsigned data_read_process(struct selector_key* key, struct smtp* state);
static unsigned data_read(struct selector_key* key);
static unsigned data_write(struct selector_key* key);
static unsigned mail_info_read_process(struct selector_key* key, struct smtp* state);
static unsigned mail_info_read(struct selector_key* key);
static unsigned mail_info_write(struct selector_key* key);

struct status global_status = { 0 };

static int historic_users = 0;
static int current_users = 0;

static unsigned
write_status(struct selector_key* key, unsigned current_state, unsigned next_state)
{
	unsigned ret = current_state;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	uint8_t* ptr = buffer_read_ptr(wb, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(wb, n);
		if (!buffer_can_read(wb)) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = next_state;
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
read_status(struct selector_key* key,
            unsigned current_state,
            unsigned (*read_process)(struct selector_key* key, struct smtp* state))
{
	unsigned ret = current_state;
	struct smtp* state = ATTACHMENT(key);

	if (buffer_can_read(&state->read_buffer)) {
		ret = read_process(key, state);
	} else {
		size_t count;
		uint8_t* ptr = buffer_write_ptr(&state->read_buffer, &count);
		ssize_t n = recv(key->fd, ptr, count, 0);

		if (n > 0) {
			buffer_write_adv(&state->read_buffer, n);
			ret = read_process(key, state);
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
greeting_write(struct selector_key* key)
{
	unsigned ret = GREETING_WRITE;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	char* greeting = "220 localhost SMTP\r\n";
	int len = strlen(greeting);
	memcpy(&state->raw_buff_write, greeting, len);
	buffer_write_adv(&state->write_buffer, len);

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

static unsigned
failed_connection_write(struct selector_key* key)
{
	unsigned ret = FAILED_CONNECTION_WRITE;
	struct smtp* state = ATTACHMENT(key);

	size_t count;
	buffer* wb = &state->write_buffer;

	char* message = "554 failed connection to localhost SMTP\r\n";
	int len = strlen(message);
	memcpy(&state->raw_buff_write, message, len);
	buffer_write_adv(&state->write_buffer, len);

	uint8_t* ptr = buffer_read_ptr(wb, &count);
	ssize_t n = send(key->fd, ptr, count, 0);

	if (n > 0) {
		buffer_read_adv(wb, n);
		if (!buffer_can_read(wb)) {
			if (selector_set_interest_key(key, OP_READ) == SELECTOR_SUCCESS) {
				ret = FAILED_CONNECTION_READ;
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
failed_connection_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = FAILED_CONNECTION_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			if (strcasecmp(state->request_parser.request->verb, "quit") == 0) {
				ret = DONE;
				strcpy((char*)ptr, "221 Bye\r\n");
				buffer_write_adv(&state->write_buffer, 9);
			} else {
				ret = FAILED_CONNECTION_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 18);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
failed_connection_read(struct selector_key* key)
{
	return read_status(key, FAILED_CONNECTION_READ, failed_connection_read_process);
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
			} else if (strcasecmp(state->request_parser.request->verb, "quit") == 0) {
				ret = DONE;
				strcpy((char*)ptr, "221 Bye\r\n");
				buffer_write_adv(&state->write_buffer, 9);
			} else {
				ret = EHLO_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 18);
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
	return read_status(key, EHLO_READ, ehlo_read_process);
}

static unsigned
ehlo_write(struct selector_key* key)
{
	return write_status(key, EHLO_WRITE, MAIL_FROM_READ);
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

			// TODO: PARSER PARA AGARRAR EL MAIL DESDE "MAIL FROM: <mail@mail.com>"
			if (strcasecmp(state->request_parser.request->verb, "mail from:") == 0) {
				strcpy(state->mailfrom, state->request_parser.request->arg1);
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "250 Mail from received\r\n");
				buffer_write_adv(&state->write_buffer, 24);
			} else if (strcasecmp(state->request_parser.request->verb, "quit") == 0) {
				ret = DONE;
				strcpy((char*)ptr, "221 Bye\r\n");
				buffer_write_adv(&state->write_buffer, 9);
			} else {
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 18);
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
	return read_status(key, MAIL_FROM_READ, mail_from_read_process);
}

static unsigned
mail_from_write(struct selector_key* key)
{
	return write_status(key, MAIL_FROM_WRITE, RCPT_TO_READ);
}

static unsigned
rcpt_to_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = RCPT_TO_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			// TODO: PARSER PARA AGARRAR EL MAIL DESDE "RCPT TO: <mail@mail.com>"
			if (strcasecmp(state->request_parser.request->verb, "rcpt to:") == 0) {
				strcpy(state->rcptto, state->request_parser.request->arg1);
				ret = RCPT_TO_WRITE;
				strcpy((char*)ptr, "250 Rcpt to received\r\n");
				buffer_write_adv(&state->write_buffer, 22);
			} else if (strcasecmp(state->request_parser.request->verb, "quit") == 0) {
				ret = DONE;
				strcpy((char*)ptr, "221 Bye\r\n");
				buffer_write_adv(&state->write_buffer, 9);
			} else {
				ret = RCPT_TO_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 18);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
rcpt_to_read(struct selector_key* key)
{
	return read_status(key, RCPT_TO_READ, rcpt_to_read_process);
}

static unsigned
rcpt_to_write(struct selector_key* key)
{
	return write_status(key, RCPT_TO_WRITE, DATA_READ);
}

static unsigned
data_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = DATA_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			// TODO: PARSER PARA FIJARSE SI TERMINA EN <CR><LF>.<CR><LF> Y AGARRAR LA INFO DEL MAIL
			if (strcasecmp(state->request_parser.request->verb, "data") == 0) {
				ret = DATA_WRITE;
				strcpy((char*)ptr, "354 End data with <CR><LF>.<CR><LF>\r\n");
				buffer_write_adv(&state->write_buffer, 37);
			} else if (strcasecmp(state->request_parser.request->verb, "quit") == 0) {
				ret = DONE;
				strcpy((char*)ptr, "221 Bye\r\n");
				buffer_write_adv(&state->write_buffer, 9);
			} else {
				ret = DATA_WRITE;
				strcpy((char*)ptr, "500 Syntax error\r\n");
				buffer_write_adv(&state->write_buffer, 18);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
data_read(struct selector_key* key)
{
	return read_status(key, DATA_READ, data_read_process);
}

static unsigned
data_write(struct selector_key* key)
{
	return write_status(key, DATA_WRITE, MAIL_INFO_READ);
}

static unsigned
mail_info_read_process(struct selector_key* key, struct smtp* state)
{
	unsigned ret = MAIL_INFO_READ;

	bool error = false;
	int st = request_consume(&state->read_buffer, &state->request_parser, &error);

	if (request_is_done(st, 0)) {
		if (selector_set_interest_key(key, OP_WRITE) == SELECTOR_SUCCESS) {
			size_t count;
			uint8_t* ptr = buffer_write_ptr(&state->write_buffer, &count);

			// TODO: PARSEAR LA INFO DEL MAIL
			if (strcasecmp(state->request_parser.request->verb, "\r\n.\r\n") == 0) {
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "250 Ok: queued");
				buffer_write_adv(&state->write_buffer, 12);
			} else {
				ret = MAIL_FROM_WRITE;
				strcpy((char*)ptr, "554 Transaction failed\r\n");
				buffer_write_adv(&state->write_buffer, 24);
			}
		} else {
			ret = ERROR;
		}
	}

	return ret;
}

static unsigned
mail_info_read(struct selector_key* key)
{
	return read_status(key, MAIL_INFO_READ, mail_info_read_process);
}

static unsigned
mail_info_write(struct selector_key* key)
{
	return write_status(key, MAIL_INFO_WRITE, MAIL_FROM_READ);
}

static const struct state_definition client_statbl[] = {
	{
	    .state = GREETING_WRITE,
	    .on_write_ready = greeting_write,
	},
	{
	    .state = FAILED_CONNECTION_READ,
	    .on_arrival = read_init,
	    .on_read_ready = failed_connection_read,
	},
	{
	    .state = FAILED_CONNECTION_WRITE,
	    .on_write_ready = failed_connection_write,
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
	{
	    .state = RCPT_TO_READ,
	    .on_arrival = read_init,
	    .on_read_ready = rcpt_to_read,
	},
	{
	    .state = RCPT_TO_WRITE,
	    .on_write_ready = rcpt_to_write,
	},
	{
	    .state = DATA_READ,
	    .on_arrival = read_init,
	    .on_read_ready = data_read,
	},
	{
	    .state = DATA_WRITE,
	    .on_write_ready = data_write,
	},
	{
	    .state = MAIL_INFO_READ,
	    .on_arrival = read_init,
	    .on_read_ready = mail_info_read,
	},
	{
	    .state = MAIL_INFO_WRITE,
	    .on_write_ready = mail_info_write,
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

	if (current_users < MAX_USERS) {
		state->stm.initial = GREETING_WRITE;
	} else {
		state->stm.initial = FAILED_CONNECTION_WRITE;
	}
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
