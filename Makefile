# Makefile for TP_D_Figueiredo_Kaplan project

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -I. 
LDFLAGS = -pthread

# Source modules
COMMON_SRC = common.c
DOWNLOAD_SRC = download.c
LATENCY_SRC = latency.c

# Main targets
CLIENT_SRCS = client.c $(COMMON_SRC) $(DOWNLOAD_SRC) $(LATENCY_SRC)
SERVER_SRCS = server.c $(COMMON_SRC) $(DOWNLOAD_SRC) $(LATENCY_SRC)

TARGETS = client server

.PHONY: all clean
all: $(TARGETS)

# Build client executable
client: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRCS) $(LDFLAGS)

# Build server executable
server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRCS) $(LDFLAGS)

# Convenience: remove executables and object files
clean:
	rm -f $(TARGETS) *.o
