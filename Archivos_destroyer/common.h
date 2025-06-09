// common.h

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

// Inicializa un socket UDP y lo enlaza a `port` si `bind_it` es true.
// Devuelve el descriptor o -1 en error.
int init_udp_socket(int port, int bind_it);

// Toma un timestamp de alta resolución (segundos + nanosegundos).
struct timespec now_ts();

// Calcula la diferencia en segundos (float) entre dos timestamps.
double diff_ts(const struct timespec *start, const struct timespec *end);

// Función de manejo de errores (message + exit).
void die(const char *msg);

#endif // COMMON_H
