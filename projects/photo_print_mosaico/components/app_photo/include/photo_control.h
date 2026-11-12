#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PHOTO_SIDE 384
#define PHOTO_BYTES (PHOTO_SIDE * PHOTO_SIDE / 8)
typedef enum {
    PHOTO_NORMAL,
    PHOTO_INVERT,
    PHOTO_DOTS,
    PHOTO_WOODCUT,
    PHOTO_ASCII,
    PHOTO_FILTER_COUNT
} photo_filter_t;
typedef enum {
    PHOTO_LOADING,
    PHOTO_LIVE,
    PHOTO_REVIEW,
    PHOTO_SENDING,
    PHOTO_SENT,
    PHOTO_FAILED,
    PHOTO_CAMERA_ERROR,
    PHOTO_FILTER,
    PHOTO_COUNTDOWN,
    PHOTO_CAPTURING
} photo_mode_t;
typedef enum { PHOTO_BUTTON1, PHOTO_BUTTON2 } photo_command_t;
typedef struct {
    photo_mode_t mode;
    photo_filter_t filter;
    bool connected, board_connected, gesture_ready, audio_ready;
    uint32_t response;
    unsigned countdown, photos;
} photo_state_t;
enum { PHOTO_DO_CAPTURE = 1, PHOTO_DO_PRINT = 2, PHOTO_DO_BEEP = 4 };
typedef struct {
    photo_state_t state;
    uint64_t countdown_end, second_click_at, palm_since, absent_since;
    bool second_click_pending, palm_tracking, absent_tracking, gesture_armed;
    unsigned actions;
} photo_control_t;
/* Single owner; all timestamps are monotonic milliseconds. */
void photo_control_init(photo_control_t *c);
void photo_control_button(photo_control_t *c, photo_command_t button, uint64_t now);
void photo_control_tick(photo_control_t *c, uint64_t now);
void photo_control_palm(photo_control_t *c, bool open_palm, uint64_t now);
unsigned photo_control_take_actions(photo_control_t *c);
void photo_control_captured(photo_control_t *c, bool success);
void photo_control_printed(photo_control_t *c, bool success);
const char *photo_filter_name(photo_filter_t filter);
#ifdef __cplusplus
}
#endif
