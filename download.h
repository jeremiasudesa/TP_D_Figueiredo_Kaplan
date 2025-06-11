#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdint.h> // For uint64_t

// Error codes
#define DOWNLOAD_OK 0
#define DOWNLOAD_ERROR_BASE -1000
#define DOWNLOAD_SEND_ERR (DOWNLOAD_ERROR_BASE - 1)
#define DOWNLOAD_RECV_ERR (DOWNLOAD_ERROR_BASE - 2)
#define DOWNLOAD_SOCK_ERR (DOWNLOAD_ERROR_BASE - 3)
#define DOWNLOAD_CONNECT_ERR (DOWNLOAD_ERROR_BASE - 4)
#define DOWNLOAD_GETADDRINFO_ERR (DOWNLOAD_ERROR_BASE - 5)
#define DOWNLOAD_PARAM_ERR (DOWNLOAD_ERROR_BASE - 6)

/**
 * @brief Handles a single client connection on the server side for download.
 *
 * Sends a continuous stream of data to the client for a predefined duration (T_SECONDS).
 *
 * @param client_socket_fd The file descriptor of the connected client socket.
 * @return int DOWNLOAD_OK on success, or an error code on failure.
 */
int server_handle_download_client(int client_socket_fd);

/**
 * @brief Performs a download operation from the client side.
 *
 * Connects to the specified server, receives data for a predefined duration,
 * and updates the total bytes transferred.
 *
 * @param host The hostname or IP address of the server.
 * @param port The port number of the server.
 * @param duration_seconds The duration for which to download data.
 * @param bytes_transferred Pointer to a uint64_t to store the total bytes received.
 * @return int DOWNLOAD_OK on success, or an error code on failure.
 */
int client_perform_download(const char *host, const char *port, int duration_seconds, uint64_t *bytes_transferred);

#endif // DOWNLOAD_H
