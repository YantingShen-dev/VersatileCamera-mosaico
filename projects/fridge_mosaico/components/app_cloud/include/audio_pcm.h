#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Validates complete bounded WAV and converts mono PCM16 16/24 kHz in place to 16 kHz. */
size_t audio_pcm_decode(uint8_t *wav, size_t length);
#ifdef __cplusplus
}
#endif
