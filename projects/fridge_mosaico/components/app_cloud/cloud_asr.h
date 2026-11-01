#pragma once
#include "app_cloud.h"
struct cloud_asr;
typedef bool (*asr_transcript_cb)(void *, const char *, uint32_t, uint32_t, const fridge_item_t *, unsigned);
cloud_asr *cloud_asr_create(const app_cloud_callbacks_t &, asr_transcript_cb, void *, const char *);
void cloud_asr_configure(cloud_asr *, const char *);
void cloud_asr_state(cloud_asr *, const fridge_snapshot_t *);
void cloud_asr_feed(cloud_asr *, const int16_t *, size_t);
void cloud_asr_diagnostics(cloud_asr *, cJSON *);
