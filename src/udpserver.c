
#include "udpserver.h"
#include "args.h"
#include "selector.h"
#include "smtpnio.h"

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
#include <netdb.h>       // for getnameinfo
#include <strings.h>     // for strncasecmp

#define RESPONSE_SIZE 16
#define BUFFER_SIZE 1024


enum client_state {
    STATE_INIT,
    STATE_WAIT_USERNAME,
    STATE_WAIT_PASSWORD,
    STATE_AUTH_SUCCESS,
    STATE_AUTH_FAILED
};

typedef struct client {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    enum client_state state;
    struct client *next;
} client_t;

client_t *clients = NULL;

client_t *find_client(struct sockaddr_storage *client_addr, socklen_t client_addr_len) {
    client_t *current = clients;
    while (current != NULL) {
        if (current->client_addr_len == client_addr_len && memcmp(&current->client_addr, client_addr, client_addr_len) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void add_client(struct sockaddr_storage *client_addr, socklen_t client_addr_len) {
    client_t *new_client = (client_t *)malloc(sizeof(client_t));
    memcpy(&new_client->client_addr, client_addr, client_addr_len);
    new_client->client_addr_len = client_addr_len;
    new_client->state = STATE_INIT;
    new_client->next = clients;
    clients = new_client;
}

void uint64_to_big_endian(uint64_t value, uint8_t *buffer) {
    buffer[0] = (value >> 56) & 0xFF;
    buffer[1] = (value >> 48) & 0xFF;
    buffer[2] = (value >> 40) & 0xFF;
    buffer[3] = (value >> 32) & 0xFF;
    buffer[4] = (value >> 24) & 0xFF;
    buffer[5] = (value >> 16) & 0xFF;
    buffer[6] = (value >> 8) & 0xFF;
    buffer[7] = value & 0xFF;
}

void handle_authentication(client_t *client, char *buffer, ssize_t received, int fd, struct sockaddr_storage *client_addr, socklen_t client_addr_len) {
    buffer[strcspn(buffer, "\r\n")] = '\0';

    if (client->state == STATE_WAIT_USERNAME) {
        if (strncasecmp(buffer, "user", received) == 0) {
            client->state = STATE_WAIT_PASSWORD;
            const char *response = "Ingrese contraseña: ";
            sendto(fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_addr_len);
        } else {
            const char *response = "Usuario inexistente. Ingrese usuario: ";
            sendto(fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_addr_len);
        }
    } else if (client->state == STATE_WAIT_PASSWORD) {
        if (strncasecmp(buffer, "user", received) == 0) {
            client->state = STATE_AUTH_SUCCESS;
            const char *response = "Acceso concedido. Puede escribir los comandos.\n";
            sendto(fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_addr_len);
        } else {
            client->state = STATE_WAIT_USERNAME;
            const char *response = "Contraseña incorrecta. Ingrese usuario: ";
            sendto(fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_addr_len);
        }
    }
}

// Manejador de lectura para el socket UDP
void udp_read_handler(struct selector_key *key) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    ssize_t received = recvfrom(key->fd, buffer, sizeof(buffer) - 1, 0,
                                (struct sockaddr *)&client_addr, &client_addr_len);
    if (received < 0) {
        perror("recvfrom");
        return;
    }
    buffer[received] = '\0';

    client_t *client = find_client(&client_addr, client_addr_len);
    if (client == NULL) {
        add_client(&client_addr, client_addr_len);
        client = find_client(&client_addr, client_addr_len);
    }

    if (client->state == STATE_INIT) {
        client->state = STATE_WAIT_USERNAME;
        const char *response = "Ingrese usuario: ";
        sendto(key->fd, response, strlen(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
        return;
    }

    if (client->state == STATE_WAIT_USERNAME || client->state == STATE_WAIT_PASSWORD) {
        handle_authentication(client, buffer, received, key->fd, &client_addr, client_addr_len);
        return;
    }

    if (client->state == STATE_AUTH_SUCCESS) {
        uint8_t response[RESPONSE_SIZE]; 
        size_t offset = 0;

        response[offset++] = 0xFF;
        response[offset++] = 0xFE;

        response[offset++] = 0x00;

        response[offset++] = 0x00;
        response[offset++] = 0x01;

        response[offset++] = 0x00;

        uint64_t cantidad;
        char rta[BUFFER_SIZE];
        switch (buffer[0]) {
        case 'a':
            cantidad = get_historic_users();
            snprintf(rta, sizeof(rta), "Cantidad historica %ld\r\n", cantidad);
            break;

        case 'b':
            cantidad = get_current_users();
            snprintf(rta, sizeof(rta), "Cantidad actual %ld\r\n", cantidad);
            break;
        default:
            cantidad = 1234;
            break;
        }
        uint64_to_big_endian(cantidad, &response[offset]);
        offset += 8;

        response[offset++] = 0x00;
        
        ssize_t sent = sendto(key->fd, rta, strlen(rta), 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent < 0) {
            perror("sendto");
            return;
        }
    }
}
