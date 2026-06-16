
#include <stdint.h>

uint32_t wl_array_fill_sum(uint8_t *buf, int n, int iters)
{
    uint32_t sum = 0;
    for(int i = 0; i < iters; i++)
    {
        for(int j = 0; j < n; j++)
        {
            buf[j] = (uint8_t)(i ^ j);
        }
        for(int j = 0; j < n; j++)
        {
            sum += buf[j];
        }
    }
    return sum;
}

uint32_t wl_xor_sweep(uint8_t *buf, int n, int iters)
{
    uint32_t acc = 0;
    for(int i = 0; i < iters; i++)
    {
        for(int j = 0; j < n; j++)
        {
            acc ^= buf[j];
        }
    }
    return acc;
}

static uint32_t rec_helper(uint32_t *scratch, int n)
{
    if(n <= 0)
    {
        return 0;
    }
    scratch[0] = (uint32_t)n;
    uint32_t child = rec_helper(scratch + 1, n - 1);
    return scratch[0] + child;
}

uint32_t wl_recursive_sum(uint32_t *scratch, int depth, int iters)
{
    uint32_t acc = 0;
    for(int i = 0; i < iters; i++)
    {
        acc += rec_helper(scratch, depth);
    }
    return acc;
}

uint32_t wl_matrix_dot(int32_t *a, int32_t *b, int32_t *out, int n, int iters)
{
    for(int k = 0; k < iters; k++)
    {
        for(int i = 0; i < n; i++)
        {
            int32_t sum = 0;
            for(int j = 0; j < n; j++)
            {
                sum += a[i * n + j] * b[j];
            }
            out[i] = sum;
        }
    }
    return (uint32_t)out[0];
}

uint32_t wl_packet_io(const uint8_t *in, uint8_t *out,
                      int n_packets, int pkt_size)
{
    uint32_t total = 0;
    for(int p = 0; p < n_packets; p++)
    {
        const uint8_t *src = in + p * pkt_size;
        uint8_t *dst = out + p * pkt_size;

        dst[0] = src[0];
        dst[1] = (uint8_t)(src[1] + 1);
        dst[2] = src[2];

        uint8_t cksum = 0;
        for(int i = 4; i < pkt_size; i++)
        {
            uint8_t v = (uint8_t)(src[i] ^ 0xA5);
            dst[i] = v;
            cksum ^= v;
        }
        dst[3] = cksum;
        total += cksum;
    }
    return total;
}

int32_t wl_fir_filter(const int16_t *input, int16_t *output,
                      const int16_t *coeffs, int n_samples, int n_taps)
{
    int32_t total = 0;
    for(int n = n_taps - 1; n < n_samples; n++)
    {
        int32_t acc = 0;
        for(int k = 0; k < n_taps; k++)
        {
            acc += (int32_t)input[n - k] * (int32_t)coeffs[k];
        }
        int16_t y = (int16_t)(acc >> 8);
        output[n] = y;
        total += y;
    }
    return total;
}

uint32_t wl_stream_xor(const uint8_t *in, uint8_t *out,
                       const uint8_t *key, int n, int key_size, int iters)
{
    uint32_t sum = 0;
    for(int it = 0; it < iters; it++)
    {
        for(int i = 0; i < n; i++)
        {
            uint8_t v = (uint8_t)(in[i] ^ key[i % key_size]);
            out[i] = v;
            sum += v;
        }
    }
    return sum;
}
