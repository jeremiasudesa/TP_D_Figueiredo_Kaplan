#ifndef COMMON_H
#define COMMON_H

// Test duration in seconds (can be modified here)
#define TEST_DURATION_SEC 20

// Number of concurrent TCP connections for throughput tests
#define NUM_CONN 10

// TCP ports
#define TCP_PORT_DOWNLOAD 20251
#define TCP_PORT_UPLOAD   20252

// UDP port (used for latency measurement and JSON reporting)
#define UDP_PORT 20251

// Logstash endpoint for JSON result reporting
#define LOGSTASH_HOST "127.0.0.1"
#define LOGSTASH_PORT 5044

#endif // COMMON_H
