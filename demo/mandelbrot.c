#include "mandelbrot.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BMP_HEADER_SIZE 54

static void put_u16(uint8_t *p, uint16_t v)
{
        p[0] = (uint8_t)(v & 0xff);
        p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32(uint8_t *p, uint32_t v)
{
        p[0] = (uint8_t)(v & 0xff);
        p[1] = (uint8_t)((v >> 8) & 0xff);
        p[2] = (uint8_t)((v >> 16) & 0xff);
        p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void color_pixel(int iter, int max_iter, uint8_t *bgr)
{
        if (iter >= max_iter) {
                bgr[0] = 8;
                bgr[1] = 8;
                bgr[2] = 12;
                return;
        }

        int t = (iter * 255) / max_iter;
        bgr[0] = (uint8_t)(80 + (t * 120) / 255);
        bgr[1] = (uint8_t)((t * t) / 255);
        bgr[2] = (uint8_t)(255 - t / 3);
}

int mandelbrot_bmp(int width, int height, int max_iter,
                   uint8_t **out, size_t *out_len)
{
        if (!out || !out_len || width <= 0 || height <= 0 || max_iter <= 0)
                return -1;

        size_t row = ((size_t)width * 3u + 3u) & ~3u;
        size_t pixels = row * (size_t)height;
        size_t total = BMP_HEADER_SIZE + pixels;
        if (total < pixels)
                return -1;

        uint8_t *bmp = calloc(1, total);
        if (!bmp)
                return -1;

        bmp[0] = 'B';
        bmp[1] = 'M';
        put_u32(&bmp[2], (uint32_t)total);
        put_u32(&bmp[10], BMP_HEADER_SIZE);
        put_u32(&bmp[14], 40);
        put_u32(&bmp[18], (uint32_t)width);
        put_u32(&bmp[22], (uint32_t)height);
        put_u16(&bmp[26], 1);
        put_u16(&bmp[28], 24);
        put_u32(&bmp[34], (uint32_t)pixels);

        for (int y = 0; y < height; y++) {
                uint8_t *dst = bmp + BMP_HEADER_SIZE + (size_t)(height - 1 - y) * row;
                double cy = ((double)y / (double)(height - 1)) * 2.4 - 1.2;

                for (int x = 0; x < width; x++) {
                        double cx = ((double)x / (double)(width - 1)) * 3.5 - 2.5;
                        double zx = 0.0;
                        double zy = 0.0;
                        int iter = 0;

                        while (zx * zx + zy * zy <= 4.0 && iter < max_iter) {
                                double nx = zx * zx - zy * zy + cx;
                                zy = 2.0 * zx * zy + cy;
                                zx = nx;
                                iter++;
                        }

                        color_pixel(iter, max_iter, dst + (size_t)x * 3u);
                }
        }

        *out = bmp;
        *out_len = total;
        return 0;
}
