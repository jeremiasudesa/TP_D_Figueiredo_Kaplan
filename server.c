#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "download.h"
#include "latency.h"

#define IPV4_STRLEN 16
#define MAX_LATENCY_REQUESTS 1000

static int create_listening_socket(const char *port);
static void *latency_server_thread();
static void *download_worker(void *arg);

static int create_listening_socket(const char *port)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, port, &hints, &res);
    if (status != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1)
    {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    const int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
    {
        perror("setsockopt");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    if (listen(fd, MAX_BACKLOG) == -1)
    {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static void *latency_server_thread()
{
    printf("server: UDP latency service on port %d …\n", UDP_SERVER_PORT);

    while (1)
    {
        int rc = server_measure_latency(MAX_LATENCY_REQUESTS);
        if (rc != LAT_OK)
        {
            fprintf(stderr, "latency server error: %d\n", rc);
            sleep(1);
        }
    }
    return NULL;
}

static void *download_worker(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    int rc = server_handle_download_client(client_fd);
    if (rc != DOWNLOAD_OK)
        fprintf(stderr, "download handler error: %d\n", rc);

    close(client_fd);
    return NULL;
}

int main()
{
    pthread_t latency_thr;
    if (pthread_create(&latency_thr, NULL, latency_server_thread, NULL) != 0)
    {
        perror("pthread_create (latency thread)");
        return EXIT_FAILURE;
    }
    pthread_detach(latency_thr);

    int srv_fd = create_listening_socket(TCP_PORT_DOWN);
    if (srv_fd < 0)
        return EXIT_FAILURE;

    printf("server: waiting for TCP connections on port %s …\n", TCP_PORT_DOWN);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof client_addr;

        int cli_fd = accept(srv_fd, (struct sockaddr *)&client_addr, &len);
        if (cli_fd == -1)
        {
            perror("accept");
            continue;
        }

        char ip[IPV4_STRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof ip);
        printf("server: connection from %s\n", ip);

        int *fd_ptr = malloc(sizeof *fd_ptr);
        if (!fd_ptr)
        {
            perror("malloc");
            close(cli_fd);
            continue;
        }
        *fd_ptr = cli_fd;

        pthread_t thr;
        if (pthread_create(&thr, NULL, download_worker, fd_ptr) != 0)
        {
            perror("pthread_create (worker)");
            close(cli_fd);
            free(fd_ptr);
            continue;
        }
        pthread_detach(thr);
    }

    close(srv_fd);
    return EXIT_SUCCESS;
}
