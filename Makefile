CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c99 -pthread -I.
CXXFLAGS = -Wall -Wextra -std=c++11 -pthread -I.

# Default target
all: server client

# Server target
server: server.cpp include/common.h
	$(CXX) $(CXXFLAGS) -o server server.cpp

# Client target  
client: client.cpp include/common.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

# Clean target
clean:
	rm -f server client

# Test target (runs server in background and client)
test: server client
	@echo "Starting server in background..."
	./server &
	@echo "Waiting 2 seconds for server to start..."
	sleep 2
	@echo "Running client test against localhost..."
	./client 127.0.0.1
	@echo "Stopping server..."
	pkill -f ./server || true

.PHONY: all clean test