#include "udpserver.h"

#include "selector.h"
#include "smtpnio.h"

#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>  // for getnameinfo
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     // for strncasecmp
#include <sys/socket.h>  // socket
#include <sys/types.h>   // socket
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>

#define RESPONSE_SIZE 16
#define BUFFER_SIZE   1024

enum client_state
{
	STATE_INIT,
	STATE_WAIT_USERNAME,
	STATE_WAIT_PASSWORD,
	STATE_AUTH_SUCCESS,
	STATE_AUTH_FAILED
};

typedef struct client
{
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len;
	enum client_state state;
	struct client* next;
} client_t;

client_t *clients = NULL;
const char *help = "HELP\r\n - Ingrese 'historico' para obtener el historico de usuarios conectados\r\n - Ingrese 'actual' para obtener los usuarios conectados ahora\r\n - Ingrese 'mail' para obtener la cantidad de mails enviados\r\n - Ingrese 'bytes' para obtener la cantidad de bytes transferidos\r\n - Ingrese 'status' para ver el estado de las transformaciones\r\n - Ingrese 'transon' para activar las transformaciones\r\n - Ingrese 'transoff' para desactivar las transformaciones\r\n - Ingrese 'max <cant>' para poner una nueva cantidad de usuarios\r\n - Ingrese 'cant' para obtener la maxima cantidad de usuarios\r\n";

client_t*
find_client(struct sockaddr_storage* client_addr, socklen_t client_addr_len)
{
	client_t* current = clients;
	while (current != NULL) {
		if (current->client_addr_len == client_addr_len &&
		    memcmp(&current->client_addr, client_addr, client_addr_len) == 0) {
			return current;
		}
		current = current->next;
	}
	return NULL;
}

void
add_client(struct sockaddr_storage* client_addr, socklen_t client_addr_len)
{
	client_t* new_client = (client_t*)malloc(sizeof(client_t));
	if (new_client == NULL) {
		perror("malloc");
		return;
	}

	memcpy(&new_client->client_addr, client_addr, client_addr_len);
	new_client->client_addr_len = client_addr_len;
	new_client->state = STATE_INIT;
	new_client->next = clients;
	clients = new_client;
}

void
uint64_to_big_endian(uint64_t value, uint8_t* buffer)
{
	buffer[0] = (value >> 56) & 0xFF;
	buffer[1] = (value >> 48) & 0xFF;
	buffer[2] = (value >> 40) & 0xFF;
	buffer[3] = (value >> 32) & 0xFF;
	buffer[4] = (value >> 24) & 0xFF;
	buffer[5] = (value >> 16) & 0xFF;
	buffer[6] = (value >> 8) & 0xFF;
	buffer[7] = value & 0xFF;
}

void
handle_authentication(client_t* client,
                      char* buffer,
                      ssize_t received,
                      int fd,
                      struct sockaddr_storage* client_addr,
                      socklen_t client_addr_len)
{
	buffer[strcspn(buffer, "\r\n")] = '\0';

	if (client->state == STATE_WAIT_USERNAME) {
		if (strncasecmp(buffer, "user", received) == 0) {
			client->state = STATE_WAIT_PASSWORD;
			const char* response = "Ingrese contraseña: ";
			sendto(fd, response, strlen(response), 0, (struct sockaddr*)client_addr, client_addr_len);
		} else {
			const char* response = "Usuario inexistente. Ingrese usuario: ";
			sendto(fd, response, strlen(response), 0, (struct sockaddr*)client_addr, client_addr_len);
		}
	} else if (client->state == STATE_WAIT_PASSWORD) {
		if (strncasecmp(buffer, "user", received) == 0) {
			client->state = STATE_AUTH_SUCCESS;
			char* response = "Acceso concedido. Puede escribir los comandos.\r\n";
			sendto(fd, response, strlen(response), 0, (struct sockaddr*)client_addr, client_addr_len);
		} else {
			client->state = STATE_WAIT_USERNAME;
			const char* response = "Contraseña incorrecta. Ingrese usuario: ";
			sendto(fd, response, strlen(response), 0, (struct sockaddr*)client_addr, client_addr_len);
		}
	}
}


bool is_number(const char *str) {
    while (*str) {
        if (!isdigit(*str)) {
            return false;
        }
        str++;
    }
    return true;
}


// Manejador de lectura para el socket UDP
void
udp_read_handler(struct selector_key* key)
{
	char buffer[BUFFER_SIZE];
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	ssize_t received =
	    recvfrom(key->fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&client_addr, &client_addr_len);
	if (received < 0) {
		perror("recvfrom");
		return;
	}
	buffer[received] = '\0';

	client_t* client = find_client(&client_addr, client_addr_len);
	if (client == NULL) {
		add_client(&client_addr, client_addr_len);
		client = find_client(&client_addr, client_addr_len);
	}

	if (client->state == STATE_INIT) {
		client->state = STATE_WAIT_USERNAME;
		const char* response = "Ingrese usuario: ";
		sendto(key->fd, response, strlen(response), 0, (struct sockaddr*)&client_addr, client_addr_len);
		return;
	}

	if (client->state == STATE_WAIT_USERNAME || client->state == STATE_WAIT_PASSWORD) {
		handle_authentication(client, buffer, received, key->fd, &client_addr, client_addr_len);

		if (client->state == STATE_AUTH_SUCCESS) {
			sendto(key->fd, help, strlen(help), 0, (struct sockaddr*)&client_addr, client_addr_len);
		}
		return;
	}

    if (client->state == STATE_AUTH_SUCCESS) {
        uint64_t cantidad=0;
        char rta[BUFFER_SIZE];

        if (strcasecmp(buffer, "historico\r\n") == 0) {
            cantidad = get_historic_users();
            snprintf(rta, BUFFER_SIZE, "Cantidad historica %ld\r\n\r\n", cantidad);
        } else if (strcasecmp(buffer, "actual\r\n") == 0) {
            cantidad = get_current_users();
            snprintf(rta, BUFFER_SIZE, "Cantidad actual %ld\r\n\r\n", cantidad);
        }else if (strcasecmp(buffer, "bytes\r\n") == 0) {
            cantidad = get_current_bytes();
            snprintf(rta, BUFFER_SIZE, "Bytes transferidos %ld\r\n\r\n", cantidad);
        }else if (strcasecmp(buffer, "mail\r\n") == 0) {
            cantidad = get_current_mails();
            snprintf(rta, BUFFER_SIZE, "Mails enviados %ld\r\n\r\n", cantidad);
        }else if (strcasecmp(buffer, "cant\r\n") == 0) {
            cantidad = get_cant_max_users();
            snprintf(rta, BUFFER_SIZE, "Cantidad maxima de usuarios %ld\r\n\r\n", cantidad);
        }else if (strcasecmp(buffer, "status\r\n") == 0) {
            bool status = get_current_status();
            if(status){
                snprintf(rta, BUFFER_SIZE, "Las transormaciones estan activadas\r\n\r\n");
            }else{
                snprintf(rta, BUFFER_SIZE, "Las transormaciones estan desactivadas\r\n\r\n");
            }
        }else if (strcasecmp(buffer, "transon\r\n") == 0) {
            set_new_status(true);
            snprintf(rta, BUFFER_SIZE, "Transormaciones activadas\r\n\r\n");
        }else if (strcasecmp(buffer, "transoff\r\n") == 0) {
            set_new_status(false);
            snprintf(rta, BUFFER_SIZE, "Transormaciones desactivadas\r\n\r\n");
        }else if (strcasecmp(buffer, "help\r\n") == 0) {
            snprintf(rta, BUFFER_SIZE, "%s\r\n\r\n", help);
            if ((buffer[0] == 'm' || buffer[0] == 'M') &&
                (buffer[1] == 'a' || buffer[1] == 'A') &&
                (buffer[2] == 'x' || buffer[2] == 'X') &&
                (buffer[3] == ' ')) {

                char *number_str = &buffer[4];
                number_str[strcspn(number_str, "\r\n")] = '\0';
                int number;

                if (is_number(number_str)) {
                    number = atoi(number_str);
                    if (number >= 0) {
                        set_max_users(number);
                        snprintf(rta, BUFFER_SIZE, "Nuevo número máximo de usuarios: %d\r\n\r\n", number);
                    } else {
                        snprintf(rta, BUFFER_SIZE, "Error: Argumento inválido\r\n\r\n");
                    }
                } else {
                    snprintf(rta, BUFFER_SIZE, "Error: Argumento inválido\r\n\r\n");
                }
            } else {
                snprintf(rta, BUFFER_SIZE, "Comando no reconocido\r\n %s", help);
            }
        }

        ssize_t sent = sendto(key->fd, rta, strlen(rta), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent < 0) {
            perror("sendto");
            return;
        }
    }
}
