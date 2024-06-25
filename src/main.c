/**
 * main.c - servidor proxy socks concurrente
 *
 * Interpreta los argumentos de línea de comandos, y monta un socket
 * pasivo.
 *
 * Todas las conexiones entrantes se manejarán en éste hilo.
 *
 * Se descargará en otro hilos las operaciones bloqueantes (resolución de
 * DNS utilizando getaddrinfo), pero toda esa complejidad está oculta en
 * el selector.
 */
#include "args.h"
#include "selector.h"
#include "smtpnio.h"
#include "udpserver.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>  // socket
#include <sys/types.h>   // socket
#include <unistd.h>
#include <arpa/inet.h>

#define RESPONSE_SIZE 16

static bool done = false;

static void
sigterm_handler(const int signal)
{
    printf("signal %d, cleaning up and exiting\n", signal);
    done = true;
}



int main(int argc, char** argv) {
    struct socks5args args;
    parse_args(argc, argv, &args);

    close(0);

    const char* err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(args.smtp_port);

    const int server = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    const int server_6969 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (server < 0) {
        err_msg = "unable to create socket";
        goto finally_tcp;
    }

    if (server_6969 < 0) {
        err_msg = "unable to create socket for port 6969";
        goto finally_udp;
    }

    fprintf(stdout, "Listening on TCP port %d\n", args.smtp_port);

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &(int){ 0 }, sizeof(int));

    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        err_msg = "unable to bind socket";
        goto finally;
    }

    if (listen(server, 20) < 0) {
        err_msg = "unable to listen";
        goto finally;
    }

    // Socket en el puerto 6969 con UDP
    struct sockaddr_in6 addr_6969;
    memset(&addr_6969, 0, sizeof(addr_6969));
    addr_6969.sin6_family = AF_INET6;
    addr_6969.sin6_addr = in6addr_any;
    addr_6969.sin6_port = htons(6969);

    fprintf(stdout, "Listening on UDP port 6969\n");

    setsockopt(server_6969, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    if (bind(server_6969, (struct sockaddr*)&addr_6969, sizeof(addr_6969)) < 0) {
        err_msg = "unable to bind socket for port 6969";
        goto finally;
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    if (selector_fd_set_nio(server) == -1) {
        err_msg = "getting server socket flags";
        goto finally;
    }

    if (selector_fd_set_nio(server_6969) == -1) {
        err_msg = "getting server_6969 socket flags";
        goto finally;
    }

    const struct selector_init conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0,
        },
    };

    if (0 != selector_init(&conf)) {
        err_msg = "initializing selector";
        goto finally;
    }

    selector = selector_new(1024);

    if (selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }

    const struct fd_handler smtp = {
        .handle_read = smtp_passive_accept,
        .handle_write = NULL,
        .handle_close = NULL,
    };

    const struct fd_handler udp = {
        .handle_read = udp_read_handler,
        .handle_write = NULL,
        .handle_close = NULL,
    };

    ss = selector_register(selector, server, &smtp, OP_READ, NULL);

    if (ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd";
        goto finally;
    }

    ss = selector_register(selector, server_6969, &udp, OP_READ, NULL);

    if (ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd for port 6969";
        goto finally;
    }

    while (!done) {
        err_msg = NULL;
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }

    if (err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;

finally_udp:
    if (server_6969 >= 0) {
        close(server_6969);
    }

finally_tcp:
    if (server >= 0) {
        close(server);
    }

finally:
    if (ss != SELECTOR_SUCCESS) {
        fprintf(stderr,
                "%s: %s\n",
                (err_msg == NULL) ? "" : err_msg,
                ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
        ret = 2;
    } else if (err_msg) {
        perror(err_msg);
        ret = 1;
    }

    if (selector != NULL) {
        selector_destroy(selector);
    }

    selector_close();
    return ret;
}
