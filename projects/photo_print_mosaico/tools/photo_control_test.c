#include "photo_control.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    photo_control_t c;
    photo_control_init(&c);
    c.state.mode = PHOTO_LIVE;
    c.state.connected = true;
    photo_control_button(&c, PHOTO_BUTTON2, 10);
    assert(c.state.mode == PHOTO_FILTER);
    photo_control_button(&c, PHOTO_BUTTON1, 20);
    assert(c.state.filter == PHOTO_ASCII);
    photo_control_button(&c, PHOTO_BUTTON2, 100);
    photo_control_tick(&c, 451);
    assert(c.state.filter == PHOTO_NORMAL);
    photo_control_button(&c, PHOTO_BUTTON2, 500);
    photo_control_button(&c, PHOTO_BUTTON2, 700);
    assert(c.state.mode == PHOTO_LIVE &&
           c.state.filter == PHOTO_NORMAL); // double click must not move selection
    photo_control_palm(&c, true, 1000);
    photo_control_palm(&c, true, 1699);
    assert(c.state.mode == PHOTO_LIVE);
    photo_control_palm(&c, true, 1700);
    assert(c.state.mode == PHOTO_COUNTDOWN);
    unsigned beeps = !!(photo_control_take_actions(&c) & PHOTO_DO_BEEP);
    for (unsigned t = 2700; t <= 5700; t += 1000) {
        photo_control_tick(&c, t);
        beeps += !!(photo_control_take_actions(&c) & PHOTO_DO_BEEP);
    }
    assert(beeps == 5);
    photo_control_tick(&c, 6699);
    assert(!(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE));
    photo_control_tick(&c, 6700);
    assert(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE);
    photo_control_captured(&c, true);
    assert(c.state.mode == PHOTO_REVIEW);
    c.state.connected = false;
    photo_control_button(&c, PHOTO_BUTTON1, 6800);
    assert(!photo_control_take_actions(&c));
    c.state.connected = true;
    photo_control_button(&c, PHOTO_BUTTON1, 6900);
    assert(photo_control_take_actions(&c) & PHOTO_DO_PRINT);
    photo_control_button(&c, PHOTO_BUTTON1, 6950);
    assert(!photo_control_take_actions(&c));
    photo_control_printed(&c, false);
    assert(c.state.mode == PHOTO_FAILED);
    photo_control_button(&c, PHOTO_BUTTON2, 7000);
    photo_control_palm(&c, true, 9000);
    assert(c.state.mode == PHOTO_LIVE);
    photo_control_palm(&c, false, 9100);
    photo_control_palm(&c, false, 9900);
    photo_control_palm(&c, true, 10000);
    photo_control_palm(&c, true, 10700);
    assert(c.state.mode == PHOTO_COUNTDOWN);
    photo_control_take_actions(&c);
    photo_control_button(&c, PHOTO_BUTTON2, 10800);
    photo_control_tick(&c, 17000);
    assert(c.state.mode == PHOTO_LIVE && !photo_control_take_actions(&c));
    photo_control_button(&c, PHOTO_BUTTON1, 17001);
    assert(photo_control_take_actions(&c) & PHOTO_DO_CAPTURE);
    puts("control: filter wrap/double-click, 5 beeps/5 seconds, hold rearm, cancel, offline and "
         "duplicate printing passed");
}
