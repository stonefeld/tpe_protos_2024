#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "buffer.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

enum request_command
{
	request_command_ehlo,
	request_command_mail,
	request_command_rcpt,
	request_command_data,
	request_command_rset,
	request_command_quit,
	request_command_noop,
	request_command_vrfy,
	request_command_expn,
	request_command_help,
	request_command_unknown,
};

enum request_state
{
	request_verb,

	request_verb_e,
	request_verb_eh,
	request_verb_ehl,
	request_verb_ehlo,

	request_verb_m,
	request_verb_ma,
	request_verb_mai,
	request_verb_mail,
	request_verb_mail_,
	request_verb_mail_f,
	request_verb_mail_fr,
	request_verb_mail_fro,
	request_verb_mail_from,

	request_verb_r,
	request_verb_rc,
	request_verb_rcp,
	request_verb_rcpt,
	request_verb_rcpt_,
	request_verb_rcpt_t,
	request_verb_rcpt_to,

	request_verb_d,
	request_verb_da,
	request_verb_dat,
	request_verb_data,

	request_verb_q,
	request_verb_qu,
	request_verb_qui,
	request_verb_quit,

	request_ehlo_sep,
	request_ehlo_domain,

	request_mail_from_sep,
	request_mail_from_sender,

	request_rcpt_to_sep,
	request_rcpt_to_recipient,

	request_cr,

	// apartir de aca están done
	request_done,

	// y apartir de aca son considerado con error
	request_error,

	/* request_error_unknown_verb,
	request_error_invalid_length, */
};

struct request
{
	char verb[10];
	char arg1[32];
	char domain[32];
};

struct request_parser
{
	struct request* request;
	enum request_state state;

	enum request_command command;

	/** cuantos bytes ya leimos */
	uint8_t i;
};

/** inicializa el parser */
void request_parser_init(struct request_parser* p);

/** entrega un byte al parser. retorna true si se llego al final  */
enum request_state request_parser_feed(struct request_parser* p, const uint8_t c);

/**
 * por cada elemento del buffer llama a `request_parser_feed' hasta que
 * el parseo se encuentra completo o se requieren mas bytes.
 *
 * @param errored parametro de salida. si es diferente de NULL se deja dicho
 *   si el parsing se debió a una condición de error
 */
enum request_state request_consume(buffer* b, struct request_parser* p, bool* errored);

/**
 * Permite distinguir a quien usa socks_hello_parser_feed si debe seguir
 * enviando caracters o no.
 *
 * En caso de haber terminado permite tambien saber si se debe a un error
 */
bool request_is_done(const enum request_state st, bool* errored);

void request_close(struct request_parser* p);

#endif
