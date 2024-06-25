/**
 * request.c -- parser del request de SMTP
 */
#include "request.h"

#include <arpa/inet.h>
#include <string.h>

void
request_parser_init(struct request_parser* p)
{
	p->state = request_verb;
	p->command = request_command_noop;
	p->i = 0;
	memset(p->request, 0, sizeof(*(p->request)));
}

enum request_state
request_parser_feed(struct request_parser* p, const uint8_t c)
{
	enum request_state next;

	switch (p->state) {
		case request_verb: {
			switch (c) {
				case 'h':
				case 'H': {
					next = request_verb_h;
				} break;

				case 'e':
				case 'E': {
					next = request_verb_e;
				} break;

				case 'm':
				case 'M': {
					next = request_verb_m;
				} break;

				case 'r':
				case 'R': {
					next = request_verb_r;
				} break;

				case 'd':
				case 'D': {
					next = request_verb_d;
				} break;

				case 'q':
				case 'Q': {
					next = request_verb_q;
				} break;

				case ' ':
				case '\t': {
					next = request_verb;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_h: {
			switch (c) {
				case 'e':
				case 'E': {
					next = request_verb_he;
				} break;

				default: {
					next = request_error;
				} break;
			}
		}

		case request_verb_he: {
			switch (c) {
				case 'l':
				case 'L': {
					next = request_verb_hel;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_verb_hel: {
			switch (c) {
				case 'o':
				case 'O': {
					next = request_verb_helo;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_verb_helo: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_helo_sep;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_verb_e: {
			switch (c) {
				case 'h':
				case 'H': {
					next = request_verb_eh;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_eh: {
			switch (c) {
				case 'l':
				case 'L': {
					next = request_verb_ehl;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_ehl: {
			switch (c) {
				case 'o':
				case 'O': {
					next = request_verb_ehlo;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_ehlo: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_ehlo_sep;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_verb_m: {
			switch (c) {
				case 'a':
				case 'A': {
					next = request_verb_ma;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_ma: {
			switch (c) {
				case 'i':
				case 'I': {
					next = request_verb_mai;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mai: {
			switch (c) {
				case 'l':
				case 'L': {
					next = request_verb_mail;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail: {
			switch (c) {
				case ' ': {
					next = request_verb_mail_;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail_: {
			switch (c) {
				case 'f':
				case 'F': {
					next = request_verb_mail_f;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail_f: {
			switch (c) {
				case 'r':
				case 'R': {
					next = request_verb_mail_fr;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail_fr: {
			switch (c) {
				case 'o':
				case 'O': {
					next = request_verb_mail_fro;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail_fro: {
			switch (c) {
				case 'm':
				case 'M': {
					next = request_verb_mail_from;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_mail_from: {
			switch (c) {
				case ':': {
					next = request_mail_from_sep;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_r: {
			switch (c) {
				case 'c':
				case 'C': {
					next = request_verb_rc;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rc: {
			switch (c) {
				case 'p':
				case 'P': {
					next = request_verb_rcp;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rcp: {
			switch (c) {
				case 't':
				case 'T': {
					next = request_verb_rcpt;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rcpt: {
			switch (c) {
				case ' ': {
					next = request_verb_rcpt_;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rcpt_: {
			switch (c) {
				case 't':
				case 'T': {
					next = request_verb_rcpt_t;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rcpt_t: {
			switch (c) {
				case 'o':
				case 'O': {
					next = request_verb_rcpt_to;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_rcpt_to: {
			switch (c) {
				case ':': {
					next = request_rcpt_to_sep;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_d: {
			switch (c) {
				case 'a':
				case 'A': {
					next = request_verb_da;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_da: {
			switch (c) {
				case 't':
				case 'T': {
					next = request_verb_dat;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_dat: {
			switch (c) {
				case 'a':
				case 'A': {
					next = request_verb_data;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_data: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_data;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_verb_q: {
			switch (c) {
				case 'u':
				case 'U': {
					next = request_verb_qu;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_qu: {
			switch (c) {
				case 'i':
				case 'I': {
					next = request_verb_qui;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_qui: {
			switch (c) {
				case 't':
				case 'T': {
					next = request_verb_quit;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_verb_quit: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_quit;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_helo_sep: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_helo_sep;
				} break;

				case '\r': {
					next = request_error;
				} break;

				default: {
					next = request_helo_domain;
					p->i = 0;
					p->request->domain[p->i++] = c;
				} break;
			}
		} break;

		case request_helo_domain: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_helo;
				} break;

				default: {
					next = request_helo_domain;
					p->request->domain[p->i++] = c;
				} break;
			}
		} break;

		case request_ehlo_sep: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_ehlo_sep;
				} break;

				case '\r': {
					next = request_error;
				} break;

				default: {
					next = request_ehlo_domain;
					p->i = 0;
					p->request->domain[p->i++] = c;
				} break;
			}
		} break;

		case request_ehlo_domain: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_ehlo;
				} break;

				default: {
					next = request_ehlo_domain;
					p->request->domain[p->i++] = c;
				} break;
			}
		} break;

		case request_mail_from_sep: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_mail_from_sep;
				} break;

				case '<': {
					next = request_mail_from;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_mail_from: {
			switch (c) {
				case '>': {
					next = request_mail_from_sender;
				} break;

				case '\r': {
					next = request_error;
				} break;

				default: {
					next = request_mail_from;
					p->request->arg[p->i++] = c;
				} break;
			}
		} break;

		case request_mail_from_sender: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_mail;
				} break;

				default: {
					next = request_error;
				} break;
			}
		} break;

		case request_rcpt_to_sep: {
			switch (c) {
				case ' ':
				case '\t': {
					next = request_rcpt_to_sep;
				} break;

				case '<': {
					next = request_rcpt_to;
				} break;

				default: {
					next = request_error;
					p->state = next;
					return request_parser_feed(p, c);
				} break;
			}
		} break;

		case request_rcpt_to: {
			switch (c) {
				case '>': {
					next = request_rcpt_to_recipient;
				} break;

				case '\r': {
					next = request_error;
				} break;

				default: {
					next = request_rcpt_to;
					p->request->arg[p->i++] = c;
				} break;
			}
		} break;

		case request_rcpt_to_recipient: {
			switch (c) {
				case '\r': {
					next = request_cr;
					p->command = request_command_rcpt;
				} break;

				default: {
					next = request_error;
				} break;
			}
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

		case request_done: {
			next = p->state;
		} break;

		case request_error: {
			p->command = request_command_unknown;
			switch (c) {
				case '\r': {
					next = request_cr;
				} break;

				default: {
					next = request_error;
				} break;
			}
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
		// TODO: check this shi
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
	return st == request_done;
}

void
request_close(struct request_parser* p)
{
	// nada que hacer
}
