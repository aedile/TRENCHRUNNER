/*
 * input.cpp - medal controls for Star Wars
 *
 * The buttons, the power rail, the coin, the power-off hold and the tilt zero all live in
 * components/medal_input, which every medal shares. What is left here is Star Wars' own: a
 * flight yoke, and two long-hold gestures on the fire button that no other medal has.
 *
 *   tilt                -> the yoke: twisting yaws, tipping pitches
 *   BOOT button         -> fire (also starts a game in free play)
 *                          held 3-13 s and released: sound on and off
 *                          held 13 s: the easter egg (input_take_gesture)
 *   PWR short press     -> coin; long press (1 s) -> power off
 *
 * The mute gesture in medal_input is switched off here, because Star Wars wants the whole of
 * the fire button for its own two holds.
 */
#include "input.h"
#include "medal_input.h"
#include "qmi8658.h"
#include "esp_log.h"

static const char *TAG = "INPUT";

#define FULL_DEFLECTION_DEG 20.0f     /* this much tilt = yoke at its stop */
#define DEADBAND_DEG 1.5f

/* Sign of each axis; flip on hardware if the ship steers the wrong way */
#define YAW_SIGN   (+1.0f)
#define PITCH_SIGN (+1.0f)

#define HOLD_SOUND_US   3000000
#define HOLD_EGG_US    13000000

static int pending_gesture = GESTURE_NONE;
static bool fire_hold_consumed;           /* the 13 s gesture fired; ignore the release */

static uint8_t angle_to_adc(float deg, float sign)
{
    if (deg > -DEADBAND_DEG && deg < DEADBAND_DEG) deg = 0.0f;
    float v = 128.0f + sign * deg * (127.0f / FULL_DEFLECTION_DEG);
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)v;
}

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.mute_hold_us = 0;             /* Star Wars handles its own fire-button holds */
    medal_input_init(&cfg);
}

void input_update(sw_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    in->fire  = st.boot;
    in->coin1 = st.coin ? 1 : 0;

    if (st.boot && st.boot_held_us < HOLD_SOUND_US) fire_hold_consumed = false;
    if (st.boot && !fire_hold_consumed && st.boot_held_us >= HOLD_EGG_US) {
        pending_gesture = GESTURE_EGG;
        fire_hold_consumed = true;
    }
    if (st.boot_released && !fire_hold_consumed &&
        st.boot_release_held_us >= HOLD_SOUND_US && st.boot_release_held_us < HOLD_EGG_US)
        pending_gesture = GESTURE_TOGGLE_SOUND;

    if (st.tilt_valid) {
        in->yaw   = angle_to_adc(st.lr, YAW_SIGN);
        in->pitch = angle_to_adc(st.ud, PITCH_SIGN);
    } else {
        in->yaw = 0x80;
        in->pitch = 0x80;
    }
}

int input_take_gesture(void)
{
    int g = pending_gesture;
    pending_gesture = GESTURE_NONE;
    return g;
}
