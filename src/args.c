#include "args.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h> /* LONG_MIN et al */
#include <stdio.h>  /* for printf */
#include <stdlib.h> /* for exit */
#include <string.h> /* memset */

static unsigned short
port(const char* s)
{
	char* end = 0;
	const long sl = strtol(s, &end, 10);

	if (end == s || '\0' != *end || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno) || sl < 0 ||
	    sl > USHRT_MAX) {
		fprintf(stderr, "port should in in the range of 1-65536: %s\n", s);
		exit(1);
		return 1;
	}
	return (unsigned short)sl;
}

static void
user(char* s, struct users* user)
{
	char* p = strchr(s, ':');
	if (p == NULL) {
		fprintf(stderr, "password not found\n");
		exit(1);
	} else {
		*p = 0;
		p++;
		user->name = s;
		user->pass = p;
	}
}

static void
version(void)
{
	fprintf(stderr,
	        "smtpd version 0.0\n"
	        "ITBA Protocolos de Comunicación 2024/1 -- Grupo X\n"
	        "AQUI VA LA LICENCIA\n");
}

static void
usage(const char* progname)
{
	fprintf(stderr,
	        "Usage: %s [OPTION]...\n"
	        "\n"
	        "   -h               Imprime la ayuda y termina.\n"
	        "   -l <SMTP addr>  Dirección donde servirá el servidor SMTP.\n"
	        "   -L <conf  addr>  Dirección donde servirá el servicio de management.\n"
	        "   -p <SMTP port>  Puerto entrante conexiones SMTP.\n"
	        "   -P <conf port>   Puerto entrante conexiones configuracion\n"
	        "   -u <name>:<pass> Usuario y contraseña de usuario que puede usar el proxy. Hasta 10.\n"
			"   -T               Apaga las transformaciones.\n"
	        "   -v               Imprime información sobre la versión versión y termina.\n"
	        "\n"
	        "   --doh-ip    <ip>    \n"
	        "   --doh-port  <port>  XXX\n"
	        "   --doh-host  <host>  XXX\n"
	        "   --doh-path  <host>  XXX\n"
	        "   --doh-query <host>  XXX\n"

	        "\n",
	        progname);
	exit(1);
}

void
parse_args(const int argc, char** argv, struct socks5args* args)
{
	memset(args, 0, sizeof(*args));  // sobre todo para setear en null los punteros de users

	args->smtp_addr = "0.0.0.0";
	args->smtp_port = 1209;

	args->mng_addr = "127.0.0.1";
	args->mng_port = 8080;

	args->transform_enabled = true;

	args->doh.host = "localhost";
	args->doh.ip = "127.0.0.1";
	args->doh.port = 8053;
	args->doh.path = "/getnsrecord";
	args->doh.query = "?dns=";

	int c;
	int nusers = 0;

	while (true) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "doh-ip", required_argument, 0, 0xD001 },    { "doh-port", required_argument, 0, 0xD002 },
			{ "doh-host", required_argument, 0, 0xD003 },  { "doh-path", required_argument, 0, 0xD004 },
			{ "doh-query", required_argument, 0, 0xD005 }, { 0, 0, 0, 0 }
		};

		c = getopt_long(argc, argv, "hl:L:Tp:P:u:v", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'h': {
				usage(argv[0]);
			} break;

			case 'l': {
				args->smtp_addr = optarg;
			} break;

			case 'L':{
				args->mng_addr = optarg;
			} break;

			case 'N': {
				args->transform_enabled = false;
			} break;

			case 'p': {
				args->smtp_port = port(optarg);
			} break;

			case 'P': {
				args->mng_port = port(optarg);
			} break;

			case 'u': {
				if (nusers >= MAX_USERS) {
					fprintf(stderr, "maximun number of command line users reached: %d.\n", MAX_USERS);
					exit(1);
				} else {
					user(optarg, args->users + nusers);
					nusers++;
				}
			} break;

			case 'v': {
				version();
				exit(0);
			} break;

			case 0xD001: {
				args->doh.ip = optarg;
			} break;

			case 0xD002: {
				args->doh.port = port(optarg);
			} break;

			case 0xD003: {
				args->doh.host = optarg;
			} break;

			case 0xD004: {
				args->doh.path = optarg;
			} break;

			case 0xD005: {
				args->doh.query = optarg;
			} break;

			default: {
				fprintf(stderr, "unknown argument %d.\n", c);
				exit(1);
			} break;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "argument not accepted: ");
		while (optind < argc) {
			fprintf(stderr, "%s ", argv[optind++]);
		}
		fprintf(stderr, "\n");
		exit(1);
	}
}
