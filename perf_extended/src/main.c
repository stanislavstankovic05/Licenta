#include "workloads.h"
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/timing/timing.h>

void perf_setup(void);
const char *perf_label(void);

#define BUF_SIZE 512
#define ARRAY_ITERS 1000
#define XOR_ITERS 2000
#define RECURSIVE_DEPTH 48
#define RECURSIVE_ITERS 500
#define MATRIX_N 8
#define MATRIX_ITERS 200

#define PKT_COUNT 128
#define PKT_SIZE 32
#define FIR_SAMPLES 256
#define FIR_TAPS 16
#define FIR_ITERS 50
#define STREAM_LEN 256
#define STREAM_KEY_LEN 16
#define STREAM_ITERS 100

static void print_row(const char *name, uint64_t cycles)
{
    uint64_t ns = timing_cycles_to_ns(cycles);
    printk("[PERF] %-22s  cycles=%9llu   time=%7llu us\n",
           name,
           (unsigned long long)cycles,
           (unsigned long long)(ns / 1000U));
}

int main(void)
{
    perf_setup();

    printk("\n==============================================\n");
    printk("  %s\n", perf_label());
    printk("  Board: " CONFIG_BOARD "\n");
    printk("==============================================\n\n");

    uint8_t *buf = malloc(BUF_SIZE * 2);
    uint32_t *scratch = malloc((RECURSIVE_DEPTH + 1) * sizeof(uint32_t));
    int32_t *mat_a = malloc(MATRIX_N * MATRIX_N * sizeof(int32_t));
    int32_t *mat_b = malloc(MATRIX_N * sizeof(int32_t));
    int32_t *mat_out = malloc(MATRIX_N * sizeof(int32_t));

    uint8_t *pkt_in = malloc(PKT_COUNT * PKT_SIZE);
    uint8_t *pkt_out = malloc(PKT_COUNT * PKT_SIZE);
    int16_t *fir_in = malloc(FIR_SAMPLES * sizeof(int16_t));
    int16_t *fir_out = malloc(FIR_SAMPLES * sizeof(int16_t));
    int16_t *fir_coef = malloc(FIR_TAPS * sizeof(int16_t));
    uint8_t *xs_in = malloc(STREAM_LEN);
    uint8_t *xs_out = malloc(STREAM_LEN);
    uint8_t *xs_key = malloc(STREAM_KEY_LEN);

    if(!buf || !scratch || !mat_a || !mat_b || !mat_out ||
       !pkt_in || !pkt_out || !fir_in || !fir_out || !fir_coef ||
       !xs_in || !xs_out || !xs_key)
    {
        printk("[perf] allocation failed\n");
        return -1;
    }

    for(int i = 0; i < MATRIX_N * MATRIX_N; i++)
    {
        mat_a[i] = (int32_t)(i + 1);
    }
    for(int i = 0; i < MATRIX_N; i++)
    {
        mat_b[i] = (int32_t)(i + 1);
    }

    for(int i = 0; i < PKT_COUNT * PKT_SIZE; i++)
    {
        pkt_in[i] = (uint8_t)(i * 7);
    }
    for(int i = 0; i < FIR_SAMPLES; i++)
    {
        fir_in[i] = (int16_t)((i * 13) & 0x7FFF);
    }
    for(int i = 0; i < FIR_TAPS; i++)
    {
        fir_coef[i] = (int16_t)(i + 1);
    }
    for(int i = 0; i < STREAM_LEN; i++)
    {
        xs_in[i] = (uint8_t)(i * 11);
    }
    for(int i = 0; i < STREAM_KEY_LEN; i++)
    {
        xs_key[i] = (uint8_t)(i * 23);
    }

    timing_init();
    timing_start();

    timing_t t1, t2;
    uint64_t c_array, c_xor, c_rec, c_mat;
    uint64_t c_pkt, c_fir, c_xs;
    uint32_t d[4];
    uint32_t e[3];

    t1 = timing_counter_get();
    d[0] = wl_array_fill_sum(buf, BUF_SIZE, ARRAY_ITERS);
    t2 = timing_counter_get();
    c_array = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    d[1] = wl_xor_sweep(buf + BUF_SIZE, BUF_SIZE, XOR_ITERS);
    t2 = timing_counter_get();
    c_xor = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    d[2] = wl_recursive_sum(scratch, RECURSIVE_DEPTH, RECURSIVE_ITERS);
    t2 = timing_counter_get();
    c_rec = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    d[3] = wl_matrix_dot(mat_a, mat_b, mat_out, MATRIX_N, MATRIX_ITERS);
    t2 = timing_counter_get();
    c_mat = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    e[0] = wl_packet_io(pkt_in, pkt_out, PKT_COUNT, PKT_SIZE);
    t2 = timing_counter_get();
    c_pkt = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    for(int it = 0; it < FIR_ITERS; it++)
    {
        e[1] = (uint32_t)wl_fir_filter(fir_in, fir_out, fir_coef,
                                       FIR_SAMPLES, FIR_TAPS);
    }
    t2 = timing_counter_get();
    c_fir = timing_cycles_get(&t1, &t2);

    t1 = timing_counter_get();
    e[2] = wl_stream_xor(xs_in, xs_out, xs_key,
                         STREAM_LEN, STREAM_KEY_LEN, STREAM_ITERS);
    t2 = timing_counter_get();
    c_xs = timing_cycles_get(&t1, &t2);

    timing_stop();

    free(buf);
    free(scratch);
    free(mat_a);
    free(mat_b);
    free(mat_out);
    free(pkt_in);
    free(pkt_out);
    free(fir_in);
    free(fir_out);
    free(fir_coef);
    free(xs_in);
    free(xs_out);
    free(xs_key);

    printk("\n");
    print_row("array_fill_sum", c_array);
    print_row("xor_sweep", c_xor);
    print_row("recursive_sum", c_rec);
    print_row("matrix_dot", c_mat);
    printk("\n");
    print_row("packet_io", c_pkt);
    print_row("fir_filter", c_fir);
    print_row("stream_xor", c_xs);
    printk("\n");
    print_row("TOTAL", c_array + c_xor + c_rec + c_mat +
                           c_pkt + c_fir + c_xs);
    printk("\n[PERF] checksum: %u %u %u %u | %u %u %u\n",
           d[0], d[1], d[2], d[3], e[0], e[1], e[2]);
#ifdef PERF_VMMU_BUILD
    extern uint32_t tlb_hits;
    printk("[PERF] tlb_hits = %u\n", (unsigned)tlb_hits);
#endif
    printk("==============================================\n\n");

    return 0;
}
