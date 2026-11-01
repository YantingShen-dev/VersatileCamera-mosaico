#include "food_result.h"
#include "fridge_match.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool food_result_parse(const char *json, const char *date, fridge_item_t *items, unsigned *count)
{
    memset(items, 0, FRIDGE_BATCH_MAX * sizeof(*items));
    *count = 0;
    if (!fridge_date_valid(date)) return false;
    cJSON *root = cJSON_Parse(json);
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "items");
    bool valid = cJSON_IsArray(array) && cJSON_GetArraySize(array) <= FRIDGE_BATCH_MAX;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        if (!valid) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *q = cJSON_GetObjectItemCaseSensitive(item, "quantity");
        if (!cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) >= FRIDGE_NAME_BYTES ||
            !cJSON_IsNumber(q) || !isfinite(q->valuedouble) || q->valuedouble <= 0 || q->valuedouble > 100000) {
            valid = false; break;
        }
        double scaled = q->valuedouble * 1000;
        if (fabs(scaled - round(scaled)) > 0.00001) { valid = false; break; }
        unsigned index = 0;
        for (; index < *count; ++index) if (!strcmp(items[index].name, name->valuestring)) break;
        int64_t total = (int64_t)llround(scaled) + (index < *count ? items[index].quantity : 0);
        if (total > FRIDGE_QUANTITY_MAX) { valid = false; break; }
        snprintf(items[index].name, sizeof(items[index].name), "%s", name->valuestring);
        snprintf(items[index].date, sizeof(items[index].date), "%s", date);
        items[index].quantity = (int32_t)total;
        if (!fridge_item_valid(&items[index]) || name->valuestring[0] == ' ' ||
            name->valuestring[strlen(name->valuestring)-1] == ' ') { valid = false; break; }
        if (index == *count) ++*count;
    }
    cJSON_Delete(root);
    if (!valid) *count = 0;
    return valid;
}

/* Observations have no stock IDs and do not receive the inventory as model context. */
bool food_observations_parse(const char *json,const char *date,fridge_item_t *items,uint8_t *evidence,unsigned *count)
{
    *count=0;if(!fridge_date_valid(date))return false;
    cJSON *root=cJSON_Parse(json);const cJSON *array=cJSON_GetObjectItemCaseSensitive(root,"items");
    bool valid=cJSON_IsArray(array) && cJSON_GetArraySize(array)<=FRIDGE_BATCH_MAX;
    const cJSON *node=NULL;
    cJSON_ArrayForEach(node,array) {
        if(!valid)break;
        const cJSON *category=cJSON_GetObjectItemCaseSensitive(node,"category");
        const cJSON *brand=cJSON_GetObjectItemCaseSensitive(node,"brand");
        const cJSON *quantity=cJSON_GetObjectItemCaseSensitive(node,"quantity");
        const cJSON *proof=cJSON_GetObjectItemCaseSensitive(node,"evidence");
        const cJSON *kind=cJSON_GetObjectItemCaseSensitive(node,"kind");
        const cJSON *label=cJSON_GetObjectItemCaseSensitive(node,"label_text");
        if(!cJSON_IsString(category) || !category->valuestring[0] || strlen(category->valuestring)>=24 ||
           !cJSON_IsString(brand) || strlen(brand->valuestring)>=20 || !cJSON_IsString(proof) ||
           !cJSON_IsString(kind) || (strcmp(kind->valuestring,"food") && strcmp(kind->valuestring,"drink")) ||
           !cJSON_IsString(label) || strlen(label->valuestring)>180 || !cJSON_IsNumber(quantity) ||
           !isfinite(quantity->valuedouble) || quantity->valuedouble<=0 || quantity->valuedouble>100000 ||
           fabs(quantity->valuedouble*1000-round(quantity->valuedouble*1000))>0.00001) {valid=false;break;}
        unsigned level=!strcmp(proof->valuestring,"label")?FRIDGE_LABEL:!strcmp(proof->valuestring,"appearance")?FRIDGE_SHAPE:FRIDGE_UNCLEAR;
        if(strcmp(proof->valuestring,"label") && strcmp(proof->valuestring,"appearance") && strcmp(proof->valuestring,"unclear")) {valid=false;break;}
        fridge_item_t *item=&items[*count];memset(item,0,sizeof(*item));
        snprintf(item->category,sizeof(item->category),"%s",category->valuestring);
        snprintf(item->brand,sizeof(item->brand),"%s",level==FRIDGE_LABEL?brand->valuestring:"");
        snprintf(item->name,sizeof(item->name),"%s%s",item->brand,item->category);
        fridge_identity_infer(item);
        bool drink=!strcmp(kind->valuestring,"drink") || fridge_is_drink(item);
        if(drink && (level!=FRIDGE_LABEL || !fridge_drink_label_supports(item,label->valuestring))) {
            level=FRIDGE_UNCLEAR;item->brand[0]=0;snprintf(item->category,sizeof(item->category),"饮品");
        }
        snprintf(item->name,sizeof(item->name),"%s%s",item->brand,item->category);
        snprintf(item->date,sizeof(item->date),"%s",date);item->quantity=(int32_t)llround(quantity->valuedouble*1000);
        if(!fridge_item_valid(item)) {valid=false;break;}
        evidence[*count]=(uint8_t)(level|(drink?FRIDGE_DRINK:0));++*count;
    }
    cJSON_Delete(root);if(!valid)*count=0;return valid;
}
