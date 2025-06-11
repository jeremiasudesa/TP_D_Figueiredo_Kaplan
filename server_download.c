/* Demostración de arquitectura de un server con multithreading */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h> /* necesario para memset() */
#include <errno.h>  /* necesario para codigos de errores */
#include <netinet/in.h>
#include <netdb.h> /* necesario para getaddrinfo() */
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include "config.h"
#include "download.h"
#include "latency.h"

#define INET4_ADDRSTRLEN 16

void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg); /* Free the allocated memory for socket descriptor */

    int result = server_handle_download_client(client_socket);
    if (result != DOWNLOAD_OK)
    {
        fprintf(stderr, "Error handling download client: %d\n", result);
    }

    close(client_socket);
    return NULL;
}

void *latency_server_thread(void *arg)
{
    printf("server: starting UDP latency server on port %d...\n", UDP_SERVER_PORT);

    // Run latency server continuously
    while (1)
    {
        int result = server_measure_latency(1000); // Handle up to 1000 latency requests
        if (result != LAT_OK)
        {
            fprintf(stderr, "Error in latency server: %d\n", result);
            break;
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int s; /*file descriptor*/
    struct addrinfo hints;
    struct addrinfo *servinfo; /* es donde van los resultados */
    int status;
    pthread_t latency_thread;

    if (pthread_create(&latency_thread, NULL, latency_server_thread, NULL) != 0)
    {
        perror("pthread_create for latency server");
        return 1;
    }
    pthread_detach(latency_thread);

    memset(&hints, 0, sizeof(hints)); /* es necesario inicializar con ceros */
    hints.ai_family = AF_INET;        /* Address Family */
    hints.ai_socktype = SOCK_STREAM;  /* Socket Type */
    hints.ai_flags = AI_PASSIVE;      /* Llena la IP por mi por favor */

    if ((status = getaddrinfo(NULL, TCP_PORT_DOWN, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "Error en getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (s == -1)
    {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("setsockopt");
        return 1;
    }

    if ((status = bind(s, servinfo->ai_addr, servinfo->ai_addrlen)))
    {
        fprintf(stderr, "Error en bind (%d): %s\n", status, strerror(errno));
        return 1;
    }

    if ((status = listen(s, MAX_BACKLOG)))
    {
        fprintf(stderr, "Error en listen (%d) : %s\n", status, strerror(errno));
        return 1;
    }

    printf("server: waiting for TCP download connections on port %s...\n", TCP_PORT_DOWN);

    int new_s;
    struct sockaddr_in their_addr;
    socklen_t addrsize;
    char addrstr[INET4_ADDRSTRLEN];
    pthread_t thread;

    while (1)
    {
        addrsize = sizeof their_addr;
        new_s = accept(s, (struct sockaddr *)&their_addr, &addrsize);
        if (new_s == -1)
        {
            perror("accept");
            continue;
        }

        inet_ntop(AF_INET, &(their_addr.sin_addr), addrstr, sizeof addrstr);
        printf("server: got TCP connection from %s\n", addrstr);

        int *client_socket = malloc(sizeof(int));
        *client_socket = new_s;

        if (pthread_create(&thread, NULL, handle_client, client_socket) != 0)
        {
            perror("pthread_create");
            close(new_s);
            free(client_socket);
            continue;
        }

        pthread_detach(thread);
    }

    close(s);
    return 0;
}
