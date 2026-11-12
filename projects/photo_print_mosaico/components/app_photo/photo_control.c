#include "photo_control.h"
#include <string.h>
static void capture(photo_control_t *c) {
    c->state.mode = PHOTO_CAPTURING;
    c->state.countdown = 0;
    c->actions |= PHOTO_DO_CAPTURE;
    c->palm_tracking = false;
    c->gesture_armed = false;
}
void photo_control_init(photo_control_t *c) {
    memset(c, 0, sizeof(*c));
    c->state.mode = PHOTO_LOADING;
    c->gesture_armed = true;
}
const char *photo_filter_name(photo_filter_t f) {
    static const char *names[] = {"正常黑白", "黑白反色", "黑白波点", "阈值版画", "ASCII"};
    return names[f >= 0 && f < PHOTO_FILTER_COUNT ? f : 0];
}
void photo_control_button(photo_control_t *c, photo_command_t b, uint64_t now) {
    ++c->state.response;
    switch (c->state.mode) {
    case PHOTO_LIVE:
        if (b == PHOTO_BUTTON1)
            capture(c);
        else {
            c->state.mode = PHOTO_FILTER;
            c->second_click_pending = false;
            c->palm_tracking = false;
        }
        break;
    case PHOTO_FILTER:
        if (b == PHOTO_BUTTON1) {
            /* A preceding single next-click is resolved before moving back. */
            if (c->second_click_pending)
                c->state.filter = (c->state.filter + 1) % PHOTO_FILTER_COUNT;
            c->second_click_pending = false;
            c->state.filter = (c->state.filter + PHOTO_FILTER_COUNT - 1) % PHOTO_FILTER_COUNT;
        } else if (c->second_click_pending && now - c->second_click_at <= 350) {
            c->second_click_pending = false;
            c->state.mode = PHOTO_LIVE;
        } else {
            if (c->second_click_pending)
                c->state.filter = (c->state.filter + 1) % PHOTO_FILTER_COUNT;
            c->second_click_pending = true;
            c->second_click_at = now;
        }
        break;
    case PHOTO_COUNTDOWN:
        if (b == PHOTO_BUTTON1)
            capture(c);
        else {
            c->state.mode = PHOTO_LIVE;
            c->state.countdown = 0;
        }
        break;
    case PHOTO_REVIEW:
    case PHOTO_SENT:
    case PHOTO_FAILED:
        if (b == PHOTO_BUTTON2) {
            c->state.mode = PHOTO_LIVE;
            c->palm_tracking = false;
        } else if (c->state.connected) {
            c->state.mode = PHOTO_SENDING;
            c->actions |= PHOTO_DO_PRINT;
        }
        break;
    default:
        break; /* Loading, capture and sending never accept another job. */
    }
}
void photo_control_tick(photo_control_t *c, uint64_t now) {
    if (c->state.mode == PHOTO_FILTER && c->second_click_pending &&
        now - c->second_click_at > 350) {
        c->second_click_pending = false;
        c->state.filter = (c->state.filter + 1) % PHOTO_FILTER_COUNT;
    }
    if (c->state.mode == PHOTO_COUNTDOWN) {
        if (now >= c->countdown_end)
            capture(c);
        else {
            unsigned n = (unsigned)((c->countdown_end - now + 999) / 1000);
            if (n != c->state.countdown) {
                c->state.countdown = n;
                c->actions |= PHOTO_DO_BEEP;
            }
        }
    }
}
void photo_control_palm(photo_control_t *c, bool open, uint64_t now) {
    if (!open) {
        c->palm_tracking = false;
        if (!c->absent_tracking) {
            c->absent_tracking = true;
            c->absent_since = now;
        }
        if (now - c->absent_since >= 800)
            c->gesture_armed = true;
        return;
    }
    c->absent_tracking = false;
    if (c->state.mode != PHOTO_LIVE || !c->gesture_armed) {
        c->palm_tracking = false;
        return;
    }
    if (!c->palm_tracking) {
        c->palm_tracking = true;
        c->palm_since = now;
    }
    if (now - c->palm_since >= 700) {
        c->gesture_armed = false;
        c->palm_tracking = false;
        c->state.mode = PHOTO_COUNTDOWN;
        c->state.countdown = 5;
        c->countdown_end = now + 5000;
        c->actions |= PHOTO_DO_BEEP;
    }
}
unsigned photo_control_take_actions(photo_control_t *c) {
    unsigned a = c->actions;
    c->actions = 0;
    return a;
}
void photo_control_captured(photo_control_t *c, bool ok) {
    c->state.mode = ok ? PHOTO_REVIEW : PHOTO_CAMERA_ERROR;
    if (ok)
        ++c->state.photos;
}
void photo_control_printed(photo_control_t *c, bool ok) {
    c->state.mode = ok ? PHOTO_SENT : PHOTO_FAILED;
}
