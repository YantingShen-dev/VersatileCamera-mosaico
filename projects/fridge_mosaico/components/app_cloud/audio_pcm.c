#include "audio_pcm.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
size_t audio_pcm_decode(uint8_t *wav, size_t length)
{
    size_t offset = 0, bytes = 0;
    unsigned rate = 0, channels = 0, bits = 0, format = 0;
    if (length < 12 || memcmp(wav,"RIFF",4) || memcmp(wav+8,"WAVE",4)) return 0;
    for (size_t pos = 12; pos + 8 <= length;) {
        size_t size = le32(wav + pos + 4);
        const int data = !memcmp(wav + pos,"data",4);
        // Qwen's complete HTTP WAV uses streaming length sentinels around 2 GiB.
        // Accept only a final data chunk with that sentinel, never a short fixed chunk.
        if (data && size >= 0x7fff0000U && size > length - pos - 8) size = length - pos - 8;
        if (size > length - pos - 8) return 0;
        if (!memcmp(wav + pos,"fmt ",4) && size >= 16) {
            format = le16(wav+pos+8); channels = le16(wav+pos+10);
            rate = le32(wav+pos+12); bits = le16(wav+pos+22);
        }
        if (data) { offset = pos + 8; bytes = size; }
        pos += 8 + size + (size & 1);
    }
    if (!bytes || bytes % 2 || format != 1 || channels != 1 || bits != 16 || (rate != 16000 && rate != 24000)) return 0;
    size_t samples = bytes / 2, target = samples * 16000 / rate;
    for (size_t n = 0; n < target; ++n) {
        size_t twice = n * (rate == 24000 ? 3 : 2), source = twice / 2;
        int a = (int16_t)le16(wav + offset + source*2);
        int b = (int16_t)le16(wav + offset + (source+1 < samples ? source+1 : source)*2);
        int16_t sample = twice % 2 ? (a+b)/2 : a;
        wav[n*2] = sample & 255; wav[n*2+1] = (sample >> 8) & 255;
    }
    return target * 2;
}
