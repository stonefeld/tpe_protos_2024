#ifndef __ARGS_H__
#define __ARGS_H__

#define MAX_USERS 10

#include <stdbool.h>

struct smtpargs
{
	char* mail_dir;
	unsigned short smtp_port;
	unsigned short mng_port;
	char* transformations;
	char* pass;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecución.
 */
void parse_args(const int argc, char** argv, struct smtpargs* args);

#endif
