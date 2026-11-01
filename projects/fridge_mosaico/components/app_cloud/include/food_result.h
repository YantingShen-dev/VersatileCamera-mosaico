#pragma once
#include "fridge_store.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Strict cloud JSON validation. Output is scratch until this returns true. */
bool food_result_parse(const char *json, const char *date, fridge_item_t *items, unsigned *count);
bool food_observations_parse(const char *json,const char *date,fridge_item_t *items,uint8_t *evidence,unsigned *count);
#ifdef __cplusplus
}
#endif
