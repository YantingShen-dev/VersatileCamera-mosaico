#include "app_photo.h"
#include <string.h>
static unsigned gray(const uint8_t *bgr, unsigned x, unsigned y) {
    const uint8_t *p = bgr + (y * PHOTO_SIDE + x) * 3;
    return (29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8;
}
static void black(uint8_t *bits, unsigned x, unsigned y) {
    unsigned i = y * PHOTO_SIDE + x;
    bits[i / 8] |= 0x80 >> (i & 7);
}
void photo_filter_apply(const uint8_t *bgr, uint8_t *bits, photo_filter_t filter) {
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    static const uint8_t circle[64] = {
        60, 52, 44, 32, 33, 45, 53, 61, 54, 34, 24, 16, 17, 25, 35, 55, 46, 26, 12, 4,  5,  13,
        27, 47, 36, 18, 6,  0,  1,  7,  19, 37, 38, 20, 8,  2,  3,  9,  21, 39, 48, 28, 14, 10,
        11, 15, 29, 49, 56, 40, 30, 22, 23, 31, 41, 57, 62, 58, 50, 42, 43, 51, 59, 63};
    /* Original 5x7 ASCII glyph columns, padded to 6x8 cells. */
    static const uint8_t glyphs[][5] = {
        {0, 0, 0, 0, 0},        {0, 0, 64, 0, 0},     {0, 0, 36, 0, 0},     {0, 8, 8, 8, 0},
        {0, 20, 20, 20, 0},     {8, 8, 62, 8, 8},     {20, 8, 62, 8, 20},   {34, 20, 8, 20, 34},
        {20, 127, 20, 127, 20}, {62, 65, 93, 85, 30}, {127, 1, 127, 1, 127}};
    memset(bits, 0, PHOTO_BYTES);
    if (filter == PHOTO_ASCII) {
        for (unsigned y = 0; y < PHOTO_SIDE; y += 8)
            for (unsigned x = 0; x < PHOTO_SIDE; x += 6) {
                unsigned sum = 0;
                for (unsigned dy = 0; dy < 8; dy++)
                    for (unsigned dx = 0; dx < 6; dx++)
                        sum += gray(bgr, x + dx, y + dy);
                unsigned level = (255 - sum / 48) * 10 / 255;
                for (unsigned dy = 0; dy < 7; dy++)
                    for (unsigned dx = 0; dx < 5; dx++)
                        if (glyphs[level][dx] & (1 << dy))
                            black(bits, x + dx, y + dy);
            }
        return;
    }
    for (unsigned y = 0; y < PHOTO_SIDE; y++)
        for (unsigned x = 0; x < PHOTO_SIDE; x++) {
            unsigned luma = gray(bgr, x, y), threshold = bayer[(y & 3) * 4 + (x & 3)] * 16 + 8;
            if (filter == PHOTO_DOTS) {
                // One coherent round dot per 8x8 cell; gray is averaged within its cell.
                unsigned sx = x & ~7U, sy = y & ~7U, sum = 0;
                // Four samples avoid blurring edges as much as a large image blur.
                sum = gray(bgr, sx + 2, sy + 2) + gray(bgr, sx + 5, sy + 2) +
                      gray(bgr, sx + 2, sy + 5) + gray(bgr, sx + 5, sy + 5);
                luma = sum / 4;
                threshold = 255 - circle[(y & 7) * 8 + (x & 7)] * 4;
            } else if (filter == PHOTO_WOODCUT) {
                int detail = (int)luma;
                if (x && y && x + 1 < PHOTO_SIDE && y + 1 < PHOTO_SIDE)
                    detail = 3 * (int)luma - ((int)gray(bgr, x - 1, y) + gray(bgr, x + 1, y) +
                                              gray(bgr, x, y - 1) + gray(bgr, x, y + 1)) /
                                                 2;
                luma = detail < 0 ? 0 : detail > 255 ? 255 : (unsigned)detail;
                threshold = 132;
            }
            bool on = luma < threshold;
            if (filter == PHOTO_INVERT)
                on = !on;
            if (on)
                black(bits, x, y);
        }
}
void photo_dither(const uint8_t *bgr, uint8_t *bits) {
    photo_filter_apply(bgr, bits, PHOTO_NORMAL);
}
void photo_bitmap_preview(const uint8_t *bits, uint16_t *pixels) {
    for (unsigned i = 0; i < PHOTO_SIDE * PHOTO_SIDE; i++)
        pixels[i] = (bits[i / 8] & (0x80 >> (i & 7))) ? 0 : 0xffff;
}
