#ifndef WORKLOADS_H
#define WORKLOADS_H
#include <stdint.h>

uint32_t wl_array_fill_sum(uint8_t *buf, int n, int iters);
uint32_t wl_xor_sweep(uint8_t *buf, int n, int iters);
uint32_t wl_recursive_sum(uint32_t *scratch, int depth, int iters);
uint32_t wl_matrix_dot(int32_t *a, int32_t *b, int32_t *out, int n, int iters);

uint32_t wl_packet_io(const uint8_t *in, uint8_t *out, int n_packets, int pkt_size);
int32_t wl_fir_filter(const int16_t *input, int16_t *output,
                      const int16_t *coeffs, int n_samples, int n_taps);
uint32_t wl_stream_xor(const uint8_t *in, uint8_t *out, const uint8_t *key,
                       int n, int key_size, int iters);

#endif
