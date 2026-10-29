// SPDX-License-Identifier: Apache-2.0
#include "app_face.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"

struct app_face {
    esp_gsp_handle_t ui;
    face_provider_t provider;
    face_snapshot_t snapshot;
    void *timer;
    uint8_t selected;
    bool waiting;
    esp_gsp_err_t error;
};

static void check(struct app_face *app, esp_gsp_err_t result)
{
    if (result != ESP_GSP_OK) {
        app->error = result;
    }
}

static const char *status_text(face_status_t status)
{
    switch (status) {
    case FACE_NO_FACE: return "未检测到人脸 / 请正对镜头";
    case FACE_UNKNOWN: return "未录入的人脸 / 可录入新面孔";
    case FACE_KNOWN: return "已识别";
    case FACE_MULTIPLE: return "多人入镜 / 请单人录入";
    case FACE_TOO_SMALL: return "请靠近一些 / 保持面部清晰";
    case FACE_ENROLLED: return "录入成功";
    case FACE_DUPLICATE: return "已经录入";
    case FACE_FULL: return "已满 / 最多录入 5 人";
    case FACE_DELETED: return "已删除人员记录";
    case FACE_STORAGE_ERROR: return "存储失败 / 请重新尝试";
    case FACE_CAMERA_ERROR: return "摄像头不可用 / 请检查左侧模块";
    case FACE_MODEL_ERROR: return "模型加载失败";
    case FACE_WORKING: return "正在处理 / 请保持清晰";
    default: return "正在加载模型";
    }
}

static void paint(struct app_face *app)
{
    char text[120];
    face_status_t status = app->waiting ? FACE_WORKING : app->snapshot.status;
    if ((status == FACE_KNOWN || status == FACE_ENROLLED || status == FACE_DUPLICATE) && app->snapshot.person) {
        snprintf(text, sizeof(text), "人员 %02u / %s", app->snapshot.person, status_text(status));
    } else {
        snprintf(text, sizeof(text), "%s", status_text(status));
    }
    check(app, gsp_face_status_set_text(app->ui, text));
    unsigned count = 0;
    for (unsigned i = 0; i < FACE_MAX_PEOPLE; ++i) {
        count += (app->snapshot.slots >> i) & 1U;
    }
    snprintf(text, sizeof(text), "已录入 %u / 5 人", count);
    check(app, gsp_face_count_set_text(app->ui, text));
    if (app->snapshot.ready) {
        snprintf(text, sizeof(text), "耗时 %lu ms", (unsigned long)app->snapshot.elapsed_ms);
    } else {
        snprintf(text, sizeof(text), "耗时 -- ms");
    }
    check(app, gsp_face_timing_set_text(app->ui, text));
    esp_gsp_err_t (*const setters[])(esp_gsp_handle_t, const char *) = {
        gsp_face_person1_set_text, gsp_face_person2_set_text, gsp_face_person3_set_text,
        gsp_face_person4_set_text, gsp_face_person5_set_text
    };
    for (unsigned i = 0; i < FACE_MAX_PEOPLE; ++i) {
        snprintf(text, sizeof(text), "人员 %02u / %s", i + 1,
                 (app->snapshot.slots & (1U << i)) ? "已保存" : "暂无记录");
        check(app, setters[i](app->ui, text));
    }
    check(app, gsp_face_manage_status_set_text(app->ui,
                                               status == FACE_DELETED || status == FACE_STORAGE_ERROR || app->waiting ? status_text(status) : ""));
}

static void tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    struct app_face *app = ctx;
    face_snapshot_t next;
    if (app->provider.poll(app->provider.ctx, &next)) {
        if (next.response != app->snapshot.response) {
            app->waiting = false;
        }
        if (memcmp(&next, &app->snapshot, sizeof(next)) != 0) {
            app->snapshot = next;
            paint(app);
        }
    }
}

static void send_command(struct app_face *app, face_command_t command, uint8_t person)
{
    if (!app->waiting && app->provider.command(app->provider.ctx, command, person)) {
        app->waiting = true;
        paint(app);
    }
}

static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *evt, void *ctx)
{
    struct app_face *app = ctx;
    if (gsp_face_event_is_enroll(evt)) {
        if (app->snapshot.ready) {
            send_command(app, FACE_CMD_ENROLL, 0);
        }
    } else if (gsp_face_event_is_manage(evt)) {
        check(app, gsp_face_manager_set_visible(ui, true));
    } else if (gsp_face_event_is_back(evt)) {
        check(app, gsp_face_manager_set_visible(ui, false));
    } else if (gsp_face_event_is_cancel(evt)) {
        check(app, gsp_face_confirm_set_visible(ui, false));
    } else if (gsp_face_event_is_confirm_delete(evt)) {
        check(app, gsp_face_confirm_set_visible(ui, false));
        send_command(app, FACE_CMD_DELETE, app->selected);
    } else {
        const bool selections[] = {gsp_face_event_is_delete1(evt), gsp_face_event_is_delete2(evt),
                                   gsp_face_event_is_delete3(evt), gsp_face_event_is_delete4(evt), gsp_face_event_is_delete5(evt)
                                  };
        for (unsigned i = 0; i < FACE_MAX_PEOPLE; ++i) {
            if (selections[i] && !app->waiting && (app->snapshot.slots & (1U << i))) {
                char text[48];
                app->selected = i + 1;
                snprintf(text, sizeof(text), "确认删除人员 %02u", i + 1);
                check(app, gsp_face_confirm_text_set_text(ui, text));
                check(app, gsp_face_confirm_set_visible(ui, true));
                break;
            }
        }
    }
}

esp_gsp_err_t app_face_create(esp_gsp_handle_t ui, const face_provider_t *provider, app_face_handle_t *out)
{
    if (!out || !ui || !provider || !provider->poll || !provider->command) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    *out = NULL;
    struct app_face *app = calloc(1, sizeof(*app));
    if (!app) {
        return ESP_GSP_ERR_NO_MEM;
    }
    app->ui = ui;
    app->provider = *provider;
    paint(app);
    check(app, esp_gsp_on_event(ui, event, app));
    app->timer = esp_gsp_timer_create(ui, 150, tick, app);
    if (!app->timer || app->error != ESP_GSP_OK) {
        esp_gsp_err_t err = app->error ? app->error : ESP_GSP_ERR_NO_MEM;
        app_face_destroy(&app);
        return err;
    }
    *out = app;
    return ESP_GSP_OK;
}

esp_gsp_err_t app_face_present(app_face_handle_t app, const uint16_t *pixels,
                               esp_gsp_canvas_release_cb_t release, void *ctx)
{
    if (!app) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    return esp_gsp_canvas_try_push(app->ui, GSP_FACE_BIND_PREVIEW, pixels, FACE_PREVIEW_W * 2, release, ctx);
}

esp_gsp_err_t app_face_destroy(app_face_handle_t *handle)
{
    if (!handle || !*handle) {
        return ESP_GSP_OK;
    }
    struct app_face *app = *handle;
    esp_gsp_err_t err = esp_gsp_on_event(app->ui, NULL, NULL);
    if (err != ESP_GSP_OK) {
        return err;
    }
    if (app->timer) {
        err = esp_gsp_timer_delete(app->ui, app->timer);
        if (err != ESP_GSP_OK) {
            return err;
        }
        app->timer = NULL;
    }
    err = esp_gsp_canvas_stop(app->ui, GSP_FACE_BIND_PREVIEW);
    if (err != ESP_GSP_OK) {
        return err;
    }
    free(app);
    *handle = NULL;
    return ESP_GSP_OK;
}
