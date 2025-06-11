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
#include "download.h"
#include "latency.h"

struct thr_arg
{
    const char *host;
    uint64_t bytes;
};

struct latency_arg
{
    const char *host;
    double *rtts;
    int num_measurements;
    int completed;
};

static void *recv_thread(void *vp)
{
    struct thr_arg *arg = vp;

    int result = client_perform_download(arg->host, TCP_PORT_DOWN, T_SECONDS, &arg->bytes);
    if (result != DOWNLOAD_OK)
        fprintf(stderr, "Error in download thread: %d\n", result);

    return NULL;
}

static void *latency_thread(void *vp)
{
    struct latency_arg *arg = vp;

    printf("client: starting latency measurements during download...\n");

    int result = client_measure_latency(arg->host, arg->num_measurements, 10, arg->rtts);
    if (result != LAT_OK)
    {
        fprintf(stderr, "Error in latency measurements: %d\n", result);
        arg->completed = 0;
    }
    else
    {
        arg->completed = 1;
        printf("client: completed %d latency measurements\n", arg->num_measurements);
    }

    return NULL;
}

int run_pipeline(const char *host, int num_connections)
{
    pthread_t *download_tids = calloc(num_connections, sizeof(pthread_t));
    struct thr_arg *download_args = calloc(num_connections, sizeof(struct thr_arg));

    int num_latency_measurements = RTT_TRIES;
    double *rtts = calloc(num_latency_measurements, sizeof(double));
    struct latency_arg lat_arg = {
        .host = host,
        .rtts = rtts,
        .num_measurements = num_latency_measurements,
        .completed = 0};
    pthread_t latency_tid;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    if (pthread_create(&latency_tid, NULL, latency_thread, &lat_arg) != 0)
        perror("pthread_create for latency");

    for (int i = 0; i < num_connections; ++i)
    {
        download_args[i].host = host;
        pthread_create(&download_tids[i], NULL, recv_thread, &download_args[i]);
    }

    uint64_t total_bytes = 0;
    for (int i = 0; i < num_connections; ++i)
    {
        pthread_join(download_tids[i], NULL);
        total_bytes += download_args[i].bytes;
    }

    pthread_join(latency_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    printf("me llegaron %lu bytes\n", total_bytes);
    printf("Pasaron %.3f segundos\n", elapsed);
    printf("Entonces, el throughput es %.2f Mb/s\n", (total_bytes * 8.0) / (elapsed * 1e6));

    if (lat_arg.completed)
    {
        double min_rtt = rtts[0], max_rtt = rtts[0], avg_rtt = 0;
        for (int i = 0; i < num_latency_measurements; i++)
        {
            if (rtts[i] > 0)
            {
                if (rtts[i] < min_rtt)
                    min_rtt = rtts[i];
                if (rtts[i] > max_rtt)
                    max_rtt = rtts[i];
                avg_rtt += rtts[i];
            }
        }
        avg_rtt /= num_latency_measurements;

        printf("\nLatency measurements during download:\n");
        printf("Min RTT: %.3f ms\n", min_rtt * 1000);
        printf("Max RTT: %.3f ms\n", max_rtt * 1000);
        printf("Avg RTT: %.3f ms\n", avg_rtt * 1000);
    }

    free(download_tids);
    free(download_args);
    free(rtts);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s host\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];

    printf("Starting download and latency test pipeline for host: %s with %d connection(s).\n", host, N_CONN);
    int result = run_pipeline(host, N_CONN);

    if (result == 0)
        printf("Pipeline completed successfully.\n");
    else
        printf("Pipeline encountered an error.\n");

    return result;
}
