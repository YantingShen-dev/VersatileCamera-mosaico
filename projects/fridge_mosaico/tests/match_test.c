/* Desktop fixtures may use the host stack. */
#include "fridge_match.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static fridge_item_t food(const char *brand,const char *category,int q) {
    fridge_item_t i={0};snprintf(i.name,64,"%s%s",brand,category);snprintf(i.category,24,"%s",category);
    snprintf(i.brand,20,"%s",brand);strcpy(i.date,"2026-11-01");i.quantity=q;return i;
}
int main(void) {
    fridge_store_t s,before;fridge_matches_t m;char error[128];fridge_store_init(&s);
    fridge_item_t lays=food("乐事","薯片",2000),chips=food("","薯片",1000);
    fridge_begin(&s,1);assert(fridge_apply(&s,s.session,&lays,1,error,128));unsigned id=s.items[0].product_id;
    fridge_begin(&s,2);fridge_match(&s,&chips,FRIDGE_SHAPE,&m);
    assert(m.automatic && m.count==1 && m.ids[0]==id);chips.product_id=id;
    assert(fridge_apply(&s,s.session,&chips,1,error,128));assert(s.items[0].quantity==1000);
    assert(fridge_undo(&s));assert(s.items[0].quantity==2000);
    fridge_item_t copico=food("可比克","薯片",1000);
    fridge_begin(&s,1);assert(fridge_apply(&s,s.session,&copico,1,error,128));fridge_begin(&s,2);
    chips.product_id=0;fridge_match(&s,&chips,FRIDGE_SHAPE,&m);assert(!m.automatic && m.count==2);
    fridge_match(&s,&lays,FRIDGE_LABEL,&m);assert(m.automatic && m.count==1 && m.ids[0]==id);
    fridge_item_t missing=food("其他品牌","薯片",1000);fridge_match(&s,&missing,FRIDGE_LABEL,&m);assert(!m.count);
    fridge_item_t coffee=food("","黑咖啡",1000),soda=food("","气泡水",1000);
    fridge_begin(&s,1);assert(fridge_apply(&s,s.session,&coffee,1,error,128));fridge_begin(&s,2);
    fridge_match(&s,&soda,FRIDGE_UNCLEAR|FRIDGE_DRINK,&m);assert(!m.automatic && m.count==1);
    fridge_match(&s,&soda,FRIDGE_LABEL|FRIDGE_DRINK,&m);assert(!m.count); // confirmed conflicting label cannot consume coffee
    assert(!fridge_drink_label_supports(&coffee,"瑞幸"));assert(fridge_drink_label_supports(&coffee,"冰美式"));
    fridge_item_t dump[2]={coffee,soda};before=s;
    assert(!fridge_apply(&s,s.session,dump,2,error,128));assert(!memcmp(&s,&before,sizeof(s)));
    // Generic IN may be enriched only by explicit selection when a brand becomes visible.
    fridge_store_init(&s);chips.product_id=0;fridge_begin(&s,1);assert(fridge_apply(&s,s.session,&chips,1,error,128));
    id=s.items[0].product_id;fridge_begin(&s,1);fridge_match(&s,&lays,FRIDGE_LABEL,&m);
    assert(!m.automatic && m.count==2 && m.ids[1]==0);
    lays.product_id=id;assert(fridge_apply(&s,s.session,&lays,1,error,128));assert(s.count==1 && !strcmp(s.items[0].name,"乐事薯片"));
    assert(fridge_undo(&s));assert(!strcmp(s.items[0].name,"薯片"));
    // Legacy v1 bytes migrate without losing date, quantity or undo baseline.
    struct old_item {char name[64];int32_t quantity;char date[11];};
    struct old_store {uint32_t version,revision,session,applied;uint16_t count,baseline_count,batch_count;uint8_t direction,undo;struct old_item items[96],baseline[96],batch[24];} old={0};
    old.version=1;old.count=old.baseline_count=1;old.session=7;old.direction=1;old.undo=1;
    strcpy(old.items[0].name,"乐事原味薯片");old.items[0].quantity=2250;strcpy(old.items[0].date,"2026-10-29");old.baseline[0]=old.items[0];old.baseline[0].quantity=1250;
    assert(fridge_store_decode(&s,&old,sizeof(old)));assert(s.items[0].quantity==2250 && s.items[0].product_id==s.baseline[0].product_id);
    assert(!strcmp(s.items[0].category,"薯片") && !strcmp(s.items[0].brand,"乐事"));assert(fridge_undo(&s));assert(s.items[0].quantity==1250);
    old.count=97;assert(!fridge_store_decode(&s,&old,sizeof(old)));
    printf("PASS: identity matching, ambiguity, drink evidence, atomic rollback, enrichment, v1 migration; store=%zu bytes\n",sizeof(s));
}
