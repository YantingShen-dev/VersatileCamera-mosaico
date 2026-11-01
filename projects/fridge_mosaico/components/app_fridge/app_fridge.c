// SPDX-License-Identifier: Apache-2.0
#include "app_fridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"

struct app_fridge {
    esp_gsp_handle_t ui;
    fridge_platform_t platform;
    fridge_snapshot_t state;
    fridge_store_t rollback;
    fridge_command_t inbox;
    esp_gsp_list_t stock_list, batch_list;
    void *timer;
    bool storage_ready;
    bool rows_pending;
    bool row_damage_pending;
    esp_gsp_err_t error;
    fridge_settings_t settings;
    fridge_setting_request_t setting_request;
    unsigned network_offset, unmuted_volume;
    char password[65];
    fridge_item_t toast_items[FRIDGE_BATCH_MAX];
    unsigned toast_count, toast_next, toast_clock, toast_kind, toast_direction;
    unsigned toast_age[3], toast_alpha[3];
    bool toast_active[3];
    fridge_item_t observations[FRIDGE_BATCH_MAX], resolved[FRIDGE_BATCH_MAX];
    uint8_t evidence[FRIDGE_BATCH_MAX];
    unsigned pending_count, pending_index;
    fridge_matches_t matches;
};

static void toast_start(struct app_fridge *app);
static void toast_tick(struct app_fridge *app);

static void choices(struct app_fridge *app) {
    fridge_snapshot_t *s=&app->state;
    for(unsigned i=0;i<3;++i) {
        unsigned index=s->choice_offset+i;s->choice_names[i][0]=0;s->choice_ids[i]=0;
        if(index>=app->matches.count)continue;
        uint32_t id=app->matches.ids[index];s->choice_ids[i]=id;
        const fridge_item_t *p=fridge_product(&s->store,id);
        snprintf(s->choice_names[i],sizeof(s->choice_names[i]),"%s",p?p->name:"新增，不合并旧记录");
    }
}
static void resolve_choice(struct app_fridge *app,uint32_t id) {
    fridge_item_t *to=&app->resolved[app->pending_index];
    *to=app->observations[app->pending_index];to->product_id=id;
    const fridge_item_t *p=fridge_product(&app->state.store,id);
    if(p && (app->state.store.direction==2 || !to->brand[0])) {
        snprintf(to->name,sizeof(to->name),"%s",p->name);
        snprintf(to->category,sizeof(to->category),"%s",p->category);
        snprintf(to->brand,sizeof(to->brand),"%s",p->brand);
    }
    ++app->pending_index;
}
static bool resolve_pending(struct app_fridge *app) {
    fridge_snapshot_t *s=&app->state;s->pending=false;s->choice_count=s->choice_offset=0;
    while(app->pending_index<app->pending_count) {
        fridge_item_t *seen=&app->observations[app->pending_index];
        fridge_match(&s->store,seen,app->evidence[app->pending_index],&app->matches);
        if(app->matches.automatic) {resolve_choice(app,app->matches.ids[0]);continue;}
        s->pending=true;s->choice_count=app->matches.count;
        snprintf(s->message,sizeof(s->message),"%s%s",app->matches.count?"请点选：":"请对准标签重拍：",seen->name);
        choices(app);return false;
    }
    return true;
}

static void check(struct app_fridge *app, esp_gsp_err_t err)
{
    if (err != ESP_GSP_OK) { app->error = err; }
}

static void quantity_text(int32_t quantity, char *buffer, unsigned size)
{
    snprintf(buffer, size, "%.3f", quantity / 1000.0);
    char *end = buffer + strlen(buffer) - 1;
    while (end > buffer && *end == '0') { *end-- = 0; }
    if (*end == '.') { *end = 0; }
}

static const gsp_component_key_t toast_keys[3][8] = {
    {GSP_FRIDGE_OBJ_KEY_TOAST_0_0, GSP_FRIDGE_OBJ_KEY_TOAST_0_1, GSP_FRIDGE_OBJ_KEY_TOAST_0_2, GSP_FRIDGE_OBJ_KEY_TOAST_0_3, GSP_FRIDGE_OBJ_KEY_TOAST_0_4, GSP_FRIDGE_OBJ_KEY_TOAST_0_5, GSP_FRIDGE_OBJ_KEY_TOAST_0_6, GSP_FRIDGE_OBJ_KEY_TOAST_0_7},
    {GSP_FRIDGE_OBJ_KEY_TOAST_1_0, GSP_FRIDGE_OBJ_KEY_TOAST_1_1, GSP_FRIDGE_OBJ_KEY_TOAST_1_2, GSP_FRIDGE_OBJ_KEY_TOAST_1_3, GSP_FRIDGE_OBJ_KEY_TOAST_1_4, GSP_FRIDGE_OBJ_KEY_TOAST_1_5, GSP_FRIDGE_OBJ_KEY_TOAST_1_6, GSP_FRIDGE_OBJ_KEY_TOAST_1_7},
    {GSP_FRIDGE_OBJ_KEY_TOAST_2_0, GSP_FRIDGE_OBJ_KEY_TOAST_2_1, GSP_FRIDGE_OBJ_KEY_TOAST_2_2, GSP_FRIDGE_OBJ_KEY_TOAST_2_3, GSP_FRIDGE_OBJ_KEY_TOAST_2_4, GSP_FRIDGE_OBJ_KEY_TOAST_2_5, GSP_FRIDGE_OBJ_KEY_TOAST_2_6, GSP_FRIDGE_OBJ_KEY_TOAST_2_7}
};
static const gsp_component_key_t toast_text_keys[3][8] = {
    {GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_0, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_1, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_2, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_3, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_4, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_5, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_6, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_0_7},
    {GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_0, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_1, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_2, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_3, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_4, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_5, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_6, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_1_7},
    {GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_0, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_1, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_2, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_3, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_4, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_5, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_6, GSP_FRIDGE_OBJ_KEY_TOAST_TEXT_2_7}
};

static void toast_start(struct app_fridge *app)
{
    for (unsigned lane = 0; lane < 3; ++lane) {
        if (app->toast_active[lane])
            check(app, esp_gsp_component_set_visible(app->ui, toast_keys[lane][app->toast_alpha[lane]], false));
        app->toast_active[lane] = false;
    }
    app->toast_count = 0; app->toast_kind = app->state.feedback; app->toast_direction = app->state.store.direction;
    for (unsigned i = 0; i < app->state.store.batch_count; ++i) {
        const fridge_item_t *item = &app->state.store.batch[i];
        bool changed = app->toast_kind != 2;
        for (unsigned j = 0; !changed && j < app->rollback.batch_count; ++j)
            if (!strcmp(item->name, app->rollback.batch[j].name) &&
                (item->quantity != app->rollback.batch[j].quantity || strcmp(item->date, app->rollback.batch[j].date))) changed = true;
        if (changed) app->toast_items[app->toast_count++] = *item;
    }
    app->toast_next = 0; app->toast_clock = 400;
}

static void toast_tick(struct app_fridge *app)
{
    const bool camera = app->state.page == 1 || app->state.page == 2;
    app->toast_clock += 40;
    for (unsigned lane = 0; lane < 3; ++lane) {
        if (app->toast_active[lane]) {
            app->toast_age[lane] += 40;
            if (!camera || app->toast_age[lane] >= 1600) {
                check(app, esp_gsp_component_set_visible(app->ui, toast_keys[lane][app->toast_alpha[lane]], false));
                app->toast_active[lane] = false;
            }
        }
        if (camera && !app->toast_active[lane] && app->toast_next < app->toast_count && app->toast_clock >= 400) {
            char text[112], quantity[24];
            const fridge_item_t *item = &app->toast_items[app->toast_next++];
            quantity_text(item->quantity, quantity, sizeof(quantity));
            snprintf(text, sizeof(text), "%s%s × %s", app->toast_kind == 2 ? "改为 " : app->toast_direction == 2 ? "− " : "+ ", item->name, quantity);
            for (unsigned a = 0; a < 8; ++a)
                check(app, esp_gsp_component_set_text(app->ui, toast_text_keys[lane][a], text));
            app->toast_age[lane] = 0; app->toast_alpha[lane] = 0;
            app->toast_active[lane] = true; app->toast_clock = 0;
        }
        if (app->toast_active[lane]) {
            unsigned age = app->toast_age[lane];
            unsigned alpha = age < 200 ? age * 8 / 200 : age > 1160 ? 7 - (age - 1160) * 8 / 440 : 7;
            if (alpha != app->toast_alpha[lane])
                check(app, esp_gsp_component_set_visible(app->ui, toast_keys[lane][app->toast_alpha[lane]], false));
            app->toast_alpha[lane] = alpha;
            check(app, esp_gsp_component_set_position(app->ui, toast_keys[lane][alpha], 24,
                    286 - (int)lane * 72 - (int)age * 48 / 1600));
            check(app, esp_gsp_component_set_visible(app->ui, toast_keys[lane][alpha], true));
        }
    }
    if (!camera) app->toast_next = app->toast_count;
}

static gsp_err_t bind_item(esp_gsp_handle_t ui, esp_gsp_row_t row, const fridge_item_t *item)
{
    char quantity[24];
    quantity_text(item->quantity, quantity, sizeof(quantity));
    gsp_err_t err = gsp_fridge_stock_row_row_set_row_name_text(ui, row, item->name);
    if (!err) { err = gsp_fridge_stock_row_row_set_row_date_text(ui, row, item->date); }
    if (!err) { err = gsp_fridge_stock_row_row_set_row_quantity_text(ui, row, quantity); }
    return err;
}

static gsp_err_t stock_row(esp_gsp_handle_t ui, esp_gsp_row_t row, uint32_t index, void *ctx)
{
    struct app_fridge *app = ctx;
    if (index >= app->state.store.count) { return ESP_GSP_OK; }
    app->row_damage_pending = true;
    return bind_item(ui, row, &app->state.store.items[index]);
}

static gsp_err_t batch_row(esp_gsp_handle_t ui, esp_gsp_row_t row, uint32_t index, void *ctx)
{
    struct app_fridge *app = ctx;
    if (index >= app->state.store.batch_count) { return ESP_GSP_OK; }
    app->row_damage_pending = true;
    return bind_item(ui, row, &app->state.store.batch[index]);
}

static void paint(struct app_fridge *app, bool refresh)
{
    fridge_snapshot_t *s = &app->state;
    char count[16];
    snprintf(count, sizeof(count), "%u", s->store.count);
    check(app, gsp_fridge_count_set_text(app->ui, count));
    check(app, gsp_fridge_empty_set_visible(app->ui, !s->store.count));
    check(app, gsp_fridge_home_actions_set_visible(app->ui, s->page == 0));
    check(app, gsp_fridge_shutter_set_visible(app->ui, s->page == 1));
    check(app, gsp_fridge_operation_set_visible(app->ui, s->page == 1 || s->page == 2));
    check(app, gsp_fridge_review_set_visible(app->ui, s->page == 3));
    check(app, gsp_fridge_settings_set_visible(app->ui, s->page == 4));
    check(app, gsp_fridge_networks_set_visible(app->ui, s->page == 5));
    check(app, gsp_fridge_password_set_visible(app->ui, s->page == 6));
    check(app, gsp_fridge_review_title_set_text(app->ui, s->store.direction == 2 ? "本次出库" : "本次入库"));
    check(app, gsp_fridge_camera_status_set_text(app->ui, s->message));
    check(app, gsp_fridge_speech_set_text(app->ui, s->message));
    check(app,gsp_fridge_match_panel_set_visible(app->ui,s->pending && s->page==1));
    check(app,gsp_fridge_match_title_set_text(app->ui,s->message));
    check(app,gsp_fridge_match_slot_0_set_visible(app->ui,s->choice_offset<s->choice_count));
    check(app,gsp_fridge_match_slot_1_set_visible(app->ui,s->choice_offset+1<s->choice_count));
    check(app,gsp_fridge_match_slot_2_set_visible(app->ui,s->choice_offset+2<s->choice_count));
    check(app,gsp_fridge_match_name_0_set_text(app->ui,s->choice_names[0]));
    check(app,gsp_fridge_match_name_1_set_text(app->ui,s->choice_names[1]));
    check(app,gsp_fridge_match_name_2_set_text(app->ui,s->choice_names[2]));
    check(app,gsp_fridge_match_more_slot_set_visible(app->ui,s->choice_count>3));
    bool greeting = !strcmp(s->message, "买了些什么？") || !strcmp(s->message, "想取出些什么？");
    check(app, gsp_fridge_camera_notice_set_visible(app->ui,
          s->page == 2 || (s->page == 1 && !s->pending && s->message[0] && !greeting)));
    if (refresh) {
        /* Instanced rows render independently of authored layer visibility. */
        check(app, esp_gsp_list_set_total(app->ui, app->stock_list, 0));
        check(app, esp_gsp_list_set_total(app->ui, app->batch_list, 0));
        /* Let the renderer retire old row instances before rebuilding them. */
        app->rows_pending = true;
    }
    if (app->platform.publish) { app->platform.publish(app->platform.ctx, s); }
}

void app_fridge_dispatch(app_fridge_handle_t app, const fridge_command_t *cmd)
{
    fridge_snapshot_t *s = &app->state;
    unsigned previous_page = s->page;
    app->rollback = s->store;
    if (cmd->id) { s->ack = cmd->id; }
    bool changed = false;
    unsigned feedback = 0;
    if ((cmd->action == FRIDGE_APPLY || cmd->action == FRIDGE_CORRECT || cmd->action == FRIDGE_ERROR ||
         cmd->action == FRIDGE_INTERRUPT || cmd->action == FRIDGE_SAY || cmd->action == FRIDGE_RESUME ||
         cmd->action == FRIDGE_OBSERVE || cmd->action == FRIDGE_SELECT || cmd->action == FRIDGE_MORE) &&
        (cmd->session != s->store.session || (cmd->capture && cmd->capture != s->capture))) {
        if (app->platform.publish) app->platform.publish(app->platform.ctx, s);
        return;
    }
    s->message[0] = 0;
    if(cmd->action==FRIDGE_FINISH || cmd->action==FRIDGE_CAPTURE || cmd->action==FRIDGE_IN || cmd->action==FRIDGE_OUT) {
        s->pending=false;s->choice_count=0;app->pending_count=0;
    }
    if (!app->storage_ready) {
        snprintf(s->message, sizeof(s->message), "库存不可读取，请检查存储");
        paint(app, false); return;
    }
    switch (cmd->action) {
    case FRIDGE_IN:
    case FRIDGE_OUT:
        if (s->page == 1 && s->store.direction == (unsigned)cmd->action) {
            fridge_begin(&s->store, s->store.direction);
            s->page = 2; s->interrupted = false; ++s->capture;
            snprintf(s->message, sizeof(s->message), "正在识别食材");
        } else if (s->page == 0) {
            fridge_begin(&s->store, cmd->action);
            ++s->operation;
            s->page = 1; s->interrupted = false; changed = true;
            snprintf(s->message, sizeof(s->message), "%s", cmd->action == FRIDGE_IN ? "买了些什么？" : "想取出些什么？");
        } else if (s->page == 2) { snprintf(s->message, sizeof(s->message), "正在识别，请稍候"); }
        else { snprintf(s->message, sizeof(s->message), "请先完成或返回本次操作"); }
        break;
    case FRIDGE_CAPTURE:
        if (s->page == 1) {
            fridge_begin(&s->store, s->store.direction);
            s->page = 2; s->interrupted = false; ++s->capture;
            snprintf(s->message, sizeof(s->message), "正在识别食材");
        }
        break;
    case FRIDGE_APPLY:
        if (s->page != 2 || cmd->capture != s->capture || cmd->session != s->store.session) { break; }
        changed = fridge_apply(&s->store, cmd->session, cmd->items, (unsigned)cmd->value, s->message, sizeof(s->message));
        s->page = 1;
        if (changed) {
            feedback = 1;
        }
        break;
    case FRIDGE_OBSERVE:
        if(s->page!=2 || cmd->capture!=s->capture || cmd->value<1 || cmd->value>FRIDGE_BATCH_MAX)break;
        app->pending_count=(unsigned)cmd->value;app->pending_index=0;
        memcpy(app->observations,cmd->items,app->pending_count*sizeof(fridge_item_t));
        memcpy(app->evidence,cmd->evidence,app->pending_count);
        for(unsigned i=0;i<app->pending_count;++i) {
            if(!fridge_item_valid(&app->observations[i]) || app->observations[i].product_id ||
               (app->evidence[i]&3)>FRIDGE_LABEL || app->evidence[i]>6) {
                app->pending_count=0;s->page=1;snprintf(s->message,sizeof(s->message),"识别结果无效，请重拍");break;
            }
            fridge_identity_infer(&app->observations[i]);
            if(app->observations[i].category[0])snprintf(app->observations[i].name,FRIDGE_NAME_BYTES,"%s%s",app->observations[i].brand,app->observations[i].category);
        }
        s->page=1;
        if(app->pending_count && resolve_pending(app)) {
            changed=fridge_apply(&s->store,s->store.session,app->resolved,app->pending_count,s->message,sizeof(s->message));
            if(changed)feedback=1;
        }
        break;
    case FRIDGE_SELECT:
        if(s->page!=1 || !s->pending || !s->choice_count)break;
        for(unsigned i=0;i<app->matches.count;++i)if(app->matches.ids[i]==(uint32_t)cmd->value) {
            resolve_choice(app,(uint32_t)cmd->value);
            if(resolve_pending(app)) {
                changed=fridge_apply(&s->store,s->store.session,app->resolved,app->pending_count,s->message,sizeof(s->message));
                if(changed)feedback=1;
            }
            break;
        }
        break;
    case FRIDGE_MORE:
        if(s->pending && s->choice_count>3) {
            s->choice_offset=(s->choice_offset+3<s->choice_count)?s->choice_offset+3:0;choices(app);
            snprintf(s->message,sizeof(s->message),"请点选：%s",app->observations[app->pending_index].name);
        }
        break;
    case FRIDGE_CORRECT:
        if (s->page != 1 || (cmd->capture && cmd->capture != s->capture)) { break; }
        changed = fridge_correct(&s->store, cmd->session, cmd->text, cmd->value, cmd->date, s->message, sizeof(s->message));
        s->interrupted = false;
        if (changed) { feedback = 2; snprintf(s->message, sizeof(s->message), "已修改本次数量"); }
        break;
    case FRIDGE_FINISH:
        if (cmd->session && cmd->session != s->store.session) { break; }
        if (s->page == 6) {
            memset(app->password, 0, sizeof(app->password));
            memset(&app->setting_request, 0, sizeof(app->setting_request));
        }
        s->page = 0; s->interrupted = false; break;
    case FRIDGE_SETTINGS:
        if (s->page == 0) s->page = 4;
        break;
    case FRIDGE_UNDO:
        if (cmd->session && cmd->session != s->store.session) { break; }
        changed = fridge_undo(&s->store);
        if (changed) { feedback = 3; s->page = 1; s->interrupted = false; snprintf(s->message, sizeof(s->message), "已撤销本次记录"); }
        break;
    case FRIDGE_ERROR:
        if (cmd->session != s->store.session || cmd->capture != s->capture || !s->page) { break; }
        if (s->page == 2) { s->page = 1; }
        snprintf(s->message, sizeof(s->message), "%s", cmd->text);
        break;
    case FRIDGE_INTERRUPT:
        if (s->page != 1 || !s->store.undo_available || cmd->session != s->store.session) { break; }
        s->interrupted = true;
        snprintf(s->message, sizeof(s->message), "正在听，请说出纠正"); break;
    case FRIDGE_SAY:
        if (s->page != 1 || !s->store.undo_available || cmd->session != s->store.session) { break; }
        snprintf(s->message, sizeof(s->message), "%s", cmd->text); break;
    case FRIDGE_RESUME:
        if (cmd->session != s->store.session || s->page != 1) { break; }
        s->interrupted = false;
        snprintf(s->message, sizeof(s->message), "可以继续拍照"); break;
    default: break;
    }
    if (changed && (!app->platform.save || !app->platform.save(app->platform.ctx, &s->store))) {
        s->store = app->rollback; s->page = previous_page == 2 ? 1 : previous_page;
        feedback = 0;
        snprintf(s->message, sizeof(s->message), "保存失败，本次修改未生效");
    }
    if (feedback) {
        s->feedback = feedback; ++s->feedback_id;
        if (feedback != 3) toast_start(app);
    }
    paint(app, changed || previous_page != s->page);
}

static void settings_paint(struct app_fridge *app)
{
    const fridge_settings_t *s = &app->settings;
    char value[64];
    snprintf(value, sizeof(value), "%u%%", s->volume);
    check(app, gsp_fridge_volume_text_set_text(app->ui, value));
    check(app, gsp_fridge_network_status_set_text(app->ui,
          s->connected && !s->busy ? "Wi-Fi 已连接" : s->status));
    snprintf(value, sizeof(value), "%s%s%s", s->ssid, s->ip[0] ? " / " : "", s->ip);
    check(app, gsp_fridge_network_address_set_text(app->ui, value));
    check(app, gsp_fridge_scan_status_set_text(app->ui, s->status));
    for (unsigned i = 0; i < 4; ++i) {
        unsigned index = app->network_offset + i;
        const bool visible = index < s->count;
        value[0] = 0;
        if (visible) snprintf(value, sizeof(value), "%s%s", s->aps[index].ssid, s->aps[index].secured ? "  *" : "");
        switch (i) {
        case 0: check(app, gsp_fridge_ap_slot_0_set_visible(app->ui, visible)); check(app, gsp_fridge_ap_text_0_set_text(app->ui, value)); break;
        case 1: check(app, gsp_fridge_ap_slot_1_set_visible(app->ui, visible)); check(app, gsp_fridge_ap_text_1_set_text(app->ui, value)); break;
        case 2: check(app, gsp_fridge_ap_slot_2_set_visible(app->ui, visible)); check(app, gsp_fridge_ap_text_2_set_text(app->ui, value)); break;
        case 3: check(app, gsp_fridge_ap_slot_3_set_visible(app->ui, visible)); check(app, gsp_fridge_ap_text_3_set_text(app->ui, value)); break;
        }
    }
}

static void settings_refresh(struct app_fridge *app)
{
    if (app->platform.settings_read) app->platform.settings_read(app->platform.ctx, &app->settings);
    else { app->settings.volume = 45; snprintf(app->settings.status, sizeof(app->settings.status), "设置服务未启动"); }
    if (app->network_offset >= app->settings.count) app->network_offset = 0;
    settings_paint(app);
}

static void settings_send(struct app_fridge *app)
{
    if (!app->platform.settings_request || !app->platform.settings_request(app->platform.ctx, &app->setting_request)) {
        check(app, gsp_fridge_scan_status_set_text(app->ui, "操作忙，请稍后再试"));
    }
    memset(&app->setting_request, 0, sizeof(app->setting_request));
    settings_refresh(app);
}

static void password_close(struct app_fridge *app)
{
    check(app, gsp_fridge_password_mask_set_text(app->ui, ""));
    memset(app->password, 0, sizeof(app->password));
}

static bool settings_event(struct app_fridge *app, const esp_gsp_event_t *evt)
{
    unsigned page = app->state.page;
    if (gsp_fridge_event_is_open_settings(evt) && page == 0) {
        app->state.page = 4; settings_refresh(app);
    } else if (page == 4 && gsp_fridge_event_is_settings_back(evt)) app->state.page = 0;
    else if (page == 4 && gsp_fridge_event_is_choose_network(evt)) {
        app->state.page = 5; app->network_offset = 0;
        app->setting_request.action = FRIDGE_CFG_SCAN; settings_send(app);
    } else if (page == 4 && (gsp_fridge_event_is_volume_up(evt) || gsp_fridge_event_is_volume_down(evt) || gsp_fridge_event_is_volume_mute(evt))) {
        unsigned volume = app->settings.volume;
        if (gsp_fridge_event_is_volume_up(evt)) volume = volume > 90 ? 100 : volume + 10;
        else if (gsp_fridge_event_is_volume_down(evt)) volume = volume < 10 ? 0 : volume - 10;
        else if (volume) { app->unmuted_volume = volume; volume = 0; }
        else volume = app->unmuted_volume ? app->unmuted_volume : 45;
        app->setting_request.action = FRIDGE_CFG_VOLUME; app->setting_request.volume = volume; settings_send(app);
    } else if (page == 4 && gsp_fridge_event_is_volume_test(evt)) {
        app->setting_request.action = FRIDGE_CFG_TEST; settings_send(app);
    } else if (page == 5 && gsp_fridge_event_is_networks_back(evt)) app->state.page = 4;
    else if (page == 5 && gsp_fridge_event_is_net_scan(evt)) {
        app->setting_request.action = FRIDGE_CFG_SCAN; settings_send(app);
    } else if (page == 5 && (gsp_fridge_event_is_net_previous(evt) || gsp_fridge_event_is_net_next(evt))) {
        if (gsp_fridge_event_is_net_previous(evt) && app->network_offset >= 4) app->network_offset -= 4;
        if (gsp_fridge_event_is_net_next(evt) && app->network_offset + 4 < app->settings.count) app->network_offset += 4;
        settings_paint(app);
    } else if (page == 6 && gsp_fridge_event_is_password_back(evt)) {
        password_close(app); memset(&app->setting_request, 0, sizeof(app->setting_request)); app->state.page = 5;
    } else if (page == 6 && gsp_fridge_event_is_wifi_keyboard_key(evt)) {
        /* Consume native keyboard key codes directly: the PC bridge does not
         * expose the device edit-buffer API. Only masked text reaches GSP. */
        size_t length = strlen(app->setting_request.password);
        if (evt->arg != 13 && evt->arg != 10) {
            if (evt->arg == 8 && length) app->setting_request.password[--length] = 0;
            else if (evt->arg >= 32 && evt->arg <= 126 && length < 63) {
                app->setting_request.password[length++] = (char)evt->arg;
                app->setting_request.password[length] = 0;
            }
            memset(app->password, '*', length); app->password[length] = 0;
            check(app, gsp_fridge_password_mask_set_text(app->ui, app->password));
            check(app, gsp_fridge_password_hint_set_text(app->ui, "输入后按连接，密码不会显示"));
            return true;
        }
        if (length < 8 || length > 63) {
            check(app, gsp_fridge_password_hint_set_text(app->ui, "密码需为 8 到 63 个字符"));
            return true;
        }
        app->setting_request.action = FRIDGE_CFG_CONNECT; settings_send(app);
        password_close(app); app->state.page = 4;
    } else if (page == 5) {
        int slot = gsp_fridge_event_is_ap_0(evt) ? 0 : gsp_fridge_event_is_ap_1(evt) ? 1 :
                   gsp_fridge_event_is_ap_2(evt) ? 2 : gsp_fridge_event_is_ap_3(evt) ? 3 : -1;
        if (slot < 0) return false;
        unsigned index = app->network_offset + slot;
        if (app->settings.busy || index >= app->settings.count) return true;
        memset(&app->setting_request, 0, sizeof(app->setting_request));
        snprintf(app->setting_request.ssid, sizeof(app->setting_request.ssid), "%s", app->settings.aps[index].ssid);
        if (!app->settings.aps[index].secured) {
            app->setting_request.action = FRIDGE_CFG_CONNECT; settings_send(app); app->state.page = 4;
        } else {
            app->state.page = 6;
            check(app, gsp_fridge_selected_network_set_text(app->ui, app->setting_request.ssid));
            check(app, gsp_fridge_password_hint_set_text(app->ui, "输入后按连接，密码不会显示"));
            password_close(app);
        }
    } else return false;
    paint(app, page != app->state.page);
    return true;
}

static void event(esp_gsp_handle_t ui, const esp_gsp_event_t *evt, void *ctx)
{
    (void)ui;
    struct app_fridge *app = ctx;
    if (settings_event(app, evt)) return;
    memset(&app->inbox, 0, sizeof(app->inbox));
    if (gsp_fridge_event_is_enter_in(evt)) { app->inbox.action = FRIDGE_IN; }
    else if (gsp_fridge_event_is_enter_out(evt)) { app->inbox.action = FRIDGE_OUT; }
    else if (gsp_fridge_event_is_take_photo(evt)) { app->inbox.action = FRIDGE_CAPTURE; }
    else if (gsp_fridge_event_is_camera_back(evt) || gsp_fridge_event_is_done(evt)) { app->inbox.action = FRIDGE_FINISH; }
    else if (gsp_fridge_event_is_undo(evt)) { app->inbox.action = FRIDGE_UNDO; }
    else if(gsp_fridge_event_is_match_pick_0(evt) || gsp_fridge_event_is_match_pick_1(evt) || gsp_fridge_event_is_match_pick_2(evt)) {
        unsigned slot=gsp_fridge_event_is_match_pick_0(evt)?0:gsp_fridge_event_is_match_pick_1(evt)?1:2;
        if(!app->state.pending || app->state.choice_offset+slot>=app->state.choice_count)return;
        app->inbox.action=FRIDGE_SELECT;app->inbox.value=(int32_t)app->state.choice_ids[slot];
        app->inbox.session=app->state.store.session;app->inbox.capture=app->state.capture;
    }
    else if(gsp_fridge_event_is_match_more(evt)) {
        app->inbox.action=FRIDGE_MORE;app->inbox.session=app->state.store.session;app->inbox.capture=app->state.capture;
    }
    else { return; }
    app_fridge_dispatch(app, &app->inbox);
}

static void tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    struct app_fridge *app = ctx;
    toast_tick(app);
    if (app->state.page == 4 || app->state.page == 5) {
        uint32_t revision = app->settings.revision;
        if (app->platform.settings_read) app->platform.settings_read(app->platform.ctx, &app->settings);
        if (revision != app->settings.revision) settings_paint(app);
    }
    if (app->row_damage_pending) {
        app->row_damage_pending = false;
        /* Remote row glyph publication can miss the host's damage rectangle.
         * Invalidate the authored full-screen backdrop after row publication. */
        check(app, gsp_fridge_backdrop_set_visible(ui, false));
        check(app, gsp_fridge_backdrop_set_visible(ui, true));
    }
    if (app->rows_pending) {
        app->rows_pending = false;
        check(app, esp_gsp_list_set_total(ui, app->stock_list, app->state.page == 0 ? app->state.store.count : 0));
        check(app, esp_gsp_list_set_total(ui, app->batch_list, app->state.page == 3 ? app->state.store.batch_count : 0));
        check(app, esp_gsp_list_refresh(ui, app->stock_list));
        check(app, esp_gsp_list_refresh(ui, app->batch_list));
    }
    if (app->platform.poll && app->platform.poll(app->platform.ctx, &app->inbox)) {
        app_fridge_dispatch(app, &app->inbox);
    }
}

esp_gsp_err_t app_fridge_create(esp_gsp_handle_t ui, const fridge_platform_t *platform, app_fridge_handle_t *out)
{
    if (!ui || !platform || !out) { return ESP_GSP_ERR_INVALID_ARG; }
    *out = NULL;
    struct app_fridge *app = calloc(1, sizeof(*app));
    if (!app) { return ESP_GSP_ERR_NO_MEM; }
    app->ui = ui; app->platform = *platform;
    fridge_store_init(&app->state.store);
    app->storage_ready = platform->load && platform->load(platform->ctx, &app->state.store);
    if (!app->storage_ready) { snprintf(app->state.message, sizeof(app->state.message), "库存读取失败"); }
    app->stock_list = gsp_fridge_stock_bind(ui, stock_row, app);
    app->batch_list = gsp_fridge_batch_bind(ui, batch_row, app);
    if (app->stock_list == ESP_GSP_LIST_NONE || app->batch_list == ESP_GSP_LIST_NONE) { free(app); return ESP_GSP_ERR_NO_MEM; }
    paint(app, true);
    check(app, esp_gsp_on_event(ui, event, app));
    app->timer = esp_gsp_timer_create(ui, 40, tick, app);
    if (!app->timer || app->error) { app_fridge_destroy(&app); return ESP_GSP_ERR_NO_MEM; }
    *out = app; return ESP_GSP_OK;
}

esp_gsp_err_t app_fridge_present(app_fridge_handle_t app, const uint16_t *pixels,
                                esp_gsp_canvas_release_cb_t release, void *ctx)
{
    return esp_gsp_canvas_try_push(app->ui, GSP_FRIDGE_BIND_PREVIEW, pixels, FRIDGE_PREVIEW_WIDTH * 2, release, ctx);
}

esp_gsp_err_t app_fridge_destroy(app_fridge_handle_t *handle)
{
    if (!handle || !*handle) { return ESP_GSP_OK; }
    struct app_fridge *app = *handle;
    esp_gsp_err_t err = esp_gsp_on_event(app->ui, NULL, NULL);
    if (err) { return err; }
    if (app->timer) { err = esp_gsp_timer_delete(app->ui, app->timer); if (err) { return err; } }
    err = esp_gsp_canvas_stop(app->ui, GSP_FRIDGE_BIND_PREVIEW);
    if (err) { return err; }
    free(app); *handle = NULL; return ESP_GSP_OK;
}
