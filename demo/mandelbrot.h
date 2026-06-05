#ifndef DEMO_MANDELBROT_H
#define DEMO_MANDELBROT_H

#include <stddef.h>
#include <stdint.h>

int mandelbrot_bmp(int width, int height, int max_iter,
                   uint8_t **out, size_t *out_len);

#endif
