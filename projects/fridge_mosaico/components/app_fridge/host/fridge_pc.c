// SPDX-License-Identifier: Apache-2.0
#include "app_fridge.h"
#include "gsp_sim_bridge.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static struct {
    app_fridge_handle_t app;
    char directory[512];
    char line[256];
    uint16_t *frame;
    bool frame_busy;
    fridge_settings_t settings;
    unsigned sound_test;
} host;

static void path(char *out, size_t length, const char *name)
{ snprintf(out, length, "%s/%s", host.directory, name); }

static bool replace(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static uint32_t checksum(const void *data, size_t size)
{
    const unsigned char *p = data; uint32_t hash = 2166136261u;
    while (size--) { hash = (hash ^ *p++) * 16777619u; }
    return hash;
}

static bool save(void *ctx, const fridge_store_t *store)
{
    (void)ctx;
    char *tmp = calloc(1024, 1), *dest = calloc(1024, 1);
    if (!tmp || !dest) { free(tmp); free(dest); return false; }
    path(tmp, 1024, "inventory.tmp"); path(dest, 1024, "inventory.bin");
    FILE *file = fopen(tmp, "wb"); bool ok = false;
    uint32_t hash = checksum(store, sizeof(*store));
    if (file) {
        ok = fwrite(&hash, sizeof(hash), 1, file) == 1 && fwrite(store, sizeof(*store), 1, file) == 1;
        if (fflush(file)) { ok = false; }
#ifdef _WIN32
        if (_commit(_fileno(file))) { ok = false; }
#else
        if (fsync(fileno(file))) { ok = false; }
#endif
        if (fclose(file)) { ok = false; }
        if (ok) { ok = replace(tmp, dest); }
    }
    free(tmp); free(dest); return ok;
}

static bool load(void *ctx, fridge_store_t *store)
{
    (void)ctx; char *name = calloc(1024, 1);
    if (!name) { return false; }
    path(name, 1024, "inventory.bin");
    FILE *file = fopen(name, "rb"); free(name);
    if (!file) { return errno == ENOENT; }
    if (fseek(file,0,SEEK_END)) {fclose(file);return false;}
    long length=ftell(file);rewind(file);
    if(length<4 || length>(long)sizeof(*store)+4) {fclose(file);return false;}
    size_t size=(size_t)length-4;void *data=malloc(size);uint32_t hash=0;
    bool ok=data && fread(&hash,4,1,file)==1 && fread(data,size,1,file)==1;
    fclose(file);
    ok=ok && hash==checksum(data,size) && fridge_store_decode(store,data,size);
    free(data);return ok;
}

static void quoted(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', file); fputc(*p, file); }
        else if (*p < 32) { fprintf(file, "\\u%04x", *p); }
        else { fputc(*p, file); }
    }
    fputc('"', file);
}

static void items_json(FILE *file, const fridge_item_t *items, unsigned count)
{
    fputc('[', file);
    for (unsigned i = 0; i < count; ++i) {
        if (i) { fputc(',', file); }
        fputs("{\"name\":", file); quoted(file, items[i].name);
        fprintf(file, ",\"quantity\":%.3f,\"date\":", items[i].quantity / 1000.0);
        quoted(file, items[i].date);
        fprintf(file,",\"product_id\":%u,\"category\":",items[i].product_id);quoted(file,items[i].category);
        fputs(",\"brand\":",file);quoted(file,items[i].brand);fputc('}',file);
    }
    fputc(']', file);
}

static void publish(void *ctx, const fridge_snapshot_t *s)
{
    (void)ctx;
    char *tmp = calloc(1024, 1), *dest = calloc(1024, 1);
    if (!tmp || !dest) { free(tmp); free(dest); return; }
    path(tmp, 1024, "state.tmp"); path(dest, 1024, "state.json");
    FILE *file = fopen(tmp, "wb");
    if (file) {
        fprintf(file, "{\"ack\":%u,\"revision\":%u,\"session\":%u,\"capture\":%u,\"page\":%u,\"direction\":%u,\"interrupted\":%s,\"undo\":%s,\"message\":",
                s->ack, s->store.revision, s->store.session, s->capture, s->page,
                s->store.direction, s->interrupted ? "true" : "false", s->store.undo_available ? "true" : "false");
        quoted(file, s->message); fputs(",\"items\":", file); items_json(file, s->store.items, s->store.count);
        fprintf(file, ",\"operation\":%u,\"feedback_id\":%u,\"feedback\":%u", s->operation, s->feedback_id, s->feedback);
        fprintf(file, ",\"volume\":%u,\"sound_test\":%u", host.settings.volume, host.sound_test);
        fprintf(file,",\"pending\":%s,\"choice_count\":%u,\"choice_offset\":%u,\"choices\":[",s->pending?"true":"false",s->choice_count,s->choice_offset);
        for(unsigned i=0;i<3 && s->choice_offset+i<s->choice_count;++i) {
            if(i)fputc(',',file);fprintf(file,"{\"id\":%u,\"name\":",s->choice_ids[i]);quoted(file,s->choice_names[i]);fputc('}',file);
        }
        fputc(']',file);
        fputs(",\"batch\":", file); items_json(file, s->store.batch, s->store.batch_count); fputs("}\n", file);
        bool ok = !ferror(file); if (fclose(file)) { ok = false; }
        if (ok) { replace(tmp, dest); }
    }
    free(tmp); free(dest);
}

static void release_frame(void *ctx) { (void)ctx; host.frame_busy = false; }

static bool poll(void *ctx, fridge_command_t *command)
{
    (void)ctx;
    char *name = calloc(1024, 1);
    if (!name) { return false; }
    if (!host.frame_busy && host.frame) {
        path(name, 1024, "preview.rgb565");
        FILE *frame = fopen(name, "rb");
        if (frame) {
            size_t got = fread(host.frame, 2, FRIDGE_PREVIEW_WIDTH * FRIDGE_PREVIEW_HEIGHT, frame); fclose(frame); remove(name);
            if (got == FRIDGE_PREVIEW_WIDTH * FRIDGE_PREVIEW_HEIGHT) {
                host.frame_busy = true;
                if (app_fridge_present(host.app, host.frame, release_frame, NULL) != ESP_GSP_OK) { host.frame_busy = false; }
            }
        }
    }
    path(name, 1024, "command.txt");
    FILE *file = fopen(name, "rb");
    if (!file) { free(name); return false; }
    memset(command, 0, sizeof(*command)); unsigned action = 0;
    bool ok = fgets(host.line, sizeof(host.line), file) &&
              sscanf(host.line, "%u %u %u %u %d", &command->id, &action,
                     &command->session, &command->capture, &command->value) == 5;
    command->action = (fridge_action_t)action;
    if (ok && (action == FRIDGE_APPLY || action == FRIDGE_OBSERVE)) {
        if (command->value < 1 || command->value > FRIDGE_BATCH_MAX) { ok = false; }
        for (int i = 0; ok && i < command->value; ++i) {
            if (!fgets(host.line, sizeof(host.line), file)) { ok = false; break; }
            host.line[strcspn(host.line,"\r\n")]=0;
            char *fields[6]={host.line};unsigned n=1;
            for(char *p=host.line;*p && n<6;++p)if(*p=='\t'){*p=0;fields[n++]=p+1;}
            if(n!=(action==FRIDGE_OBSERVE?6u:3u) || strlen(fields[0])>=FRIDGE_NAME_BYTES || strlen(fields[2])!=10) {ok=false;break;}
            snprintf(command->items[i].name,FRIDGE_NAME_BYTES,"%s",fields[0]);
            command->items[i].quantity=(int32_t)strtol(fields[1],NULL,10);
            snprintf(command->items[i].date,11,"%s",fields[2]);
            if(action==FRIDGE_OBSERVE) {
                if(strlen(fields[3])>=24 || strlen(fields[4])>=20) {ok=false;break;}
                snprintf(command->items[i].category,24,"%s",fields[3]);
                snprintf(command->items[i].brand,20,"%s",fields[4]);command->evidence[i]=(uint8_t)strtoul(fields[5],NULL,10);
            }
        }
    } else if (ok) {
        if (fgets(command->text, sizeof(command->text), file)) { command->text[strcspn(command->text, "\r\n")] = 0; }
        if (fgets(command->date, sizeof(command->date), file)) { command->date[strcspn(command->date, "\r\n")] = 0; }
    }
    fclose(file); remove(name); free(name); return ok;
}

static void settings_read(void *ctx, fridge_settings_t *out) { (void)ctx; *out = host.settings; }
static bool settings_request(void *ctx, const fridge_setting_request_t *request)
{
    (void)ctx;
    if (request->action == FRIDGE_CFG_TEST) { ++host.sound_test; return true; }
    if (request->action == FRIDGE_CFG_VOLUME) {
        if (request->volume > 100) return false;
        host.settings.volume = request->volume;
        char *name = calloc(1024, 1);
        if (!name) return false;
        path(name, 1024, "volume.txt"); FILE *file = fopen(name, "wb"); free(name);
        if (!file) return false;
        bool ok = fprintf(file, "%u\n", request->volume) > 0;
        if (fclose(file)) ok = false;
        ++host.settings.revision; return ok;
    }
    if (getenv("FRIDGE_SIM_TEST_NETWORKS") && request->action == FRIDGE_CFG_SCAN) {
        host.settings.count = 5;
        for (unsigned i = 0; i < 5; ++i) {
            snprintf(host.settings.aps[i].ssid, sizeof(host.settings.aps[i].ssid), "Test WiFi %u", i + 1);
            host.settings.aps[i].secured = i != 4;
        }
        snprintf(host.settings.status, sizeof(host.settings.status), "测试网络，仅用于模拟器验证");
    } else if (getenv("FRIDGE_SIM_TEST_NETWORKS") && request->action == FRIDGE_CFG_CONNECT) {
        snprintf(host.settings.status, sizeof(host.settings.status), "测试连接失败，请重试");
    } else snprintf(host.settings.status, sizeof(host.settings.status), "模拟器使用电脑网络，请在设备上设置");
    ++host.settings.revision; return true;
}

esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    const char *directory = getenv("FRIDGE_DATA_DIR");
    snprintf(host.directory, sizeof(host.directory), "%s", directory ? directory : ".mosaico-state/fridge");
    host.frame = calloc(FRIDGE_PREVIEW_WIDTH * FRIDGE_PREVIEW_HEIGHT, 2);
    host.settings.volume = 45; host.settings.revision = 1;
    snprintf(host.settings.status, sizeof(host.settings.status), "模拟器使用电脑网络，请在设备上设置");
    char *name = calloc(1024, 1);
    if (name) {
        path(name, 1024, "volume.txt"); FILE *file = fopen(name, "rb"); free(name);
        if (file) { unsigned value = 45; if (fscanf(file, "%u", &value) == 1 && value <= 100) host.settings.volume = value; fclose(file); }
    }
    fridge_platform_t platform = {.load=load, .save=save, .poll=poll, .publish=publish,
        .settings_read=settings_read, .settings_request=settings_request};
    return app_fridge_create(ui, &platform, &host.app);
}

void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    if (app_fridge_destroy(&host.app) == ESP_GSP_OK) { free(host.frame); host.frame = NULL; }
}
