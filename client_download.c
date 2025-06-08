/* Client for download-throughput test – keeps original variable names */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <netdb.h>
#include "config.h"

struct thr_arg
{
    const char *host;
    uint64_t bytes;
};

static void *recv_thread(void *vp)
{
    struct thr_arg *arg = vp;
    int s;
    struct addrinfo hints, *servinfo;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(arg->host, TCP_PORT_DOWN, &hints, &servinfo)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return NULL;
    }
    s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (connect(s, servinfo->ai_addr, servinfo->ai_addrlen))
    {
        perror("connect");
        return NULL;
    }

    char buf[PAYLOAD];
    ssize_t n;
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    while ((n = read(s, buf, sizeof buf)) > 0)
    {
        arg->bytes += n;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec >= T_SECONDS)
            break;
    }
    close(s);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s host N_conexiones\n", argv[0]);
        return 1;
    }
    int N = atoi(argv[2]);
    pthread_t *tid = calloc(N, sizeof *tid);
    struct thr_arg *targ = calloc(N, sizeof *targ);

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int i = 0; i < N; ++i)
    {
        targ[i].host = argv[1];
        pthread_create(&tid[i], NULL, recv_thread, &targ[i]);
    }
    uint64_t total_bytes = 0;
    for (int i = 0; i < N; ++i)
    {
        pthread_join(tid[i], NULL);
        total_bytes += targ[i].bytes;
    }
    clock_gettime(CLOCK_MONOTONIC, &t_end);

    double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    printf("me llegaron %lu\n bytes", total_bytes);
    printf("Pasaron %.3f segundos\n", elapsed);
    printf("Entonces, el throughput es %.2f Mb/s\n",
           (total_bytes * 8.0) / (elapsed * 1e6));

    free(tid);
    free(targ);
    return 0;
}
