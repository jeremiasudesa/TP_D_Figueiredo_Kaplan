// client.cpp
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include "include/common.h"

// Structure to hold connection data for download test
struct download_conn_data {
    char *server_ip;
    uint64_t bytes_received;
    double duration;
    int conn_id;
    int success;
};

// Structure to hold connection data for upload test
struct upload_conn_data {
    char *server_ip;
    uint32_t test_id;
    uint16_t conn_id;
    uint64_t bytes_sent;
    double duration;
    int success;
};

// Structure to hold latency measurement data
struct latency_data {
    double min_latency;
    double max_latency;
    double avg_latency;
    int packet_count;
    int successful_packets;
};

// Generate random 4-byte test ID (first byte cannot be 0xFF)
uint32_t generate_test_id() {
    uint32_t test_id;
    do {
        test_id = (uint32_t)random();
    } while ((test_id & 0xFF000000) == 0xFF000000); // Ensure first byte is not 0xFF
    return test_id;
}

// Measure latency using UDP echo
struct latency_data measure_latency(const char *server_ip, int packet_count) {
    struct latency_data result = {9999.0, 0.0, 0.0, packet_count, 0};
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[64];
    struct timeval start, end, timeout;
    double total_latency = 0.0;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return result;
    
    // Set socket timeout
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    for (int i = 0; i < packet_count; i++) {
        snprintf(buffer, sizeof(buffer), "ping_%d_%ld", i, time(NULL));
        
        gettimeofday(&start, NULL);
        if (sendto(sockfd, buffer, strlen(buffer), 0, 
                   (struct sockaddr*)&server_addr, sizeof(server_addr)) > 0) {
            
            char recv_buffer[64];
            if (recvfrom(sockfd, recv_buffer, sizeof(recv_buffer), 0, NULL, NULL) > 0) {
                gettimeofday(&end, NULL);
                
                double latency = (end.tv_sec - start.tv_sec) * 1000.0 + 
                               (end.tv_usec - start.tv_usec) / 1000.0;
                
                total_latency += latency;
                result.successful_packets++;
                
                if (latency < result.min_latency) result.min_latency = latency;
                if (latency > result.max_latency) result.max_latency = latency;
            }
        }
        usleep(10000); // 10ms between packets
    }
    
    if (result.successful_packets > 0) {
        result.avg_latency = total_latency / result.successful_packets;
    }
    
    close(sockfd);
    return result;
}

// Thread function for download test
void* download_thread(void* arg) {
    struct download_conn_data *data = (struct download_conn_data*)arg;
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[64*1024];
    struct timeval start, end;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        data->success = 0;
        return NULL;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT_DOWNLOAD);
    inet_pton(AF_INET, data->server_ip, &server_addr.sin_addr);
    
    gettimeofday(&start, NULL);
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        data->success = 0;
        return NULL;
    }
    
    data->bytes_received = 0;
    time_t test_start = time(NULL);
    
    while (difftime(time(NULL), test_start) < TEST_DURATION_SEC) {
        ssize_t bytes = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;
        data->bytes_received += bytes;
    }
    
    gettimeofday(&end, NULL);
    data->duration = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    data->success = 1;
    
    close(sockfd);
    return NULL;
}

// Thread function for upload test
void* upload_thread(void* arg) {
    struct upload_conn_data *data = (struct upload_conn_data*)arg;
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[64*1024];
    struct timeval start, end;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        data->success = 0;
        return NULL;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT_UPLOAD);
    inet_pton(AF_INET, data->server_ip, &server_addr.sin_addr);
    
    gettimeofday(&start, NULL);
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        data->success = 0;
        return NULL;
    }
    
    // Send 4-byte test ID + 2-byte connection ID
    uint32_t net_test_id = htonl(data->test_id);
    uint16_t net_conn_id = htons(data->conn_id);
    
    if (send(sockfd, &net_test_id, sizeof(net_test_id), 0) != sizeof(net_test_id) ||
        send(sockfd, &net_conn_id, sizeof(net_conn_id), 0) != sizeof(net_conn_id)) {
        close(sockfd);
        data->success = 0;
        return NULL;
    }
    
    // Fill buffer with random data
    for (int i = 0; i < sizeof(buffer); i++) {
        buffer[i] = (char)(random() % 256);
    }
    
    data->bytes_sent = 0;
    time_t test_start = time(NULL);
    
    while (difftime(time(NULL), test_start) < TEST_DURATION_SEC) {
        ssize_t bytes = send(sockfd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;
        data->bytes_sent += bytes;
    }
    
    gettimeofday(&end, NULL);
    data->duration = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    data->success = 1;
    
    close(sockfd);
    return NULL;
}

// Send JSON results to Logstash
void send_json_results(const char *server_ip, uint32_t test_id, 
                      struct download_conn_data *download_data,
                      struct upload_conn_data *upload_data,
                      struct latency_data *latency_pre,
                      struct latency_data *latency_download,
                      struct latency_data *latency_upload) {
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return;
    
    struct sockaddr_in logstash_addr;
    memset(&logstash_addr, 0, sizeof(logstash_addr));
    logstash_addr.sin_family = AF_INET;
    logstash_addr.sin_port = htons(LOGSTASH_PORT);
    inet_pton(AF_INET, LOGSTASH_HOST, &logstash_addr.sin_addr);
    
    char json_buffer[4096];
    time_t now = time(NULL);
    
    // Calculate total throughput
    uint64_t total_download_bytes = 0, total_upload_bytes = 0;
    double total_download_time = 0, total_upload_time = 0;
    
    for (int i = 0; i < NUM_CONN; i++) {
        if (download_data[i].success) {
            total_download_bytes += download_data[i].bytes_received;
            if (download_data[i].duration > total_download_time) {
                total_download_time = download_data[i].duration;
            }
        }
        if (upload_data[i].success) {
            total_upload_bytes += upload_data[i].bytes_sent;
            if (upload_data[i].duration > total_upload_time) {
                total_upload_time = upload_data[i].duration;
            }
        }
    }
    
    double download_throughput = total_download_time > 0 ? 
        (total_download_bytes * 8.0) / (total_download_time * 1000000.0) : 0.0; // Mbps
    double upload_throughput = total_upload_time > 0 ? 
        (total_upload_bytes * 8.0) / (total_upload_time * 1000000.0) : 0.0; // Mbps
    
    snprintf(json_buffer, sizeof(json_buffer),
        "{"
        "\"timestamp\":\"%ld\","
        "\"test_id\":\"0x%08x\","
        "\"server_ip\":\"%s\","
        "\"test_duration_sec\":%d,"
        "\"num_connections\":%d,"
        "\"download_throughput_mbps\":%.2f,"
        "\"upload_throughput_mbps\":%.2f,"
        "\"total_download_bytes\":%lu,"
        "\"total_upload_bytes\":%lu,"
        "\"latency_pre_test\":{"
            "\"min_ms\":%.2f,"
            "\"max_ms\":%.2f,"
            "\"avg_ms\":%.2f,"
            "\"success_rate\":%.2f"
        "},"
        "\"latency_during_download\":{"
            "\"min_ms\":%.2f,"
            "\"max_ms\":%.2f,"
            "\"avg_ms\":%.2f,"
            "\"success_rate\":%.2f"
        "},"
        "\"latency_during_upload\":{"
            "\"min_ms\":%.2f,"
            "\"max_ms\":%.2f,"
            "\"avg_ms\":%.2f,"
            "\"success_rate\":%.2f"
        "}"
        "}",
        now, test_id, server_ip, TEST_DURATION_SEC, NUM_CONN,
        download_throughput, upload_throughput,
        total_download_bytes, total_upload_bytes,
        latency_pre->min_latency, latency_pre->max_latency, latency_pre->avg_latency,
        (double)latency_pre->successful_packets / latency_pre->packet_count * 100.0,
        latency_download->min_latency, latency_download->max_latency, latency_download->avg_latency,
        (double)latency_download->successful_packets / latency_download->packet_count * 100.0,
        latency_upload->min_latency, latency_upload->max_latency, latency_upload->avg_latency,
        (double)latency_upload->successful_packets / latency_upload->packet_count * 100.0
    );
    
    sendto(sockfd, json_buffer, strlen(json_buffer), 0,
           (struct sockaddr*)&logstash_addr, sizeof(logstash_addr));
    
    close(sockfd);
    
    printf("Results sent to Logstash:\n%s\n", json_buffer);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    
    char *server_ip = argv[1];
    srandom(time(NULL));
    
    printf("Starting network performance test against %s\n", server_ip);
    printf("Test duration: %d seconds\n", TEST_DURATION_SEC);
    printf("Number of connections: %d\n\n", NUM_CONN);
    
    // Generate test ID
    uint32_t test_id = generate_test_id();
    printf("Test ID: 0x%08x\n\n", test_id);
    
    // Phase 1: Pre-test latency measurement
    printf("Phase 1: Pre-test latency measurement...\n");
    struct latency_data latency_pre = measure_latency(server_ip, 10);
    printf("Pre-test latency - Min: %.2f ms, Max: %.2f ms, Avg: %.2f ms, Success: %d/%d\n\n",
           latency_pre.min_latency, latency_pre.max_latency, latency_pre.avg_latency,
           latency_pre.successful_packets, latency_pre.packet_count);
    
    // Phase 2: Download test
    printf("Phase 2: Download throughput test...\n");
    pthread_t download_threads[NUM_CONN];
    struct download_conn_data download_data[NUM_CONN];
    
    for (int i = 0; i < NUM_CONN; i++) {
        download_data[i].server_ip = server_ip;
        download_data[i].conn_id = i;
        download_data[i].bytes_received = 0;
        download_data[i].success = 0;
        pthread_create(&download_threads[i], NULL, download_thread, &download_data[i]);
    }
    
    // Measure latency during download
    struct latency_data latency_download = measure_latency(server_ip, 5);
    
    // Wait for download threads to complete
    for (int i = 0; i < NUM_CONN; i++) {
        pthread_join(download_threads[i], NULL);
    }
    
    // Calculate download results
    uint64_t total_download_bytes = 0;
    int successful_downloads = 0;
    for (int i = 0; i < NUM_CONN; i++) {
        if (download_data[i].success) {
            total_download_bytes += download_data[i].bytes_received;
            successful_downloads++;
        }
    }
    
    printf("Download completed - %d/%d connections successful\n", successful_downloads, NUM_CONN);
    printf("Total bytes received: %lu\n", total_download_bytes);
    printf("Download latency - Min: %.2f ms, Max: %.2f ms, Avg: %.2f ms\n\n",
           latency_download.min_latency, latency_download.max_latency, latency_download.avg_latency);
    
    // Phase 3: Upload test
    printf("Phase 3: Upload throughput test...\n");
    pthread_t upload_threads[NUM_CONN];
    struct upload_conn_data upload_data[NUM_CONN];
    
    for (int i = 0; i < NUM_CONN; i++) {
        upload_data[i].server_ip = server_ip;
        upload_data[i].test_id = test_id;
        upload_data[i].conn_id = i;
        upload_data[i].bytes_sent = 0;
        upload_data[i].success = 0;
        pthread_create(&upload_threads[i], NULL, upload_thread, &upload_data[i]);
    }
    
    // Measure latency during upload
    struct latency_data latency_upload = measure_latency(server_ip, 5);
    
    // Wait for upload threads to complete
    for (int i = 0; i < NUM_CONN; i++) {
        pthread_join(upload_threads[i], NULL);
    }
    
    // Calculate upload results
    uint64_t total_upload_bytes = 0;
    int successful_uploads = 0;
    for (int i = 0; i < NUM_CONN; i++) {
        if (upload_data[i].success) {
            total_upload_bytes += upload_data[i].bytes_sent;
            successful_uploads++;
        }
    }
    
    printf("Upload completed - %d/%d connections successful\n", successful_uploads, NUM_CONN);
    printf("Total bytes sent: %lu\n", total_upload_bytes);
    printf("Upload latency - Min: %.2f ms, Max: %.2f ms, Avg: %.2f ms\n\n",
           latency_upload.min_latency, latency_upload.max_latency, latency_upload.avg_latency);
    
    // Send results to Logstash
    printf("Sending results to Logstash...\n");
    send_json_results(server_ip, test_id, download_data, upload_data, 
                     &latency_pre, &latency_download, &latency_upload);
    
    printf("Test completed successfully!\n");
    return 0;
}