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
#include "handle_result.h" // For struct BW_result and packResultPayload

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

int export_results_json(const struct test_results *results, const char *result_ip, int result_port)
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
    struct sockaddr_in result_addr;
    int sock = udp_socket_init(result_ip, result_port, &result_addr, 0);
    if (sock < 0)
    {
        fprintf(stderr, "Error creating socket for JSON export\n");
        return -1;
    }

    // Enviar JSON
    if (sendto(sock, json_buffer, strlen(json_buffer), 0,
               (struct sockaddr *)&result_addr, sizeof(result_addr)) < 0)
    {
        perror("sendto JSON");
        close(sock);
        return -1;
    }

    printf("Results exported in JSON format to %s:%d\n", result_ip, result_port);
    printf("JSON payload: %s\n", json_buffer);

    close(sock);
    return 0;
}

int run_pipeline(const char *host, int num_connections, const char *result_ip, int result_port)
{
    // Variables for storing results
    uint64_t download_total_bytes = 0;
    double download_elapsed = 0;
    double download_min_rtt = 0, download_max_rtt = 0, download_avg_rtt = 0;
    double upload_min_rtt = 0, upload_max_rtt = 0, upload_avg_rtt = 0;
    double idle_rtt = 0;

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

    struct BW_result upload_result;
    client_upload(host, N_CONN, &upload_result);
    // Allocate memory for RTT measurements during upload
    double *upload_rtts = calloc(num_latency_measurements, sizeof(double));
    struct latency_arg upload_lat_arg = {
        .host = host,
        .rtts = upload_rtts,
        .num_measurements = num_latency_measurements,
        .completed = 0};
    pthread_t upload_latency_tid;

    struct timespec t_start_upload, t_end_upload;
    clock_gettime(CLOCK_MONOTONIC, &t_start_upload);

    // Start latency measurement thread
    if (pthread_create(&upload_latency_tid, NULL, latency_thread, &upload_lat_arg) != 0)
        perror("pthread_create for upload latency");
    // Upload is already called above
    // Calculate total bytes and find maximum duration
    uint64_t upload_total_bytes = 0;
    double upload_elapsed = 0.0;

    for (int i = 0; i < N_CONN; i++)
    {
        upload_total_bytes += upload_result.conn_bytes[i];
        if (upload_result.conn_duration[i] > upload_elapsed)
        {
            upload_elapsed = upload_result.conn_duration[i];
        }
    }

    double upload_throughput = (upload_total_bytes * 8.0) / upload_elapsed; // in bps

    pthread_join(upload_latency_tid, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end_upload);

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

    export_results_json(&results, result_ip, result_port);

    // Free resources
    free(idle_rtts);
    free(download_rtts);
    free(upload_rtts);

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s host result_ip result_port\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    const char *result_ip = argv[2];
    int result_port = atoi(argv[3]);

    printf("Starting throughput and latency test pipeline for host: %s with %d connection(s).\n", host, N_CONN);
    printf("The pipeline will perform both download and upload tests with latency measurements.\n");

    int result = run_pipeline(host, N_CONN, result_ip, result_port);

    if (result == 0)
        printf("\nPipeline completed successfully - both download and upload tests finished.\n");
    else
        printf("\nPipeline encountered an error.\n");

    return result;
}
