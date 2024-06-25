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

#define RESPONSE_SIZE 16

/*
* RESPONSE:
*   Protocol signature - 2 bytes - 0xFF 0xFE 
*   Versión del protocolo - 1 byte -  0x00
*   Identificador del request - 2 bytes
*   Status - 1 byte
*       Success - 0x00
*   Errors:
*       Auth failed - 0x01
*       Invalid version - 0x02
*       Invalid command - 0x03
*       Invalid request (length) - 0x04
*       Unexpected error - 0x05
*       0x06 - 0xFF saved for future errors
*   Cantidad - (uint64_t) 8 bytes unsigned en Big Endian (Network Order)
*   Booleano (0x00 TRUE - 0x01 FALSE) - 1 byte
*/

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

// Manejador de lectura para el socket UDP
void udp_read_handler(struct selector_key *key) {
    char buffer[1024];
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    ssize_t received = recvfrom(key->fd, buffer, sizeof(buffer) - 1, 0,
                                (struct sockaddr *)&client_addr, &client_addr_len);
    if (received < 0) {
        perror("recvfrom");
        return;
    }
    buffer[received] = '\0';
    // printf("UDP data: %s\n", buffer);

    uint8_t response[RESPONSE_SIZE]; // 2 + 1 + 2 + 1 + 8 + 1 = 15 bytes + 1 byte de padding
    size_t offset = 0;

    // Protocol signature
    response[offset++] = 0xFF;
    response[offset++] = 0xFE;

    // Versión del protocolo
    response[offset++] = 0x00;

    // Identificador del request (por simplicidad, aquí usamos 0x0001)
    response[offset++] = 0x00;
    response[offset++] = 0x01;

    // Status (Success y manejar errores)
    response[offset++] = 0x00;

    // Cantidad (traer los datos posta Big Endian)
    uint64_t cantidad;
    char rta[128];  // Buffer para almacenar la cadena formateada
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
        cantidad = 1234; // valor falopa para testear
        break;
    }
    // printf("Cant: %ld\n", cantidad);
    uint64_to_big_endian(cantidad, &response[offset]);
    offset += 8;

    response[offset++] = 0x00;
    // Booleano (TRUE)
    printf("response: ");
    for (size_t i = 0; i < offset; i++) {
        printf("%d ", response[i]);
    }
    printf("\n");

    printf("Client address: ");
    char client_address_str[INET6_ADDRSTRLEN];
    void *addr;
    if (client_addr.ss_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)&client_addr;
        addr = &(ipv4->sin_addr);
    } else { // AF_INET6
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&client_addr;
        addr = &(ipv6->sin6_addr);
    }
    if (inet_ntop(client_addr.ss_family, addr, client_address_str, sizeof(client_address_str)) == NULL) {
        perror("inet_ntop");
        return;
    }
    printf("%s\n", client_address_str);

    // Validación adicional de la dirección del cliente (puedes ajustar según tus necesidades)
    // Aquí no se está validando específicamente, puedes agregar tu lógica de validación aquí.

    ssize_t sent = sendto(key->fd, rta, strlen(rta), 0,
                          (struct sockaddr *)&client_addr, client_addr_len);
    if (sent < 0) {
        printf("Hay error y sent es: %ld\n", sent);
        perror("sendto");
        return;
    }

    // Lógica adicional de los paquetes UDP
}
