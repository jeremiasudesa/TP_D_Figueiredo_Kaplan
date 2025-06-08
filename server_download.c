// server.c
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
#include <sys/time.h> // Added for struct timeval
#include "include/common.h"

static void* tcp_download_listener(void *arg) {
    (void)arg; // Suppress unused parameter warning
    int listen_fd, client_fd;
    struct sockaddr_in addr;
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TCP_PORT_DOWNLOAD);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, NUM_CONN);
    while (1) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) continue;
        pthread_t th;
        int *pfd = (int*)malloc(sizeof(int)); // Add explicit cast
        *pfd = client_fd;
        pthread_create(&th, NULL, 
            [](void *arg)->void* {
                int fd = *(int*)arg; free(arg);

#ifdef __APPLE__
                // Set SO_NOSIGPIPE on macOS to prevent SIGPIPE on send if client disconnects
                int set = 1;
                if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int)) < 0) {
                    // perror("setsockopt SO_NOSIGPIPE failed"); // Optional: log error
                    close(fd);
                    return NULL;
                }
#endif

                char buf[64*1024] = {0}; // Server sends zeros, content doesn't matter
                time_t test_start_time = time(NULL);
                
                // Send data for TEST_DURATION_SEC
                while (difftime(time(NULL), test_start_time) < TEST_DURATION_SEC) {
#ifndef __APPLE__
                    // Use MSG_NOSIGNAL on Linux, if SO_NOSIGPIPE is not used/available
                    ssize_t n = send(fd, buf, sizeof(buf), MSG_NOSIGNAL);
#else
                    ssize_t n = send(fd, buf, sizeof(buf), 0); // No flag needed if SO_NOSIGPIPE is set
#endif
                    if (n <= 0) { // Error or client closed connection
                        close(fd);
                        return NULL; // Exit thread
                    }
                }

                // After TEST_DURATION_SEC of sending, wait for client to close or for T+3 total timeout
                struct timeval tv;
                tv.tv_sec = 0; 
                tv.tv_usec = 100000; // 100ms timeout for each recv attempt
                if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
                    // perror("setsockopt SO_RCVTIMEO failed"); // Optional: log error
                    close(fd);
                    return NULL;
                }

                while (difftime(time(NULL), test_start_time) < (TEST_DURATION_SEC + 3)) {
                    char dummy_buf[1];
                    ssize_t n_recv = recv(fd, dummy_buf, sizeof(dummy_buf), 0);
                    if (n_recv == 0) { // Client closed connection
                        break; 
                    }
                    if (n_recv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Timeout on recv, continue checking overall time
                        usleep(10000); // Sleep briefly (10ms) to prevent busy-wait
                        continue;
                    }
                    if (n_recv < 0) { // Other recv error
                        break;
                    }
                    // If n_recv > 0, client sent something unexpected. Continue waiting.
                }
                close(fd); 
                return NULL;
            }, pfd);
        pthread_detach(th);
    }
    return NULL;
}

static void* tcp_upload_listener(void *arg) {
    (void)arg; // Suppress unused parameter warning
    int listen_fd, client_fd;
    struct sockaddr_in addr;
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TCP_PORT_UPLOAD);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, NUM_CONN);
    while (1) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) continue;
        pthread_t th;
        int *pfd = (int*)malloc(sizeof(int)); // Add explicit cast
        *pfd = client_fd;
        pthread_create(&th, NULL,
            [](void *arg)->void* {
                int fd = *(int*)arg; free(arg);
                
                // Read 4-byte test ID + 2-byte connection ID from client
                char id_buf[6]; 
                ssize_t bytes_read_id = 0;
                while (bytes_read_id < (ssize_t)sizeof(id_buf)) { // Fix signed comparison
                    ssize_t n_id = recv(fd, id_buf + bytes_read_id, sizeof(id_buf) - bytes_read_id, 0);
                    if (n_id <= 0) { // Error or client closed connection prematurely
                        close(fd);
                        return NULL; // Exit thread
                    }
                    bytes_read_id += n_id;
                }
                // Identifiers have been received (stored in id_buf). Server consumes them.

                char data_buf[64*1024];
                time_t test_start_time = time(NULL);
                
                // Receive data for TEST_DURATION_SEC
                while (difftime(time(NULL), test_start_time) < TEST_DURATION_SEC) {
                    ssize_t n_recv_data = recv(fd, data_buf, sizeof(data_buf), 0);
                    if (n_recv_data <= 0) { // Error or client closed connection
                        close(fd);
                        return NULL; // Exit thread
                    }
                }

                // After TEST_DURATION_SEC of receiving, wait for client to close or for T+3 total timeout
                struct timeval tv;
                tv.tv_sec = 0; 
                tv.tv_usec = 100000; // 100ms timeout for each recv attempt
                if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
                    // perror("setsockopt SO_RCVTIMEO failed"); // Optional: log error
                    close(fd);
                    return NULL;
                }
                
                while (difftime(time(NULL), test_start_time) < (TEST_DURATION_SEC + 3)) {
                    char dummy_buf[1]; 
                    ssize_t n_recv_dummy = recv(fd, dummy_buf, sizeof(dummy_buf), 0);
                    if (n_recv_dummy == 0) { // Client closed
                        break;
                    }
                    if (n_recv_dummy < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Timeout on recv, continue checking overall time
                        usleep(10000); // Sleep briefly (10ms) to prevent busy-wait
                        continue;
                    }
                    if (n_recv_dummy < 0) { // Other error
                        break;
                    }
                    // If n_recv_dummy > 0, client sent more data. Consume and continue waiting.
                }
                close(fd);
                return NULL;
            }, pfd);
        pthread_detach(th);
    }
    return NULL;
}

static void* udp_echo_listener(void *arg) {
    (void)arg; // Suppress unused parameter warning
    int sockfd;
    struct sockaddr_in addr, peer;
    socklen_t peerlen = sizeof(peer);
    char buf[1500];
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(UDP_PORT);
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    while (1) {
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&peer, &peerlen);
        if (n>0) {
            sendto(sockfd, buf, n, 0,
                   (struct sockaddr*)&peer, peerlen);
        }
    }
    return NULL;
}

int main() {
    pthread_t th_dl, th_ul, th_udp;
    pthread_create(&th_dl,  NULL, tcp_download_listener, NULL);
    pthread_create(&th_ul,  NULL, tcp_upload_listener,   NULL);
    pthread_create(&th_udp, NULL, udp_echo_listener,     NULL);
    pthread_join(th_dl,  NULL);
    pthread_join(th_ul,  NULL);
    pthread_join(th_udp, NULL);
    return 0;
}