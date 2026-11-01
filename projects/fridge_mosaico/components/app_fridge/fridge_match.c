#include "fridge_match.h"
#include <stdio.h>
#include <string.h>

static const struct { const char *alias, *category; } aliases[] = {
    {"黑咖", "黑咖啡"}, {"美式", "黑咖啡"}, {"气泡水", "气泡水"}, {"苏打水", "气泡水"},
    {"拿铁", "拿铁"}, {"咖啡", "咖啡"}, {"AD钙奶", "AD钙奶"}, {"酸奶", "酸奶"},
    {"牛奶", "牛奶"}, {"奶茶", "奶茶"}, {"果汁", "果汁"}, {"可乐", "可乐"},
    {"矿泉水", "饮用水"}, {"纯净水", "饮用水"}, {"饮用水", "饮用水"},
    {"杯装饮品", "饮品"}, {"饮料", "饮品"}, {"饮品", "饮品"},
    {"薯片", "薯片"}, {"夹心饼干", "饼干"}, {"饼干", "饼干"},
    {"腰果", "腰果"}, {"花生", "花生"}, {"牛肉干", "牛肉干"}, {"巧克力", "巧克力"},
    {"番茄", "西红柿"}, {"西红柿", "西红柿"}, {"苹果", "苹果"}, {"香蕉", "香蕉"}
};
void fridge_identity_infer(fridge_item_t *item) {
    const char *source=item->category[0]?item->category:item->name;
    for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);++i) {
        if(strstr(source,aliases[i].alias)) {
            snprintf(item->category,sizeof(item->category),"%s",aliases[i].category);break;
        }
    }
    /* Legacy brands are inferred only from explicit names, not appearance. */
    if(!item->brand[0]) {
        static const char *brands[]={"乐事","可比克","奥利奥","娃哈哈","百事","可口可乐","农夫山泉","瑞幸","星巴克"};
        for(unsigned i=0;i<sizeof(brands)/sizeof(*brands);++i)
            if(strstr(item->name,brands[i])) {snprintf(item->brand,sizeof(item->brand),"%s",brands[i]);break;}
    }
}
bool fridge_is_drink(const fridge_item_t *item) {
    const char *s=item->category[0]?item->category:item->name;
    return strstr(s,"咖啡") || strstr(s,"拿铁") || strstr(s,"气泡水") || strstr(s,"饮用水") || strstr(s,"矿泉水") || strstr(s,"奶") ||
           strstr(s,"茶") || strstr(s,"饮") || strstr(s,"汁") || strstr(s,"可乐") || strstr(s,"酒");
}
bool fridge_drink_label_supports(const fridge_item_t *item,const char *label) {
    if(!label || !label[0] || !item->category[0] || !strcmp(item->category,"饮品"))return false;
    if(strstr(label,item->category))return true;
    for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);++i)
        if(!strcmp(item->category,aliases[i].category) && strstr(label,aliases[i].alias))return true;
    return false;
}
const fridge_item_t *fridge_product(const fridge_store_t *s,uint32_t id) {
    for(unsigned i=0;id && i<s->baseline_count;++i)
        if(s->baseline[i].product_id==id)return &s->baseline[i];
    return NULL;
}
static void add(fridge_matches_t *m,uint32_t id) {
    for(unsigned i=0;i<m->count;++i)if(m->ids[i]==id)return;
    if(m->count<FRIDGE_CAPACITY+1)m->ids[m->count++]=id;
}
void fridge_match(const fridge_store_t *s,const fridge_item_t *seen,unsigned evidence,fridge_matches_t *m) {
    memset(m,0,sizeof(*m));
    const bool drink=(evidence&FRIDGE_DRINK) || fridge_is_drink(seen);
    const bool uncertain=(evidence&3)==FRIDGE_UNCLEAR || (drink && (evidence&3)!=FRIDGE_LABEL);
    for(unsigned i=0;i<s->baseline_count;++i) {
        const fridge_item_t *candidate=&s->baseline[i];
        if(candidate->quantity<=0)continue;
        bool same=seen->category[0] && candidate->category[0] && !strcmp(seen->category,candidate->category);
        bool exact=!strcmp(seen->name,candidate->name);
        if(uncertain) {
            // An unlabelled drink requires selection even with only one candidate.
            if(drink ? fridge_is_drink(candidate) : (same || exact))add(m,candidate->product_id);
        } else if((same || exact) && (!seen->brand[0] || !candidate->brand[0] || !strcmp(seen->brand,candidate->brand))) {
            add(m,candidate->product_id);
        }
    }
    if(uncertain)return;
    if(seen->brand[0]) {
        unsigned exact=0;
        for(unsigned i=0;i<m->count;++i) {
            const fridge_item_t *p=fridge_product(s,m->ids[i]);
            if(p && !strcmp(p->brand,seen->brand))m->ids[exact++]=m->ids[i];
        }
        if(exact)m->count=exact;
    }
    if(s->direction==1) {
        if(m->count==1) {
            const fridge_item_t *p=fridge_product(s,m->ids[0]);
            if(!strcmp(seen->brand,p->brand)) {m->automatic=true;return;}
        }
        // Missing/previously unknown brand: user chooses reuse/enrichment or a new product.
        add(m,0);m->automatic=m->count==1;
    } else m->automatic=m->count==1;
}
