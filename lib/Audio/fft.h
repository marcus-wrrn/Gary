#pragma once
#include <stddef.h>

// Radix-2 FFT, written out here rather than pulled in from esp-dsp so the Audio
// library stays clear of the IDF component manager. Sized for visualisation
// work: single precision, no SIMD, and accurate enough that a few tenths of a
// dB of twiddle drift does not matter.

// In-place complex forward transform. n must be a power of two, and re and im
// must each hold n floats.
void fft_forward(float* re, float* im, size_t n);

// Hann-windows the real signal already sitting in re, transforms it, and writes
// the first n / 2 magnitudes into mags. im is scratch and is zeroed on entry;
// re is destroyed. Bin k covers k * sample_rate / n Hz.
void fft_magnitudes(float* re, float* im, float* mags, size_t n);
