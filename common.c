#define _POSIX_C_SOURCE 199309L

#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h> // For getaddrinfo

int udp_socket_init(const char *host, int port, struct sockaddr_in *server_addr_out, int do_bind)
{
    int sockfd = -1;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    int rv;
    char port_str[6];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // Use AF_INET for sockaddr_in
    hints.ai_socktype = SOCK_DGRAM;

    if (do_bind && host == NULL) // Server binding to any IP
    {
        hints.ai_flags = AI_PASSIVE;
    }

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo in udp_socket_init for host '%s': %s\n", host, gai_strerror(rv));
        return -1;
    }

    // Loop through all the results and try to create socket and bind if needed
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET) // We are interested in IPv4 for sockaddr_in
        {
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                perror("udp_socket_init: socket");
                continue;
            }

            if (do_bind) // Server: bind to the address
            {
                if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
                {
                    close(sockfd);
                    perror("udp_socket_init: bind");
                    sockfd = -1; // Mark as failed
                    continue;
                }
            }
            // If successful, copy the sockaddr_in structure to server_addr_out
            memcpy(server_addr_out, p->ai_addr, sizeof(struct sockaddr_in));
            break; // Successfully created/bound a socket
        }
    }

    if (p == NULL || sockfd == -1)
    {
        fprintf(stderr, "udp_socket_init: failed to create/bind socket for host '%s'\n", host);
        if (servinfo)
            freeaddrinfo(servinfo);
        return -1;
    }

    freeaddrinfo(servinfo); // All done with this structure
    return sockfd;
}

struct timespec now_ts(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

double diff_ts(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}