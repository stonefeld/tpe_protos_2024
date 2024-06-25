#ifndef __ARGS_H__
#define __ARGS_H__

#include <stdbool.h>

#define MAX_USERS 10

struct users
{
	char* name;
	char* pass;
};

struct doh
{
	char* host;
	char* ip;
	unsigned short port;
	char* path;
	char* query;
};

struct socks5args
{
	char* smtp_addr;
	unsigned short smtp_port;

	char* mng_addr;
	unsigned short mng_port;

	char* transformation_program;
	
	struct doh doh;
	struct users users[MAX_USERS];
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecución.
 */
void parse_args(const int argc, char** argv, struct socks5args* args);

#endif
