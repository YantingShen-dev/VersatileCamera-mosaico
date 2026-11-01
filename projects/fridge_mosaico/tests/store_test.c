// Host-only regression tests; normal desktop stack budget permits fixtures here.
#include "fridge_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    fridge_store_t s, before;
    char error[128];
    fridge_item_t tomato = {"tomato", 5000, "2026-10-29"};
    fridge_store_init(&s);
    assert(fridge_store_valid(&s));
    assert(fridge_date_valid("2024-02-29"));
    assert(!fridge_date_valid("2025-02-29"));
    assert(!fridge_date_valid("2026-13-01"));
    fridge_begin(&s, 1);
    assert(fridge_apply(&s, s.session, &tomato, 1, error, sizeof(error)));
    assert(s.items[0].quantity == 5000);
    before=s;
    assert(!fridge_apply(&s, s.session, &tomato, 1, error, sizeof(error)));
    assert(!memcmp(&s,&before,sizeof(s)));
    fridge_begin(&s, 1);
    tomato.quantity=2000;
    assert(fridge_apply(&s,s.session,&tomato,1,error,sizeof(error)));
    assert(s.items[0].quantity==7000);
    assert(fridge_correct(&s,s.session,"tomato",3000,"",error,sizeof(error)));
    assert(s.items[0].quantity==8000); // baseline 5 + corrected batch 3
    assert(fridge_correct(&s,s.session,"tomato",1500,"",error,sizeof(error)));
    assert(s.items[0].quantity==6500);
    assert(fridge_undo(&s));assert(s.items[0].quantity==5000);
    assert(!fridge_undo(&s));
    fridge_begin(&s,1);strcpy(tomato.date,"2026-10-30");tomato.quantity=2000;
    assert(fridge_apply(&s,s.session,&tomato,1,error,sizeof(error)));
    assert(s.count==2);
    fridge_begin(&s,2);tomato.quantity=6000;
    assert(fridge_apply(&s,s.session,&tomato,1,error,sizeof(error)));
    assert(s.count==1 && s.items[0].quantity==1000 && !strcmp(s.items[0].date,"2026-10-30"));
    assert(fridge_correct(&s,s.session,"tomato",3000,"",error,sizeof(error)));
    assert(s.count==2 && s.items[0].quantity==2000 && s.items[1].quantity==2000);
    before=s;
    assert(!fridge_correct(&s,s.session,"tomato",8000,"",error,sizeof(error)));
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(!fridge_correct(&s,s.session-1,"tomato",1000,"",error,sizeof(error)));
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(fridge_correct(&s,s.session,"tomato",0,"",error,sizeof(error)));
    assert(s.items[0].quantity==5000 && s.items[1].quantity==2000);
    fridge_begin(&s,2);tomato.quantity=8000;before=s;
    assert(!fridge_apply(&s,s.session,&tomato,1,error,sizeof(error)));
    assert(!memcmp(&s,&before,sizeof(s)));
    fridge_begin(&s,1);tomato.quantity=-1;before=s;
    assert(!fridge_apply(&s,s.session,&tomato,1,error,sizeof(error)));
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(fridge_store_valid(&s));
    puts("PASS: atomic input/output, fractional corrections, FIFO, duplicate/stale requests, undo, dates");
    return 0;
}
