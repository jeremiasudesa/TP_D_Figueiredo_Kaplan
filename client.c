#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h> // Added for inet_ntop
#include "config.h"
#include "download.h"
#include "latency.h"
#include "upload.h"

struct watchdog_arg
{
    pthread_t *thread_to_watch;
    int timeout_seconds;
    volatile int *complete_flag;
};

// Watchdog thread function to monitor other threads with timeout
static void *watchdog_thread(void *arg)
{
    struct watchdog_arg *warg = (struct watchdog_arg *)arg;

    // Sleep for short intervals and check if the main thread has
    // marked the operation as complete
    for (int i = 0; i < warg->timeout_seconds && !*(warg->complete_flag); i++)
    {
        sleep(1);
    }

    // If operation is not complete after timeout, cancel the thread
    if (!*(warg->complete_flag))
    {
        printf("Thread timed out after %d seconds. Cancelling...\n",
               warg->timeout_seconds);
        pthread_cancel(*(warg->thread_to_watch));
    }

    return NULL;
}

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

// Estructura para mantener todos los resultados
struct test_results
{
    const char *src_ip;
    const char *dst_ip;
    uint64_t download_bytes;
    double download_elapsed;
    double avg_bw_download_bps;
    double upload_elapsed;
    double avg_bw_upload_bps;
    int num_conns;
    double rtt_idle;
    double rtt_download;
    double rtt_upload;
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

static void *upload_thread(void *vp)
{
    struct thr_arg *arg = vp;

    int result = client_upload(arg->host, N_CONN);
    if (result < 0)
        fprintf(stderr, "Error in upload thread: %d\n", result);
    else
        arg->bytes = result; // Store the returned bytes from upload

    return NULL;
}

// Exporta los resultados en formato JSON via UDP
int export_results_json(const struct test_results *results, const char *logstash_ip, int logstash_port)
{
    char json_buffer[1024];    // Buffer para el JSON
    char timestamp_buffer[32]; // Buffer para el timestamp

    // Obtener timestamp actual
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timestamp_buffer, sizeof(timestamp_buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Crear JSON en buffer
    snprintf(json_buffer, sizeof(json_buffer),
             "{"
             "\"src_ip\": \"%s\","
             "\"dst_ip\": \"%s\","
             "\"timestamp\": \"%s\","
             "\"avg_bw_download_bps\": %.0f,"
             "\"avg_bw_upload_bps\": %.0f,"
             "\"num_conns\": %d,"
             "\"rtt_idle\": %.3f,"
             "\"rtt_download\": %.3f,"
             "\"rtt_upload\": %.3f"
             "}",
             results->src_ip,
             results->dst_ip,
             timestamp_buffer,
             results->avg_bw_download_bps,
             results->avg_bw_upload_bps,
             results->num_conns,
             results->rtt_idle,
             results->rtt_download,
             results->rtt_upload);

    // Crear socket UDP
    struct sockaddr_in logstash_addr;
    int sock = udp_socket_init(logstash_ip, logstash_port, &logstash_addr, 0);
    if (sock < 0)
    {
        fprintf(stderr, "Error creating socket for JSON export\n");
        return -1;
    }

    // Enviar JSON
    if (sendto(sock, json_buffer, strlen(json_buffer), 0,
               (struct sockaddr *)&logstash_addr, sizeof(logstash_addr)) < 0)
    {
        perror("sendto JSON");
        close(sock);
        return -1;
    }

    printf("Results exported in JSON format to %s:%d\n", logstash_ip, logstash_port);
    printf("JSON payload: %s\n", json_buffer);

    close(sock);
    return 0;
}

int run_pipeline(const char *host, int num_connections)
{
    // Variables for storing results
    uint64_t download_total_bytes = 0;
    double download_elapsed = 0;
    double download_min_rtt = 0, download_max_rtt = 0, download_avg_rtt = 0;
    double upload_min_rtt = 0, upload_max_rtt = 0, upload_avg_rtt = 0;
    double idle_rtt = 0;
    const char *logstash_ip = "127.0.0.1"; // Change to actual Logstash server IP
    int logstash_port = 20251;

    // Get my own IP (a simple way, can be improved)
    char hostname[256];
    char my_ip[INET_ADDRSTRLEN] = "0.0.0.0"; // Default if we can't determine
    struct hostent *h;

    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        h = gethostbyname(hostname);
        if (h && h->h_addr_list[0])
        {
            inet_ntop(AF_INET, h->h_addr_list[0], my_ip, sizeof(my_ip));
        }
    }

    // === Initial latency measurements (idle) ===
    printf("\n=== Measuring initial (idle) latency ===\n");
    int num_latency_measurements = RTT_TRIES;
    double *idle_rtts = calloc(num_latency_measurements, sizeof(double));

    int result = client_measure_latency(host, num_latency_measurements, 10, idle_rtts);
    if (result == LAT_OK)
    {
        // Calculate average of initial RTT measurements
        idle_rtt = 0;
        for (int i = 0; i < num_latency_measurements; i++)
        {
            idle_rtt += idle_rtts[i];
        }
        idle_rtt /= num_latency_measurements;
        printf("Idle RTT: %.3f ms\n", idle_rtt * 1000);
    }
    else
    {
        fprintf(stderr, "Error in initial latency measurements: %d\n", result);
        free(idle_rtts);
        return -1;
    }

    // === Download + Latency phase ===
    printf("\n=== Starting DOWNLOAD + latency test ===\n");

    pthread_t *download_tids = calloc(num_connections, sizeof(pthread_t));
    struct thr_arg *download_args = calloc(num_connections, sizeof(struct thr_arg));

    double *download_rtts = calloc(num_latency_measurements, sizeof(double));
    struct latency_arg download_lat_arg = {
        .host = host,
        .rtts = download_rtts,
        .num_measurements = num_latency_measurements,
        .completed = 0};
    pthread_t download_latency_tid;

    struct timespec t_start_download, t_end_download;
    clock_gettime(CLOCK_MONOTONIC, &t_start_download);

    if (pthread_create(&download_latency_tid, NULL, latency_thread, &download_lat_arg) != 0)
        perror("pthread_create for download latency");

    for (int i = 0; i < num_connections; ++i)
    {
        download_args[i].host = host;
        pthread_create(&download_tids[i], NULL, recv_thread, &download_args[i]);
    }

    for (int i = 0; i < num_connections; ++i)
    {
        pthread_join(download_tids[i], NULL);
        download_total_bytes += download_args[i].bytes;
    }

    pthread_join(download_latency_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end_download);

    download_elapsed = (t_end_download.tv_sec - t_start_download.tv_sec) +
                       (t_end_download.tv_nsec - t_start_download.tv_nsec) / 1e9;

    double download_throughput = (download_total_bytes * 8.0) / download_elapsed; // in bps

    printf("Download: received %llu bytes\n", download_total_bytes);
    printf("Download: elapsed time %.3f seconds\n", download_elapsed);
    printf("Download: throughput %.2f Mb/s\n", download_throughput / 1e6);

    if (download_lat_arg.completed)
    {
        download_min_rtt = download_rtts[0];
        download_max_rtt = download_rtts[0];
        download_avg_rtt = 0;

        for (int i = 0; i < num_latency_measurements; i++)
        {
            if (download_rtts[i] > 0)
            {
                if (download_rtts[i] < download_min_rtt)
                    download_min_rtt = download_rtts[i];
                if (download_rtts[i] > download_max_rtt)
                    download_max_rtt = download_rtts[i];
                download_avg_rtt += download_rtts[i];
            }
        }
        download_avg_rtt /= num_latency_measurements;

        printf("\nLatency measurements during download:\n");
        printf("Min RTT: %.3f ms\n", download_min_rtt * 1000);
        printf("Max RTT: %.3f ms\n", download_max_rtt * 1000);
        printf("Avg RTT: %.3f ms\n", download_avg_rtt * 1000);
    }

    // Free download resources
    free(download_tids);
    free(download_args);

    // Allow some time between tests
    sleep(2);

    // === Upload + Latency phase ===
    printf("\n=== Starting UPLOAD + latency test ===\n");

    struct thr_arg upload_arg = {.host = host, .bytes = 0};
    pthread_t upload_tid;

    double *upload_rtts = calloc(num_latency_measurements, sizeof(double));
    struct latency_arg upload_lat_arg = {
        .host = host,
        .rtts = upload_rtts,
        .num_measurements = num_latency_measurements,
        .completed = 0};
    pthread_t upload_latency_tid;

    struct timespec t_start_upload, t_end_upload;
    clock_gettime(CLOCK_MONOTONIC, &t_start_upload);

    // Create latency thread for upload
    if (pthread_create(&upload_latency_tid, NULL, latency_thread, &upload_lat_arg) != 0)
    {
        perror("pthread_create for upload latency");
        free(upload_rtts);
        return -1;
    }

    // Create upload thread with better error handling
    printf("Starting upload threads to server %s...\n", host);
    if (pthread_create(&upload_tid, NULL, upload_thread, &upload_arg) != 0)
    {
        perror("pthread_create for upload");
        pthread_cancel(upload_latency_tid);
        pthread_join(upload_latency_tid, NULL);
        free(upload_rtts);
        return -1;
    }

    // Wait for upload to complete with timeout protection
    printf("Waiting for upload threads to complete...\n");

    // Simpler approach: Use a timeout counter instead of pthread_tryjoin_np
    int upload_complete = 0;
    void *upload_result = NULL;

    // Start watchdog thread
    pthread_t watchdog_tid;
    struct watchdog_arg warg = {&upload_tid, 30, &upload_complete};
    if (pthread_create(&watchdog_tid, NULL, watchdog_thread, &warg) != 0)
    {
        perror("pthread_create for watchdog");
    }

    // Wait for upload thread to complete
    pthread_join(upload_tid, &upload_result);
    upload_complete = 1; // Mark upload as complete

    // Wait for watchdog thread to finish
    pthread_join(watchdog_tid, NULL);

    // Wait for latency thread
    pthread_join(upload_latency_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end_upload);
    double upload_elapsed = (t_end_upload.tv_sec - t_start_upload.tv_sec) +
                            (t_end_upload.tv_nsec - t_start_upload.tv_nsec) / 1e9;

    // Calculate upload throughput using bytes returned from upload_thread
    uint64_t upload_total_bytes = upload_arg.bytes;
    double upload_throughput = (upload_total_bytes * 8.0) / upload_elapsed; // in bps

    printf("Upload: sent %llu bytes\n", upload_total_bytes);
    printf("Upload: elapsed time %.3f seconds\n", upload_elapsed);
    printf("Upload: throughput %.2f Mb/s\n", upload_throughput / 1e6);

    if (upload_lat_arg.completed)
    {
        upload_min_rtt = upload_rtts[0];
        upload_max_rtt = upload_rtts[0];
        upload_avg_rtt = 0;

        for (int i = 0; i < num_latency_measurements; i++)
        {
            if (upload_rtts[i] > 0)
            {
                if (upload_rtts[i] < upload_min_rtt)
                    upload_min_rtt = upload_rtts[i];
                if (upload_rtts[i] > upload_max_rtt)
                    upload_max_rtt = upload_rtts[i];
                upload_avg_rtt += upload_rtts[i];
            }
        }
        upload_avg_rtt /= num_latency_measurements;

        printf("\nLatency measurements during upload:\n");
        printf("Min RTT: %.3f ms\n", upload_min_rtt * 1000);
        printf("Max RTT: %.3f ms\n", upload_max_rtt * 1000);
        printf("Avg RTT: %.3f ms\n", upload_avg_rtt * 1000);
    }

    // Export results in JSON format
    struct test_results results = {
        .src_ip = my_ip,
        .dst_ip = host,
        .download_bytes = download_total_bytes,
        .download_elapsed = download_elapsed,
        .avg_bw_download_bps = download_throughput,
        .upload_elapsed = upload_elapsed,
        .avg_bw_upload_bps = upload_throughput,
        .num_conns = num_connections,
        .rtt_idle = idle_rtt,
        .rtt_download = download_avg_rtt,
        .rtt_upload = upload_avg_rtt};

    // Export results to Logstash
    export_results_json(&results, logstash_ip, logstash_port);

    // Free resources
    free(idle_rtts);
    free(download_rtts);
    free(upload_rtts);

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

    printf("Starting throughput and latency test pipeline for host: %s with %d connection(s).\n", host, N_CONN);
    printf("The pipeline will perform both download and upload tests with latency measurements.\n");

    int result = run_pipeline(host, N_CONN);

    if (result == 0)
        printf("\nPipeline completed successfully - both download and upload tests finished.\n");
    else
        printf("\nPipeline encountered an error.\n");

    return result;
}
