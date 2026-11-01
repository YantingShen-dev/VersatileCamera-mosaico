#pragma once
#include "fridge_store.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Evidence comes from image observations, never from stock names. */
enum { FRIDGE_UNCLEAR=0, FRIDGE_SHAPE=1, FRIDGE_LABEL=2, FRIDGE_DRINK=4 };
typedef struct {
    uint32_t ids[FRIDGE_CAPACITY + 1]; /* 0 = explicitly create a new IN product */
    unsigned count;
    bool automatic;
} fridge_matches_t;
void fridge_identity_infer(fridge_item_t *item); /* conservative legacy aliases */
bool fridge_is_drink(const fridge_item_t *item);
bool fridge_drink_label_supports(const fridge_item_t *item,const char *label);
void fridge_match(const fridge_store_t *store, const fridge_item_t *seen,
                  unsigned evidence, fridge_matches_t *matches);
const fridge_item_t *fridge_product(const fridge_store_t *store, uint32_t id);
#ifdef __cplusplus
}
#endif
