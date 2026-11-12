#include "app_photo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
struct app_photo {
    esp_gsp_handle_t ui;
    photo_provider_t provider;
    photo_state_t state;
    void *timer;
    bool waiting;
    esp_gsp_err_t error;
};
static void check(struct app_photo *a, esp_gsp_err_t e) {
    if (e)
        a->error = e;
}
static void paint(struct app_photo *a) {
    bool live =
        a->state.mode == PHOTO_LIVE || a->state.mode == PHOTO_LOADING || a->state.mode == PHOTO_CAMERA_ERROR;
    const char *status = "正在启动摄像头";
    switch (a->state.mode) {
    case PHOTO_LIVE:
        status = "实时取景 / 点击拍照";
        break;
    case PHOTO_REVIEW:
        status = "黑白打印预览 / 确认后出纸";
        break;
    case PHOTO_SENDING:
        status = "正在发送 / 请保持打印机开机";
        break;
    case PHOTO_SENT:
        status = "已发送 / 请检查出纸";
        break;
    case PHOTO_FAILED:
        status = "发送失败 / 检查出纸后再重试";
        break;
    case PHOTO_CAMERA_ERROR:
        status = "摄像头不可用 / 请检查左侧模块";
        break;
    default:
        break;
    }
    if (a->waiting)
        status = "正在处理";
    check(a, gsp_photo_status_set_text(a->ui, status));
    check(a, gsp_photo_title_set_text(a->ui, live ? "拍照打印" : "确认照片"));
    check(a,
          gsp_photo_connection_set_text(a->ui, a->state.connected ? "RPP02N / 已连接" : "RPP02N / 连接中"));
    check(a, gsp_photo_live_controls_set_visible(a->ui, live));
    check(a, gsp_photo_review_controls_set_visible(a->ui, !live));
    check(a, gsp_photo_print_text_set_text(a->ui, a->state.mode == PHOTO_SENDING ? "正在打印"
                                                  : a->state.mode == PHOTO_SENT  ? "再打印一张"
                                                                                 : "确认打印"));
}
static void tick(esp_gsp_handle_t ui, void *ctx) {
    (void)ui;
    struct app_photo *a = ctx;
    photo_state_t next = {0};
    if (a->provider.poll(a->provider.ctx, &next)) {
        if (next.response != a->state.response)
            a->waiting = false;
        if (memcmp(&next, &a->state, sizeof(next))) {
            a->state = next;
            paint(a);
        }
    }
}
static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *e, void *ctx) {
    (void)ui;
    struct app_photo *a = ctx;
    if (a->waiting || a->state.mode == PHOTO_SENDING)
        return;
    photo_command_t cmd;
    if (gsp_photo_event_is_capture(e) && a->state.mode == PHOTO_LIVE)
        cmd = PHOTO_CAPTURE;
    else if (gsp_photo_event_is_retake(e) && a->state.mode != PHOTO_LIVE)
        cmd = PHOTO_RETAKE;
    else if (gsp_photo_event_is_print(e) && a->state.connected &&
             (a->state.mode == PHOTO_REVIEW || a->state.mode == PHOTO_SENT || a->state.mode == PHOTO_FAILED))
        cmd = PHOTO_PRINT;
    else
        return;
    if (a->provider.command(a->provider.ctx, cmd)) {
        a->waiting = true;
        paint(a);
    }
}
esp_gsp_err_t app_photo_create(esp_gsp_handle_t ui, const photo_provider_t *p, app_photo_handle_t *out) {
    if (!ui || !p || !p->poll || !p->command || !out)
        return ESP_GSP_ERR_INVALID_ARG;
    *out = NULL;
    struct app_photo *a = calloc(1, sizeof(*a));
    if (!a)
        return ESP_GSP_ERR_NO_MEM;
    a->ui = ui;
    a->provider = *p;
    paint(a);
    check(a, esp_gsp_on_event(ui, event, a));
    a->timer = esp_gsp_timer_create(ui, 100, tick, a);
    if (!a->timer || a->error) {
        esp_gsp_err_t err = a->error ? a->error : ESP_GSP_ERR_NO_MEM;
        app_photo_destroy(&a);
        return err;
    }
    *out = a;
    return ESP_GSP_OK;
}
esp_gsp_err_t app_photo_present(app_photo_handle_t a, const uint16_t *p, esp_gsp_canvas_release_cb_t release,
                                void *ctx) {
    return esp_gsp_canvas_try_push(a->ui, GSP_PHOTO_BIND_PREVIEW, p, PHOTO_SIDE * 2, release, ctx);
}
esp_gsp_err_t app_photo_destroy(app_photo_handle_t *p) {
    if (!p || !*p)
        return ESP_GSP_OK;
    struct app_photo *a = *p;
    esp_gsp_err_t e = esp_gsp_on_event(a->ui, NULL, NULL);
    if (e)
        return e;
    if (a->timer) {
        e = esp_gsp_timer_delete(a->ui, a->timer);
        if (e)
            return e;
    }
    e = esp_gsp_canvas_stop(a->ui, GSP_PHOTO_BIND_PREVIEW);
    if (e)
        return e;
    free(a);
    *p = NULL;
    return ESP_GSP_OK;
}
