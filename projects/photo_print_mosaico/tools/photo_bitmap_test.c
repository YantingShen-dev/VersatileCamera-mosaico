#include "app_photo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    uint8_t *source = calloc(PHOTO_SIDE * PHOTO_SIDE, 3);
    uint8_t *bits = malloc(PHOTO_BYTES);
    uint16_t *preview = malloc(PHOTO_SIDE * PHOTO_SIDE * 2);
    assert(source && bits && preview);
    photo_dither(source, bits);
    for (unsigned i = 0; i < PHOTO_BYTES; ++i) assert(bits[i] == 0xff);
    memset(source, 255, PHOTO_SIDE * PHOTO_SIDE * 3);
    photo_dither(source, bits);
    for (unsigned i = 0; i < PHOTO_BYTES; ++i) assert(bits[i] == 0);
    memset(source, 128, PHOTO_SIDE * PHOTO_SIDE * 3);
    photo_dither(source, bits);
    photo_bitmap_preview(bits, preview);
    unsigned black = 0;
    for (unsigned i = 0; i < PHOTO_SIDE * PHOTO_SIDE; ++i) {
        bool bit = (bits[i / 8] & (0x80 >> (i % 8))) != 0;
        assert(preview[i] == (bit ? 0 : 0xffff));
        black += bit;
    }
    assert(black == PHOTO_SIDE * PHOTO_SIDE / 2);
    memset(bits, 0, PHOTO_BYTES);
    bits[0] = 0x80;
    bits[39] = 1;
    photo_bitmap_preview(bits, preview);
    assert(preview[0] == 0 && preview[319] == 0);
    assert(preview[1] == 0xffff && preview[320] == 0xffff);
    free(source); free(bits); free(preview);
    puts("bitmap: white/black/gray, bit order and exact preview passed");
    return 0;
}
