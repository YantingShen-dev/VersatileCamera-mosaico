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
    esp_gsp_err_t error;
};
static void check(struct app_photo *a, esp_gsp_err_t e) {
    if (e)
        a->error = e;
}
static void paint(struct app_photo *a) {
    const photo_state_t *s = &a->state;
    const char *status = "启动摄像头", *one = "1 拍照", *two = "2 滤镜", *title = "CCD SNAP";
    switch (s->mode) {
    case PHOTO_LIVE:
        status = s->voice_ready ? "取景 / 说“准备拍照”" : "取景 / 语音准备中，可按键拍照";
        break;
    case PHOTO_FILTER:
        status = "滤镜 / 实时预览";
        one = "1 上一项";
        two = "2 下一项";
        title = "FILTER";
        break;
    case PHOTO_COUNTDOWN:
        status = "准备拍照 / 五秒倒计时";
        one = "1 立即拍照";
        two = "2 取消";
        break;
    case PHOTO_CAPTURING:
        status = "正在拍照";
        one = "正在处理";
        two = "--";
        break;
    case PHOTO_REVIEW:
        status = "检查照片 / 确认后出纸";
        one = "1 打印";
        two = "2 重拍";
        title = "PHOTO CHECK";
        break;
    case PHOTO_SENDING:
        status = "正在发送 / 请保持打印机开机";
        one = "正在打印";
        two = "--";
        title = "PHOTO CHECK";
        break;
    case PHOTO_SENT:
        status = "已发送 / 请检查出纸";
        one = "1 再打印";
        two = "2 重拍";
        title = "PHOTO CHECK";
        break;
    case PHOTO_FAILED:
        status = "发送失败 / 检查出纸后再试";
        one = "1 再试";
        two = "2 重拍";
        title = "PHOTO CHECK";
        break;
    case PHOTO_CAMERA_ERROR:
        status = "摄像头不可用 / 检查左侧模块";
        break;
    default:
        break;
    }
    char heading[96];
    snprintf(heading, sizeof(heading), "%s / %s", title, photo_filter_name(s->filter));
    check(a, gsp_photo_title_set_text(a->ui, heading));
    check(a, gsp_photo_connection_set_text(a->ui, s->connected ? "BT 已连接" : "BT 未连接"));
    check(a, gsp_photo_status_set_text(a->ui, status));
    check(a, gsp_photo_button1_text_set_text(a->ui, one));
    check(a, gsp_photo_button2_text_set_text(a->ui, two));
    check(a, gsp_photo_filter_box_set_visible(a->ui, s->mode == PHOTO_FILTER));
    check(a, gsp_photo_countdown_box_set_visible(a->ui, s->mode == PHOTO_COUNTDOWN));
    check(a, gsp_photo_focus_set_visible(a->ui, s->mode == PHOTO_LIVE));
    char text[96];
    snprintf(text, sizeof(text), "%u / 5  %s  /  384px  1:1", (unsigned)s->filter + 1,
             photo_filter_name(s->filter));
    check(a, gsp_photo_filter_name_set_text(a->ui, text));
    snprintf(text, sizeof(text), "%u", s->countdown);
    check(a, gsp_photo_countdown_set_text(a->ui, text));
}
static void tick(esp_gsp_handle_t ui, void *ctx) {
    (void)ui;
    struct app_photo *a = ctx;
    photo_state_t next = {0};
    if (a->provider.poll(a->provider.ctx, &next) && memcmp(&next, &a->state, sizeof(next))) {
        a->state = next;
        paint(a);
    }
}
static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *e, void *ctx) {
    (void)ui;
    struct app_photo *a = ctx;
    if (gsp_photo_event_is_button1(e))
        a->provider.command(a->provider.ctx, PHOTO_BUTTON1);
    else if (gsp_photo_event_is_button2(e))
        a->provider.command(a->provider.ctx, PHOTO_BUTTON2);
}
esp_gsp_err_t app_photo_create(esp_gsp_handle_t ui, const photo_provider_t *p,
                               app_photo_handle_t *out) {
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
    a->timer = esp_gsp_timer_create(ui, 50, tick, a);
    if (!a->timer || a->error) {
        esp_gsp_err_t err = a->error ? a->error : ESP_GSP_ERR_NO_MEM;
        app_photo_destroy(&a);
        return err;
    }
    *out = a;
    return ESP_GSP_OK;
}
esp_gsp_err_t app_photo_present(app_photo_handle_t a, const uint16_t *p,
                                esp_gsp_canvas_release_cb_t release, void *ctx) {
    return esp_gsp_canvas_try_push(a->ui, GSP_PHOTO_BIND_PREVIEW, p, PHOTO_SIDE * 2, release, ctx);
}
esp_gsp_err_t app_photo_destroy(app_photo_handle_t *p) {
    if (!p || !*p)
        return ESP_GSP_OK;
    struct app_photo *a = *p;
    esp_gsp_err_t e = esp_gsp_on_event(a->ui, NULL, NULL);
    if (e)
        return e;
    if (a->timer && (e = esp_gsp_timer_delete(a->ui, a->timer)))
        return e;
    e = esp_gsp_canvas_stop(a->ui, GSP_PHOTO_BIND_PREVIEW);
    if (e)
        return e;
    free(a);
    *p = NULL;
    return ESP_GSP_OK;
}
