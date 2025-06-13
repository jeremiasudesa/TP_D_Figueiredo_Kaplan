#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h> /* For select(), fd_set, FD_ZERO, FD_SET */

#include "config.h"
#include "download.h"
#include "latency.h"
#include "upload.h"
#include "handle_result.h" /* For struct BW_result and packResultPayload */
#include "common.h"        /* For results_lock_t, udp_socket_init, now_ts, diff_ts, die */

#define IPV4_STRLEN 16
#define MAX_LATENCY_REQUESTS 1000

static int create_listening_socket(const char *port);
static void *latency_server_thread();
static void *download_worker(void *arg);
static void *upload_worker(void *arg);

typedef struct upload_worker_args
{
    results_lock_t *results_lock;
} upload_worker_args_t;

// Upload server handler thread
static void *upload_worker(void *arg)
{
    upload_worker_args_t *args = arg;
    server_upload(N_CONN, T_SECONDS, args->results_lock);
    return NULL;
}

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
    // Initicializo lista de resultados para los clientes
    results_lock_t results_lock;
    if (pthread_mutex_init(&results_lock.mutex, NULL) != 0)
    {
        perror("pthread_mutex_init");
        return EXIT_FAILURE;
    }

    results_lock.results = calloc(MAX_CLIENTS, sizeof *results_lock.results);
    if (!results_lock.results)
    {
        perror("calloc");
        pthread_mutex_destroy(&results_lock.mutex);
        return EXIT_FAILURE;
    }

    // Empiezo latency echo
    pthread_t latency_thr;
    echo_server_args_t echo_args = {.results_lock = &results_lock};
    if (pthread_create(&latency_thr, NULL, latency_echo_server, &echo_args) != 0)
    {
        perror("pthread_create (latency thread)");
        return EXIT_FAILURE;
    }
    pthread_detach(latency_thr);

    // Ahora para upload
    pthread_t upload_thr;
    upload_worker_args_t up_args = {.results_lock = &results_lock};
    if (pthread_create(&upload_thr, NULL, upload_worker, &up_args) != 0)
    {
        perror("pthread_create (upload thread)");
        return EXIT_FAILURE;
    }
    pthread_detach(upload_thr);

    // Set up download service in main thread
    int srv_fd = create_listening_socket(TCP_PORT_DOWN);
    if (srv_fd < 0)
        return EXIT_FAILURE;

    printf("server: waiting for TCP connections on port %s (download) and port %d (upload)...\n",
           TCP_PORT_DOWN, TCP_PORT_UPLOAD);

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
        printf("server: download connection from %s\n", ip);

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
