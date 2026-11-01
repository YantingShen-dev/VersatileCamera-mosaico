// Desktop-only fixtures have the host's normal stack budget.
#include "food_result.h"
#include "audio_pcm.h"
#include "fridge_match.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    fridge_item_t items[FRIDGE_BATCH_MAX]; unsigned count;
    assert(food_result_parse("{\"items\":[{\"name\":\"tomato\",\"quantity\":1.5},{\"name\":\"tomato\",\"quantity\":2}]}","2026-10-31",items,&count));
    assert(count == 1 && items[0].quantity == 3500 && !strcmp(items[0].date,"2026-10-31"));
    assert(food_result_parse("{\"items\":[]}","2026-10-31",items,&count) && !count);
    const char *invalid[] = {
        "{}", "{\"items\":null}", "{\"items\":[{\"name\":\"x\",\"quantity\":true}]}",
        "{\"items\":[{\"name\":\"x\",\"quantity\":0}]}", "{\"items\":[{\"name\":\"x\",\"quantity\":-1}]}",
        "{\"items\":[{\"name\":\"x\",\"quantity\":0.0001}]}", "{\"items\":[{\"name\":\"x\",\"quantity\":100001}]}",
        "{\"items\":[{\"name\":\"x\",\"quantity\":100000},{\"name\":\"x\",\"quantity\":1}]}",
        "{\"items\":[{\"name\":\"a\\nb\",\"quantity\":1}]}", "{\"items\":[{\"name\":\" \",\"quantity\":1}]}"
    };
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) assert(!food_result_parse(invalid[i],"2026-10-31",items,&count) && !count);
    assert(!food_result_parse("{\"items\":[]}","2025-02-29",items,&count));
    puts("PASS: native food validation, duplicate merging, fractions, bounds and dates");
    uint8_t evidence[FRIDGE_BATCH_MAX];
    const char *labels[]={"", "瑞幸", "冰美式"};
    for(unsigned i=0;i<3;++i) {
        char json[512];
        snprintf(json,sizeof(json),"{\"items\":[{\"category\":\"黑咖啡\",\"brand\":\"瑞幸\",\"quantity\":1,\"kind\":\"drink\",\"evidence\":\"label\",\"label_text\":\"%s\"}]}",labels[i]);
        assert(food_observations_parse(json,"2026-11-01",items,evidence,&count) && count==1);
        assert(evidence[0]==(FRIDGE_DRINK|(i==2?FRIDGE_LABEL:FRIDGE_UNCLEAR)));
        assert(!strcmp(items[0].name,i==2?"瑞幸黑咖啡":"饮品"));
    }
    assert(food_observations_parse("{\"items\":[{\"category\":\"夹心饼干\",\"brand\":\"奥利奥\",\"quantity\":1,\"kind\":\"food\",\"evidence\":\"label\",\"label_text\":\"奥利奥夹心饼干\"}]}","2026-11-01",items,evidence,&count));
    assert(count==1 && !strcmp(items[0].name,"奥利奥饼干") && !items[0].product_id);
    assert(!food_observations_parse("{\"items\":[{\"category\":\"苹果\",\"quantity\":1}]}","2026-11-01",items,evidence,&count) && !count);
    puts("PASS: conservative drink label evidence and canonical snack observations");
    if (argc > 1) {
        FILE *file = fopen(argv[1],"rb"); assert(file);
        fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
        unsigned char *wav=malloc(size), *copy=malloc(size); assert(wav && copy);
        assert(fread(wav,1,size,file)==(size_t)size); fclose(file); memcpy(copy,wav,size);
        size_t pcm=audio_pcm_decode(wav,size); assert(pcm > 1000 && pcm < (size_t)size);
        assert(!audio_pcm_decode(copy,43));
        memcpy(wav,copy,size); wav[22]=2; assert(!audio_pcm_decode(wav,size));
        memcpy(wav,copy,size); wav[40]=100;wav[41]=wav[42]=wav[43]=0; assert(audio_pcm_decode(wav,60)==0);
        printf("PASS: actual Qwen streaming WAV -> %zu bytes PCM16/16kHz; malformed/truncated audio rejected\n",pcm);
        free(wav);free(copy);
    }
    return 0;
}
