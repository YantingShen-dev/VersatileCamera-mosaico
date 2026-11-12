#include "app_photo.h"
#include <string.h>
void photo_dither(const uint8_t *bgr, uint8_t *bits) {
    /* Ordered dithering is deterministic and uses no task-stack image buffers. */
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    memset(bits, 0, PHOTO_BYTES);
    for (unsigned y = 0; y < PHOTO_SIDE; ++y) {
        for (unsigned x = 0; x < PHOTO_SIDE; ++x) {
            unsigned i = y * PHOTO_SIDE + x;
            unsigned luma = (29 * bgr[i * 3] + 150 * bgr[i * 3 + 1] + 77 * bgr[i * 3 + 2]) >> 8;
            unsigned threshold = bayer[(y & 3) * 4 + (x & 3)] * 16 + 8;
            if (luma < threshold)
                bits[i / 8] |= 0x80 >> (i & 7);
        }
    }
}
void photo_bitmap_preview(const uint8_t *bits, uint16_t *pixels) {
    for (unsigned i = 0; i < PHOTO_SIDE * PHOTO_SIDE; ++i)
        pixels[i] = (bits[i / 8] & (0x80 >> (i & 7))) ? 0 : 0xffff;
}
