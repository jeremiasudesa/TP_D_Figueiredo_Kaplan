#define _POSIX_C_SOURCE 200112L

#include "download.h"
#include "config.h"     // For T_SECONDS, PAYLOAD
#include <stdio.h>      // For perror, fprintf
#include <string.h>     // For memset
#include <unistd.h>     // For read, send, close
#include <time.h>       // For clock_gettime, struct timespec
#include <sys/socket.h> // For socket, connect, send, read
#include <netdb.h>      // For getaddrinfo, struct addrinfo, gai_strerror

int server_handle_download_client(int client_socket_fd)
{
    if (client_socket_fd < 0)
    {
        return DOWNLOAD_PARAM_ERR;
    }

    char buffer[PAYLOAD];
    memset(buffer, 'A', sizeof(buffer)); // Fill buffer with data

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Send data continuously for T_SECONDS
    while (1)
    {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_sec - start_time.tv_sec >= T_SECONDS)
        {
            break;
        }

        if (send(client_socket_fd, buffer, sizeof(buffer), 0) == -1)
        {
            perror("send in server_handle_download_client");
            return DOWNLOAD_SEND_ERR;
        }
    }
    // close(client_socket_fd); // The caller of this function (server_download.c) will close it.
    printf("server: finished sending data to client (fd: %d)\n", client_socket_fd);
    return DOWNLOAD_OK;
}

int client_perform_download(const char *host, const char *port, int duration_seconds, uint64_t *bytes_transferred)
{
    if (!host || !port || duration_seconds <= 0 || !bytes_transferred)
    {
        return DOWNLOAD_PARAM_ERR;
    }

    int s;
    struct addrinfo hints, *servinfo;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo in client_perform_download: %s\n", gai_strerror(status));
        return DOWNLOAD_GETADDRINFO_ERR;
    }

    s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (s == -1)
    {
        perror("socket in client_perform_download");
        freeaddrinfo(servinfo);
        return DOWNLOAD_SOCK_ERR;
    }

    if (connect(s, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        perror("connect in client_perform_download");
        close(s);
        freeaddrinfo(servinfo);
        return DOWNLOAD_CONNECT_ERR;
    }
    freeaddrinfo(servinfo); // all done with this structure

    char buf[PAYLOAD];
    ssize_t n;
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    *bytes_transferred = 0;

    while ((n = read(s, buf, sizeof buf)) > 0)
    {
        *bytes_transferred += n;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec >= duration_seconds)
        {
            break;
        }
    }

    if (n < 0)
    {
        perror("read in client_perform_download");
        close(s);
        return DOWNLOAD_RECV_ERR;
    }

    // close(s); // The caller of this function (client_download.c) will close it.
    return DOWNLOAD_OK;
}
