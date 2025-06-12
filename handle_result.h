#ifndef HANDLE_RESULT_H
#define HANDLE_RESULT_H

#include <stdint.h>

#define NUM_CONN 10

#define E_MINIMUM_DATA    -1
#define E_NOT_ENOUGH_DATA -2
#define E_NO_NEW_LINE     -3
#define E_LINE_TOO_LONG   -4
#define E_INV_LINE_FORMAT -5
#define E_NUMBER_PARSE    -6

struct BW_result {
  uint32_t id_measurement;
  uint64_t conn_bytes[NUM_CONN];
  double conn_duration[NUM_CONN];
};

void printBwResult(struct BW_result bw_result);
int packResultPayload(struct BW_result bw_result, void *buffer, int buffer_size);
int unpackResultPayload(struct BW_result *bw_result, void *buffer, int buffer_size);

#endif /* HANDLE_RESULT_H */
