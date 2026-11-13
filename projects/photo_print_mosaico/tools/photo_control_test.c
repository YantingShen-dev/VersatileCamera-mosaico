#include "photo_control.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    photo_control_t c;
    photo_control_init(&c);
    c.state.mode = PHOTO_LIVE;
    c.state.connected = true;
    photo_control_voice(&c, 0);
    assert(c.state.mode == PHOTO_LIVE); // unavailable recognizer cannot trigger
    c.state.voice_ready = true;
    photo_control_button(&c, PHOTO_BUTTON2, 10);
    photo_control_voice(&c, 11);
    assert(c.state.mode == PHOTO_FILTER);
    photo_control_button(&c, PHOTO_BUTTON1, 20);
    assert(c.state.filter == PHOTO_ASCII);
    photo_control_button(&c, PHOTO_BUTTON2, 100);
    photo_control_tick(&c, 451);
    assert(c.state.filter == PHOTO_NORMAL);
    photo_control_button(&c, PHOTO_BUTTON2, 500);
    photo_control_button(&c, PHOTO_BUTTON2, 700);
    assert(c.state.mode == PHOTO_LIVE && c.state.filter == PHOTO_NORMAL);
    photo_control_voice(&c, 1000);
    assert(c.state.mode == PHOTO_COUNTDOWN && c.state.countdown == 5);
    unsigned beeps = !!(photo_control_take_actions(&c) & PHOTO_DO_BEEP);
    photo_control_voice(&c, 1900); // duplicate command must not extend countdown
    for (unsigned t = 2000; t <= 5000; t += 1000) {
        photo_control_tick(&c, t);
        beeps += !!(photo_control_take_actions(&c) & PHOTO_DO_BEEP);
    }
    assert(beeps == 5);
    photo_control_tick(&c, 5999);
    assert(!(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE));
    photo_control_tick(&c, 6000);
    assert(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE);
    photo_control_captured(&c, true);
    photo_control_voice(&c, 6100);
    assert(c.state.mode == PHOTO_REVIEW && !photo_control_take_actions(&c));
    c.state.connected = false;
    photo_control_button(&c, PHOTO_BUTTON1, 6200);
    assert(!photo_control_take_actions(&c));
    c.state.connected = true;
    photo_control_button(&c, PHOTO_BUTTON1, 6300);
    assert(photo_control_take_actions(&c) & PHOTO_DO_PRINT);
    photo_control_voice(&c, 6350);
    photo_control_button(&c, PHOTO_BUTTON1, 6360);
    assert(c.state.mode == PHOTO_SENDING && !photo_control_take_actions(&c));
    photo_control_printed(&c, false);
    photo_control_voice(&c, 6400);
    assert(c.state.mode == PHOTO_FAILED);
    photo_control_button(&c, PHOTO_BUTTON2, 6500);
    photo_control_voice(&c, 7000);
    assert(c.state.mode == PHOTO_COUNTDOWN);
    photo_control_button(&c, PHOTO_BUTTON2, 7001);
    photo_control_tick(&c, 13000);
    assert(c.state.mode == PHOTO_LIVE && !photo_control_take_actions(&c));
    photo_control_button(&c, PHOTO_BUTTON1, 14000);
    assert(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE);
    puts("PASS: voice-only live trigger, five beeps/seconds, duplicate suppression, cancel, filters, manual capture and print");
}
