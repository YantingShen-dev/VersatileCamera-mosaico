#include "app_voice.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include "bsp/esp_mosaico.h"
#include "esp_aec.h"
#include "esp_vad.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

struct app_voice {
    app_voice_callbacks_t callbacks{};
    std::atomic<app_cloud_handle_t> cloud{nullptr};
    SemaphoreHandle_t lock=nullptr, ref_lock=nullptr;
    esp_codec_dev_handle_t speaker=nullptr, microphone=nullptr;
    esp_tts_handle_t tts=nullptr;
    esp_tts_voice_t *voice=nullptr;
    esp_partition_mmap_handle_t mapping=0;
    aec_handle_t *aec=nullptr;
    vad_handle_t vad=nullptr;
    std::atomic<uint32_t> epoch{0}, played{0}, first_ms{0};
    std::atomic<bool> speaking{false}, mic_ready{false};
    std::atomic<int> applied_volume{-1};
    unsigned page=0;
    uint32_t operation=0, session=0, capture=0;
    bool paused=false, correctable=false, prompt=false;
    int64_t interrupted_at=0;
    char phrase[192]{};
    char notice[192]{};
    fridge_command_t command{};
    int16_t reference[4096]{};
    size_t ref_read=0, ref_write=0, ref_count=0;
    int16_t *input=nullptr, *ref=nullptr, *clean=nullptr, *vad_frame=nullptr, *output=nullptr;
    int chunk=0;

    void volume() {
        const int wanted=callbacks.volume(callbacks.ctx);
        if (wanted != applied_volume.load() && esp_codec_dev_set_out_vol(speaker,wanted)==ESP_CODEC_DEV_OK &&
            esp_codec_dev_set_out_mute(speaker,wanted==0)==ESP_CODEC_DEV_OK) applied_volume.store(wanted);
    }
    void push_reference(const int16_t *pcm,size_t n) {
        xSemaphoreTake(ref_lock,portMAX_DELAY);
        for(size_t i=0;i<n;++i) {
            if(ref_count==4096) {ref_read=(ref_read+1)%4096; --ref_count;}
            reference[ref_write]=pcm[i]; ref_write=(ref_write+1)%4096; ++ref_count;
        }
        xSemaphoreGive(ref_lock);
    }
    void pull_reference() {
        xSemaphoreTake(ref_lock,portMAX_DELAY);
        for(int i=0;i<chunk;++i) {
            ref[i]=ref_count ? reference[ref_read] : 0;
            if(ref_count) {ref_read=(ref_read+1)%4096; --ref_count;}
        }
        xSemaphoreGive(ref_lock);
    }
    bool say(uint32_t token) {
        const int64_t start=esp_timer_get_time();
        speaking.store(true);
        bool complete=esp_tts_parse_chinese(tts,phrase);
        bool first=true;
        while(complete && token==epoch.load()) {
            int samples=0;
            short *pcm=esp_tts_stream_play(tts,&samples,4);
            if(samples<=0) break;
            for(int offset=0;offset<samples && token==epoch.load();offset+=320) {
                const size_t n=std::min(320,samples-offset);
                memcpy(output,pcm+offset,n*2);
                volume(); push_reference(output,n);
                if(esp_codec_dev_write(speaker,output,n*2)!=ESP_CODEC_DEV_OK) {complete=false;break;}
                played.fetch_add(n*2);
                if(first) {first_ms.store((esp_timer_get_time()-start)/1000);first=false;}
            }
        }
        esp_tts_stream_reset(tts); speaking.store(false);
        return complete && token==epoch.load();
    }
    void speech_run() {
        while(true) {
            volume();
            xSemaphoreTake(lock,portMAX_DELAY);
            const uint32_t token=epoch.load();
            const bool available=prompt;
            if(prompt) {snprintf(phrase,sizeof(phrase),"%s",notice);prompt=false;}
            xSemaphoreGive(lock);
            if(!available) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
            say(token);
        }
    }
    void mic_run() {
        size_t filled=0; bool previous=false;
        mic_ready.store(true);
        while(true) {
            if(esp_codec_dev_read(microphone,input,chunk*2)!=ESP_CODEC_DEV_OK) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
            pull_reference(); aec_process(aec,input,ref,clean);
            app_cloud_feed_audio(cloud.load(),clean,chunk);
            for(int i=0;i<chunk;++i) {
                vad_frame[filled++]=clean[i];
                if(filled!=480) continue;
                filled=0;
                bool speech=vad_process_with_trigger(vad,vad_frame)==VAD_SPEECH;
                xSemaphoreTake(lock,portMAX_DELAY);
                if(speech && !previous && page==1 && correctable && !paused) {
                    paused=true; interrupted_at=esp_timer_get_time(); epoch.fetch_add(1);
                    memset(&command,0,sizeof(command));command.action=FRIDGE_INTERRUPT;
                    command.session=session;command.capture=capture;
                    callbacks.deliver(callbacks.ctx,&command);
                }
                // A false VAD or failed ASR must not leave the shutter disabled forever.
                if(paused && interrupted_at && esp_timer_get_time()-interrupted_at>12000000) {
                    memset(&command,0,sizeof(command));command.action=FRIDGE_RESUME;
                    command.session=session;command.capture=capture;
                    callbacks.deliver(callbacks.ctx,&command); interrupted_at=0;
                }
                xSemaphoreGive(lock);previous=speech;
            }
        }
    }
};
extern "C" esp_err_t app_voice_start(app_voice_handle_t *out,const app_voice_callbacks_t *cb) {
    if(!out || !cb || !cb->deliver || !cb->volume) return ESP_ERR_INVALID_ARG;
    *out=nullptr;
    auto *s=new(std::nothrow) app_voice(); if(!s) return ESP_ERR_NO_MEM;
    s->callbacks=*cb; s->lock=xSemaphoreCreateMutex();s->ref_lock=xSemaphoreCreateMutex();
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"voice_data");
    const void *data=nullptr;
    esp_err_t err=ESP_ERR_NO_MEM;
    if(!s->lock || !s->ref_lock) goto fail;
    if(!partition || esp_partition_mmap(partition,0,partition->size,ESP_PARTITION_MMAP_DATA,&data,&s->mapping)!=ESP_OK) goto fail;
    s->voice=esp_tts_voice_set_init(&esp_tts_voice_template,const_cast<void *>(data));
    if(!s->voice || !(s->tts=esp_tts_create(s->voice))) goto fail;
    s->speaker=bsp_audio_codec_speaker_init();s->microphone=bsp_audio_codec_microphone_init();
    if(!s->speaker || !s->microphone) goto fail;
    {
        esp_codec_dev_sample_info_t format{};format.sample_rate=16000;format.bits_per_sample=16;format.channel=1;
        if(esp_codec_dev_open(s->speaker,&format)!=ESP_CODEC_DEV_OK || esp_codec_dev_open(s->microphone,&format)!=ESP_CODEC_DEV_OK) goto fail;
        esp_codec_dev_set_in_gain(s->microphone,24);
        aec_config_t config{};config.mic_num=config.ref_num=config.out_num=1;
        config.filter_length=4;config.sample_rate=16000;config.mode=AEC_MODE_FD_LOW_COST;
        config.nlp_level=AEC_NLP_LEVEL_AGGR;config.caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
        s->aec=aec_create_from_config(&config);
    }
    if(!s->aec) goto fail;
    s->chunk=aec_get_chunksize(s->aec);
    s->input=(int16_t*)heap_caps_aligned_alloc(16,s->chunk*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->ref=(int16_t*)heap_caps_aligned_alloc(16,s->chunk*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->clean=(int16_t*)heap_caps_aligned_alloc(16,s->chunk*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->vad_frame=(int16_t*)heap_caps_aligned_alloc(16,960,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s->output=(int16_t*)heap_caps_aligned_alloc(16,640,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->vad=vad_create_with_param(VAD_MODE_3,16000,30,120,300);
    if(!s->input || !s->ref || !s->clean || !s->vad_frame || !s->output || !s->vad) goto fail;
    {
        TaskHandle_t speech_task=nullptr;
        if(xTaskCreateWithCaps([](void *p){((app_voice*)p)->speech_run();},"fridge_tts",8192,s,4,&speech_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) goto fail;
        if(xTaskCreateWithCaps([](void *p){((app_voice*)p)->mic_run();},"fridge_mic",6144,s,5,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {vTaskDeleteWithCaps(speech_task);goto fail;}
    }
    *out=s; ESP_LOGI("fridge_voice","Local Xiaole TTS and AEC ready, frame=%d",s->chunk);return ESP_OK;
fail:
    if(s->speaker) esp_codec_dev_close(s->speaker);
    if(s->microphone) esp_codec_dev_close(s->microphone);
    if(s->tts) esp_tts_destroy(s->tts);
    if(s->voice) esp_tts_voice_set_free(s->voice);
    if(s->mapping) esp_partition_munmap(s->mapping);
    if(s->aec) aec_destroy(s->aec);
    if(s->vad) vad_destroy(s->vad);
    heap_caps_free(s->input);heap_caps_free(s->ref);heap_caps_free(s->clean);heap_caps_free(s->vad_frame);heap_caps_free(s->output);
    if(s->lock) vSemaphoreDelete(s->lock);
    if(s->ref_lock) vSemaphoreDelete(s->ref_lock);
    delete s;return err;
}
extern "C" void app_voice_connect(app_voice_handle_t s,app_cloud_handle_t cloud) {if(s) s->cloud.store(cloud);}
extern "C" void app_voice_publish(app_voice_handle_t s,const fridge_snapshot_t *state) {
    if(!s) return;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    bool entered=state->page==1 && state->operation!=s->operation;
    if(state->capture!=s->capture || (state->page!=1 && state->page!=2)) {
        s->epoch.fetch_add(1);s->prompt=false;s->paused=false;s->interrupted_at=0;
    }
    if(entered) {
        s->epoch.fetch_add(1);s->prompt=true;
        snprintf(s->notice,sizeof(s->notice),"%s",state->store.direction==1?"买了些什么？":"想取出些什么？");
    }
    // Results, corrections, undo and recognition errors are visual-only.
    if(state->interrupted!=s->paused) {s->epoch.fetch_add(1);s->paused=state->interrupted;s->interrupted_at=state->interrupted?esp_timer_get_time():0;}
    s->page=state->page;s->operation=state->operation;s->session=state->store.session;s->capture=state->capture;
    s->correctable=state->store.undo_available;
    xSemaphoreGive(s->lock);
    app_cloud_voice_state(s->cloud.load(),state);
}
extern "C" void app_voice_test(app_voice_handle_t s) {
    if(!s) return;
    xSemaphoreTake(s->lock,portMAX_DELAY);s->epoch.fetch_add(1);s->prompt=true;
    snprintf(s->notice,sizeof(s->notice),"声音测试，现在是本地语音播报。");xSemaphoreGive(s->lock);
}
extern "C" void app_voice_diagnostics(app_voice_handle_t s,cJSON *result) {
    cJSON_AddStringToObject(result,"tts_provider","local-xiaole");
    cJSON_AddBoolToObject(result,"speaker_ready",s && s->tts);
    cJSON_AddBoolToObject(result,"mic_ready",s && s->mic_ready.load());
    cJSON_AddBoolToObject(result,"aec_ready",s && s->aec);
    if(!s)return;
    cJSON_AddNumberToObject(result,"volume_applied",s->applied_volume.load());
    cJSON_AddNumberToObject(result,"result_audio_played",s->played.load());
    cJSON_AddNumberToObject(result,"tts_first_audio_ms",s->first_ms.load());
    cJSON_AddBoolToObject(result,"tts_speaking",s->speaking.load());
}
