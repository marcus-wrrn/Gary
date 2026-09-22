#include "fft.h"
#include <math.h>

#define FFT_PI  3.14159265358979f

void fft_forward(float* re, float* im, size_t n) {
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (i < j) {
            float tmp = re[i];
            re[i] = re[j];
            re[j] = tmp;
            tmp = im[i];
            im[i] = im[j];
            im[j] = tmp;
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * FFT_PI / (float)len;
        float step_r = cosf(angle);
        float step_i = sinf(angle);

        for (size_t start = 0; start < n; start += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;

            for (size_t k = 0; k < len / 2; k++) {
                size_t a = start + k;
                size_t b = a + len / 2;

                float t_r = re[b] * w_r - im[b] * w_i;
                float t_i = re[b] * w_i + im[b] * w_r;
                re[b] = re[a] - t_r;
                im[b] = im[a] - t_i;
                re[a] += t_r;
                im[a] += t_i;

                float next_r = w_r * step_r - w_i * step_i;
                w_i = w_r * step_i + w_i * step_r;
                w_r = next_r;
            }
        }
    }
}

void fft_magnitudes(float* re, float* im, float* mags, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float window = 0.5f * (1.0f - cosf(2.0f * FFT_PI * (float)i / (float)(n - 1)));
        re[i] *= window;
        im[i] = 0.0f;
    }

    fft_forward(re, im, n);
    
    for (size_t k = 0; k < n / 2; k++) {
        mags[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }
}
