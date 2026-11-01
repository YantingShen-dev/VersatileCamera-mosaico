#include "cloud_asr.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <new>
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

struct cloud_asr {
    app_cloud_callbacks_t callbacks{};
    asr_transcript_cb transcript=nullptr;void *owner=nullptr;
    SemaphoreHandle_t lock=nullptr;
    esp_websocket_client_handle_t ws=nullptr;
    esp_websocket_client_config_t config{};
    std::atomic<bool> enabled{false},connected{false},ready{false},refresh{false};
    char key[512]{},headers[560]{},incoming[8192]{},encoded[2561]{},outgoing[2800]{};
    size_t received=0;
    int16_t ring[16000]{},frame[960]{};
    size_t read=0,write=0,count=0;
    uint32_t session=0,capture=0,utterance=0,utterance_capture=0;
    unsigned batch_count=0,spoken_count=0;
    uint64_t connection_samples=0,batch_start_sample=0;
    char utterance_id[100]{};
    bool correctable=false;
    fridge_item_t batch[FRIDGE_BATCH_MAX]{},spoken[FRIDGE_BATCH_MAX]{};
    fridge_command_t command{};
    uint32_t transcripts=0,sent=0,dropped=0,connections=0;
    char status[80]="idle";
    void note(const char *value) {xSemaphoreTake(lock,portMAX_DELAY);snprintf(status,sizeof(status),"%s",value);xSemaphoreGive(lock);}
    void event(esp_websocket_event_data_t *data,int32_t id) {
        if(id==WEBSOCKET_EVENT_CONNECTED) {
            xSemaphoreTake(lock,portMAX_DELAY);connection_samples=batch_start_sample=0;utterance=0;utterance_id[0]=0;xSemaphoreGive(lock);
            connected.store(true);ready.store(false);note("configuring");return;
        }
        if(id==WEBSOCKET_EVENT_DISCONNECTED || id==WEBSOCKET_EVENT_ERROR) {
            connected.store(false);ready.store(false);note("reconnecting");
            xSemaphoreTake(lock,portMAX_DELAY);utterance=0;read=write=count=0;xSemaphoreGive(lock);return;
        }
        if(id!=WEBSOCKET_EVENT_DATA || (data->op_code!=1 && data->op_code!=0)) return;
        if(data->payload_offset==0) received=0;
        if(data->payload_len<=0 || data->payload_len>=(int)sizeof(incoming) || data->payload_offset!=(int)received || data->data_len<0) {received=0;return;}
        if(received+(size_t)data->data_len>=sizeof(incoming)) {received=0;return;}
        memcpy(incoming+received,data->data_ptr,data->data_len);received+=data->data_len;
        if(received!=(size_t)data->payload_len) return;
        incoming[received]=0;
        cJSON *root=cJSON_Parse(incoming);
        const cJSON *type=cJSON_GetObjectItemCaseSensitive(root,"type");
        if(cJSON_IsString(type)) {
            if(!strcmp(type->valuestring,"session.updated")) {ready.store(true);note("listening");}
            else if(!strcmp(type->valuestring,"input_audio_buffer.speech_started")) {
                xSemaphoreTake(lock,portMAX_DELAY);
                const cJSON *item=cJSON_GetObjectItemCaseSensitive(root,"item_id");
                const cJSON *start=cJSON_GetObjectItemCaseSensitive(root,"audio_start_ms");
                bool fresh=cJSON_IsString(item) && strlen(item->valuestring)<sizeof(utterance_id) &&
                    cJSON_IsNumber(start) && start->valuedouble>=0 && start->valuedouble*16>=batch_start_sample;
                utterance=correctable && fresh?session:0;utterance_capture=capture;
                snprintf(utterance_id,sizeof(utterance_id),"%s",fresh?item->valuestring:"");
                spoken_count=batch_count;memcpy(spoken,batch,spoken_count*sizeof(fridge_item_t));
                if(utterance) {
                    memset(&command,0,sizeof(command));command.action=FRIDGE_INTERRUPT;command.session=utterance;command.capture=utterance_capture;
                    callbacks.deliver(callbacks.ctx,&command);
                }
                xSemaphoreGive(lock);
            } else if(!strcmp(type->valuestring,"conversation.item.input_audio_transcription.completed")) {
                const cJSON *text=cJSON_GetObjectItemCaseSensitive(root,"transcript");
                const cJSON *item=cJSON_GetObjectItemCaseSensitive(root,"item_id");
                xSemaphoreTake(lock,portMAX_DELAY);
                if(utterance && cJSON_IsString(item) && !strcmp(item->valuestring,utterance_id) && correctable && utterance==session && utterance_capture==capture && cJSON_IsString(text) && strlen(text->valuestring)<900) {
                    ++transcripts;
                    if(!transcript(owner,text->valuestring,utterance,utterance_capture,spoken,spoken_count)) {
                        memset(&command,0,sizeof(command));command.action=FRIDGE_RESUME;command.session=utterance;command.capture=utterance_capture;
                        callbacks.deliver(callbacks.ctx,&command);
                    }
                }
                if(cJSON_IsString(item) && !strcmp(item->valuestring,utterance_id)) utterance=0;
                xSemaphoreGive(lock);
            } else if(!strcmp(type->valuestring,"error")) {ready.store(false);note("asr_service_error");refresh.store(true);}
        }
        cJSON_Delete(root);
    }
    void run() {
        int64_t retry=0;bool configured=false;
        while(true) {
            if(ws && (!enabled.load() || refresh.exchange(false))) {
                esp_websocket_client_stop(ws);esp_websocket_client_destroy(ws);ws=nullptr;
                connected.store(false);ready.store(false);configured=false;
                xSemaphoreTake(lock,portMAX_DELAY);read=write=count=0;utterance=0;xSemaphoreGive(lock);
                memset(headers,0,sizeof(headers));retry=esp_timer_get_time()+2000000;
            }
            if(!enabled.load()) {vTaskDelay(pdMS_TO_TICKS(100));continue;}
            if(!ws && esp_timer_get_time()>=retry && time(nullptr)>1767225600) {
                xSemaphoreTake(lock,portMAX_DELAY);
                bool has_key=key[0];snprintf(headers,sizeof(headers),"Authorization: Bearer %s\r\n",key);
                xSemaphoreGive(lock);
                if(!has_key) {note("not_configured");retry=esp_timer_get_time()+3000000;continue;}
                config={};config.uri="wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen3-asr-flash-realtime";
                config.headers=headers;config.crt_bundle_attach=esp_crt_bundle_attach;
                config.buffer_size=2048;config.task_stack=6144;config.reconnect_timeout_ms=5000;config.network_timeout_ms=5000;
                ws=esp_websocket_client_init(&config);
                if(ws) {
                    esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,[](void *arg,esp_event_base_t,int32_t id,void *data){((cloud_asr*)arg)->event((esp_websocket_event_data_t*)data,id);},this);
                    if(esp_websocket_client_start(ws)!=ESP_OK) {esp_websocket_client_destroy(ws);ws=nullptr;}
                    else {xSemaphoreTake(lock,portMAX_DELAY);++connections;xSemaphoreGive(lock);}
                }
                retry=esp_timer_get_time()+5000000;configured=false;
            }
            if(!connected.load()) {configured=false;vTaskDelay(pdMS_TO_TICKS(60));continue;}
            if(!configured) {
                constexpr char update[]=R"({"event_id":"fridge_session","type":"session.update","session":{"input_audio_format":"pcm","sample_rate":16000,"input_audio_transcription":{"language":"zh"},"turn_detection":{"type":"server_vad","threshold":0.3,"silence_duration_ms":400}}})";
                configured=esp_websocket_client_send_text(ws,update,sizeof(update)-1,pdMS_TO_TICKS(1000))==(int)sizeof(update)-1;
                vTaskDelay(pdMS_TO_TICKS(20));continue;
            }
            if(!ready.load()) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
            xSemaphoreTake(lock,portMAX_DELAY);
            bool available=count>=960;
            if(available) {for(unsigned i=0;i<960;++i){frame[i]=ring[read];read=(read+1)%16000;}count-=960;connection_samples+=960;}
            xSemaphoreGive(lock);
            if(!available) {vTaskDelay(pdMS_TO_TICKS(10));continue;}
            size_t length=0;
            mbedtls_base64_encode((unsigned char*)encoded,sizeof(encoded),&length,(const unsigned char*)frame,sizeof(frame));
            encoded[length]=0;
            int bytes=snprintf(outgoing,sizeof(outgoing),"{\"event_id\":\"a%lld\",\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}",esp_timer_get_time(),encoded);
            if(esp_websocket_client_send_text(ws,outgoing,bytes,pdMS_TO_TICKS(1000))!=bytes) refresh.store(true);
            else {xSemaphoreTake(lock,portMAX_DELAY);sent+=960;xSemaphoreGive(lock);}
        }
    }
};
cloud_asr *cloud_asr_create(const app_cloud_callbacks_t &callbacks,asr_transcript_cb transcript,void *owner,const char *key) {
    auto *s=new(std::nothrow) cloud_asr();if(!s)return nullptr;
    s->callbacks=callbacks;s->transcript=transcript;s->owner=owner;s->lock=xSemaphoreCreateMutex();
    snprintf(s->key,sizeof(s->key),"%s",key);
    if(!s->lock || xTaskCreateWithCaps([](void *p){((cloud_asr*)p)->run();},"fridge_asr",6144,s,3,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        if(s->lock) vSemaphoreDelete(s->lock);
        memset(s->key,0,sizeof(s->key));delete s;return nullptr;
    }
    return s;
}
void cloud_asr_configure(cloud_asr *s,const char *key) {
    if(!s) return;
    xSemaphoreTake(s->lock,portMAX_DELAY);snprintf(s->key,sizeof(s->key),"%s",key);xSemaphoreGive(s->lock);s->refresh.store(true);
}
void cloud_asr_state(cloud_asr *s,const fridge_snapshot_t *state) {
    if(!s) return;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    s->enabled.store(state->page==1 || state->page==2);
    s->correctable=state->page==1 && state->store.undo_available;
    // ASR timestamps identify when audio was captured, not when a delayed event arrived.
    if(s->session!=state->store.session) s->batch_start_sample=s->connection_samples+s->count;
    s->session=state->store.session;s->capture=state->capture;s->batch_count=state->store.batch_count;
    memcpy(s->batch,state->store.batch,s->batch_count*sizeof(fridge_item_t));
    if(s->utterance && (s->utterance!=s->session || s->utterance_capture!=s->capture)) s->utterance=0;
    xSemaphoreGive(s->lock);
}
void cloud_asr_feed(cloud_asr *s,const int16_t *pcm,size_t samples) {
    if(!s || !s->ready.load() || !s->enabled.load())return;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    if(samples>16000-s->count) {s->dropped+=s->count;s->read=s->write=s->count=0;s->refresh.store(true);}
    else for(size_t i=0;i<samples;++i){s->ring[s->write]=pcm[i];s->write=(s->write+1)%16000;++s->count;}
    xSemaphoreGive(s->lock);
}
void cloud_asr_diagnostics(cloud_asr *s,cJSON *result) {
    cJSON_AddBoolToObject(result,"asr_ready",s && s->ready.load());if(!s)return;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    cJSON_AddStringToObject(result,"asr_status",s->status);cJSON_AddNumberToObject(result,"asr_samples_sent",s->sent);
    cJSON_AddNumberToObject(result,"asr_transcripts",s->transcripts);cJSON_AddNumberToObject(result,"asr_dropped_samples",s->dropped);
    cJSON_AddNumberToObject(result,"asr_connections",s->connections);xSemaphoreGive(s->lock);
}
