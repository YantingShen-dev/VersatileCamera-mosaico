// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRIDGE_CAPACITY 96
#define FRIDGE_BATCH_MAX 24
#define FRIDGE_NAME_BYTES 64
#define FRIDGE_QUANTITY_MAX 100000000
#define FRIDGE_STORE_VERSION 2

typedef struct {
    char name[FRIDGE_NAME_BYTES];
    int32_t quantity; /* thousandths: 1500 means 1.5; no unit field */
    char date[11];    /* YYYY-MM-DD */
    uint32_t product_id; /* assigned locally, stable across names and entry dates */
    char category[24];
    char brand[20];
} fridge_item_t;

typedef struct {
    uint32_t version;
    uint32_t revision;
    uint32_t session;
    uint32_t applied_session;
    uint16_t count;
    uint16_t baseline_count;
    uint16_t batch_count;
    uint8_t direction; /* 1 = in, 2 = out */
    uint8_t undo_available;
    uint32_t next_product_id;
    fridge_item_t items[FRIDGE_CAPACITY];
    fridge_item_t baseline[FRIDGE_CAPACITY];
    fridge_item_t batch[FRIDGE_BATCH_MAX];
} fridge_store_t;

/* Single owner, no I/O, heap-own the store and scratch copies. All rejected
 * operations leave stock unchanged. Persist successful mutations before ack. */
void fridge_store_init(fridge_store_t *store);
bool fridge_date_valid(const char *date);
bool fridge_item_valid(const fridge_item_t *item);
bool fridge_store_valid(const fridge_store_t *store);
/* Decode current or legacy v1 data. Never writes storage or changes input. */
bool fridge_store_decode(fridge_store_t *store, const void *data, size_t size);
void fridge_begin(fridge_store_t *store, unsigned direction);
bool fridge_apply(fridge_store_t *store, uint32_t session,
                  const fridge_item_t *items, unsigned count, char *error, unsigned size);
bool fridge_correct(fridge_store_t *store, uint32_t session, const char *name,
                    int32_t quantity, const char *date, char *error, unsigned size);
bool fridge_undo(fridge_store_t *store);
#ifdef __cplusplus
}
#endif
