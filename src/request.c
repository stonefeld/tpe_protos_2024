/**
 * request.c -- parser del request de SMTP
 */
#include "request.h"

#include <arpa/inet.h>
#include <string.h>

static enum request_state verb(const uint8_t c, struct request_parser* p);
static enum request_state sep_arg1(const uint8_t c, struct request_parser* p);
static enum request_state arg1(const uint8_t c, struct request_parser* p);

static enum request_state
verb(const uint8_t c, struct request_parser* p)
{
	enum request_state next;

	switch (c) {
		case '\r': {
			next = request_cr;
		} break;

		default: {
			next = request_verb;
		} break;
	}

	if (next == request_verb) {
		p->request->verb[p->i++] = c;
		// if (p->i < sizeof(p->request->verb) - 1)  // TODO: Check this
		// 	p->request->verb[p->i++] = (char)c;
	} else {
		p->request->verb[p->i] = 0;
		/*if (strcmp(p->request->verb, "data") == 0)
		    next = request_data;*/
	}

	return next;
}

static enum request_state
sep_arg1(const uint8_t c, struct request_parser* p)
{
	return request_arg1;
}

static enum request_state
arg1(const uint8_t c, struct request_parser* p)
{
	p->request->arg1[0] = c;
	return request_done;
}

void
request_parser_init(struct request_parser* p)
{
	p->state = request_verb;
	p->i = 0;
	memset(p->request, 0, sizeof(*(p->request)));
}

enum request_state
request_parser_feed(struct request_parser* p, const uint8_t c)
{
	enum request_state next;

	switch (p->state) {
		case request_verb: {
			next = verb(c, p);
		} break;

		case request_sep_arg1: {
			next = sep_arg1(c, p);
		} break;

		case request_arg1: {
			next = arg1(c, p);
		} break;

		case request_cr: {
			switch (c) {
				case '\n': {
					next = request_done;
				} break;

				default: {
					next = request_verb;
				} break;
			}
		} break;

		case request_data:
		case request_done:
		case request_error: {
			next = p->state;
		} break;

		default: {
			next = request_error;
		} break;
	}

	return p->state = next;
}

enum request_state
request_consume(buffer* b, struct request_parser* p, bool* errored)
{
	enum request_state st = p->state;

	while (buffer_can_read(b)) {
		const uint8_t c = buffer_read(b);
		st = request_parser_feed(p, c);
		if (request_is_done(st, errored))
			break;
	}

	return st;
}

bool
request_is_done(const enum request_state st, bool* errored)
{
	if (st >= request_error && errored != 0)
		*errored = true;
	return st >= request_done;
}

void
request_close(struct request_parser* p)
{
	// nada que hacer
}
