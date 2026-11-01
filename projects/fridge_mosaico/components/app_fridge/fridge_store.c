// SPDX-License-Identifier: Apache-2.0
#include "fridge_store.h"
#include "fridge_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, unsigned size, const char *message)
{
    if (error && size) { snprintf(error, size, "%s", message); }
    return false;
}

void fridge_store_init(fridge_store_t *s)
{
    memset(s, 0, sizeof(*s));
    s->version = FRIDGE_STORE_VERSION;
    s->next_product_id = 1;
}

bool fridge_date_valid(const char *date)
{
    if (!date || strlen(date) != 10 || date[4] != '-' || date[7] != '-') { return false; }
    for (unsigned i = 0; i < 10; ++i) {
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) { return false; }
    }
    unsigned year = 0, month = 0, day = 0;
    if (sscanf(date, "%u-%u-%u", &year, &month, &day) != 3 ||
            year < 2000 || year > 2199 || month < 1 || month > 12) { return false; }
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned limit = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) { ++limit; }
    return day >= 1 && day <= limit;
}

bool fridge_item_valid(const fridge_item_t *item)
{
    if (!memchr(item->name, 0, sizeof(item->name)) || !item->name[0] ||
            !memchr(item->date, 0, sizeof(item->date)) ||
            !memchr(item->category,0,sizeof(item->category)) || !memchr(item->brand,0,sizeof(item->brand)) ||
            item->product_id > INT32_MAX || item->quantity < 0 || item->quantity > FRIDGE_QUANTITY_MAX) { return false; }
    for (const unsigned char *p = (const unsigned char *)item->name; *p; ++p) {
        if (*p < 32 || *p == 127) { return false; }
    }
    return fridge_date_valid(item->date);
}

bool fridge_store_valid(const fridge_store_t *s)
{
    if (s->version != FRIDGE_STORE_VERSION || s->count > FRIDGE_CAPACITY ||
            s->baseline_count > FRIDGE_CAPACITY || s->batch_count > FRIDGE_BATCH_MAX ||
            s->direction > 2 || s->undo_available > 1 || !s->next_product_id || s->next_product_id>INT32_MAX) { return false; }
    for(unsigned group=0;group<3;++group) {
        const fridge_item_t *items=group==0?s->items:group==1?s->baseline:s->batch;
        unsigned count=group==0?s->count:group==1?s->baseline_count:s->batch_count;
        for(unsigned i=0;i<count;++i)if(!fridge_item_valid(&items[i]) || !items[i].product_id || items[i].product_id>=s->next_product_id)return false;
    }
    return true;
}

typedef struct {char name[64];int32_t quantity;char date[11];} legacy_item_t;
typedef struct {
    uint32_t version,revision,session,applied_session;
    uint16_t count,baseline_count,batch_count;uint8_t direction,undo_available;
    legacy_item_t items[96],baseline[96],batch[24];
} legacy_store_t;

static uint32_t known_id(const fridge_store_t *s,const char *name) {
    for(unsigned i=0;i<s->count;++i)if(s->items[i].product_id && !strcmp(s->items[i].name,name))return s->items[i].product_id;
    for(unsigned i=0;i<s->baseline_count;++i)if(s->baseline[i].product_id && !strcmp(s->baseline[i].name,name))return s->baseline[i].product_id;
    for(unsigned i=0;i<s->batch_count;++i)if(s->batch[i].product_id && !strcmp(s->batch[i].name,name))return s->batch[i].product_id;
    return 0;
}
bool fridge_store_decode(fridge_store_t *s,const void *data,size_t size) {
    if(size==sizeof(*s)) {memcpy(s,data,size);return fridge_store_valid(s);}
    if(size!=sizeof(legacy_store_t))return false;
    const legacy_store_t *old=data;
    if(old->version!=1 || old->count>96 || old->baseline_count>96 || old->batch_count>24 || old->direction>2 || old->undo_available>1)return false;
    fridge_store_init(s);
    s->revision=old->revision;s->session=old->session;s->applied_session=old->applied_session;
    s->count=old->count;s->baseline_count=old->baseline_count;s->batch_count=old->batch_count;
    s->direction=old->direction;s->undo_available=old->undo_available;
    for(unsigned group=0;group<3;++group) {
        const legacy_item_t *from=group==0?old->items:group==1?old->baseline:old->batch;
        fridge_item_t *to=group==0?s->items:group==1?s->baseline:s->batch;
        unsigned count=group==0?s->count:group==1?s->baseline_count:s->batch_count;
        for(unsigned i=0;i<count;++i) {
            memcpy(to[i].name,from[i].name,64);memcpy(to[i].date,from[i].date,11);to[i].quantity=from[i].quantity;
            if(!fridge_item_valid(&to[i]))return false;
            to[i].product_id=known_id(s,to[i].name);
            if(!to[i].product_id)to[i].product_id=s->next_product_id++;
            fridge_identity_infer(&to[i]);
        }
    }
    return fridge_store_valid(s);
}

void fridge_begin(fridge_store_t *s, unsigned direction)
{
    if (direction != 1 && direction != 2) { return; }
    memcpy(s->baseline, s->items, sizeof(s->items));
    s->baseline_count = s->count;
    s->batch_count = 0;
    s->direction = (uint8_t)direction;
    s->undo_available = 0;
    ++s->session;
    if (!s->session) { ++s->session; }
    ++s->revision;
}

static bool rebuild(fridge_store_t *s, char *error, unsigned size)
{
    memcpy(s->items, s->baseline, sizeof(s->items));
    s->count = s->baseline_count;
    for (unsigned b = 0; b < s->batch_count; ++b) {
        const fridge_item_t *item = &s->batch[b];
        if (!fridge_item_valid(item)) { return fail(error, size, "名称、数量或日期无效"); }
        int32_t remaining = item->quantity;
        if (!remaining) { continue; }
        if (s->direction == 1) {
            unsigned i = 0;
            for (; i < s->count; ++i) {
                if (s->items[i].product_id == item->product_id && !strcmp(s->items[i].date, item->date)) { break; }
            }
            if (i == s->count) {
                if (s->count == FRIDGE_CAPACITY) { return fail(error, size, "库存记录已满"); }
                s->items[s->count++] = *item;
            } else {
                int64_t total = (int64_t)s->items[i].quantity + remaining;
                if (total > FRIDGE_QUANTITY_MAX) { return fail(error, size, "数量超出范围"); }
                s->items[i].quantity = (int32_t)total;
            }
            // A user-confirmed enrichment updates every dated lot of that product.
            for(unsigned k=0;k<s->count;++k)if(s->items[k].product_id==item->product_id) {
                snprintf(s->items[k].name,sizeof(s->items[k].name),"%s",item->name);
                snprintf(s->items[k].category,sizeof(s->items[k].category),"%s",item->category);
                snprintf(s->items[k].brand,sizeof(s->items[k].brand),"%s",item->brand);
            }
        } else {
            while (remaining > 0) {
                int earliest = -1;
                for (unsigned i = 0; i < s->count; ++i) {
                    if (s->items[i].quantity > 0 && s->items[i].product_id == item->product_id &&
                            (earliest < 0 || strcmp(s->items[i].date, s->items[earliest].date) < 0)) {
                        earliest = (int)i;
                    }
                }
                if (earliest < 0) { return fail(error, size, "库存不足，请修改本次数量"); }
                int32_t used = remaining < s->items[earliest].quantity ? remaining : s->items[earliest].quantity;
                s->items[earliest].quantity -= used;
                remaining -= used;
            }
        }
    }
    unsigned out = 0;
    for (unsigned i = 0; i < s->count; ++i) {
        if (s->items[i].quantity) { s->items[out++] = s->items[i]; }
    }
    s->count = (uint16_t)out;
    s->undo_available = 1;
    s->applied_session = s->session;
    ++s->revision;
    return true;
}

bool fridge_apply(fridge_store_t *s, uint32_t session, const fridge_item_t *items,
                  unsigned count, char *error, unsigned size)
{
    if (!session || session != s->session || s->applied_session == session ||
            (s->direction != 1 && s->direction != 2)) { return fail(error, size, "请求已过期或已经处理"); }
    if (!count || count > FRIDGE_BATCH_MAX) { return fail(error, size, "没有可确认的食材"); }
    fridge_store_t *next = malloc(sizeof(*next));
    if (!next) { return fail(error, size, "内存不足"); }
    *next = *s;
    next->batch_count = (uint16_t)count;
    memcpy(next->batch, items, count * sizeof(*items));
    for(unsigned i=0;i<count;++i) {
        fridge_item_t *item=&next->batch[i];
        if(!fridge_item_valid(item)) {free(next);return fail(error,size,"名称、数量或日期无效");}
        fridge_identity_infer(item);
        if(item->product_id && !fridge_product(s,item->product_id)) {free(next);return fail(error,size,"库存商品已变化，请重拍");}
        if(!item->product_id) {
            item->product_id=known_id(next,item->name);
            if(!item->product_id && next->direction==1 && next->next_product_id<INT32_MAX)item->product_id=next->next_product_id++;
            if(!item->product_id) {free(next);return fail(error,size,"库存没有此商品，请重新确认");}
        }
    }
    unsigned merged=0;
    for(unsigned i=0;i<next->batch_count;++i) {
        unsigned j=0;
        for(;j<merged;++j)if(next->batch[j].product_id==next->batch[i].product_id && !strcmp(next->batch[j].date,next->batch[i].date))break;
        if(j==merged)next->batch[merged++]=next->batch[i];
        else {
            int64_t quantity=(int64_t)next->batch[j].quantity+next->batch[i].quantity;
            if(quantity>FRIDGE_QUANTITY_MAX){free(next);return fail(error,size,"数量超出范围");}
            next->batch[j].quantity=(int32_t)quantity;
        }
    }
    next->batch_count=(uint16_t)merged;
    bool ok = rebuild(next, error, size);
    if (ok) { *s = *next; }
    free(next);
    return ok;
}

bool fridge_correct(fridge_store_t *s, uint32_t session, const char *name,
                    int32_t quantity, const char *date, char *error, unsigned size)
{
    if (session != s->session || !s->undo_available || quantity < 0 ||
            quantity > FRIDGE_QUANTITY_MAX) { return fail(error, size, "当前没有可修改的操作"); }
    fridge_store_t *next = malloc(sizeof(*next));
    if (!next) { return fail(error, size, "内存不足"); }
    *next = *s;
    int match = -1;
    for (unsigned i = 0; i < s->batch_count; ++i) {
        if (!strcmp(s->batch[i].name, name)) {
            if (match >= 0) { free(next); return fail(error, size, "同名食材有多个批次，请明确日期"); }
            match = (int)i;
        }
    }
    if (match < 0) { free(next); return fail(error, size, "本次操作中没有这个食材"); }
    next->batch[match].quantity = quantity;
    if (date && date[0]) {
        if (!fridge_date_valid(date)) { free(next); return fail(error, size, "日期无效"); }
        snprintf(next->batch[match].date, sizeof(next->batch[match].date), "%s", date);
    }
    bool ok = rebuild(next, error, size);
    if (ok) { *s = *next; }
    free(next);
    return ok;
}

bool fridge_undo(fridge_store_t *s)
{
    if (!s->undo_available) { return false; }
    memcpy(s->items, s->baseline, sizeof(s->items));
    s->count = s->baseline_count;
    s->undo_available = 0;
    s->batch_count = 0;
    ++s->revision;
    return true;
}
