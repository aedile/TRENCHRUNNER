/*
 * autoplay.h - Star Wars flies itself when nobody else will.
 *
 * The attract mode is eighty-five seconds of text. On a medal that is meant to be looked at,
 * the game is far better company than its attract, so after the attract has run through
 * once with nobody touching anything, this starts a game and plays it.
 *
 * It aims from the vector list we already generate. Every TIE fighter is exactly 94 green
 * segments; fireballs are red clusters in the middle of the screen; the crosshair is 16 cyan
 * segments and it drifts, so the yoke is driven closed-loop on where the crosshair actually
 * is rather than open-loop on where it ought to be. The cockpit brackets, the shield bar, the
 * score and the message text sit in fixed bands at the edges and are simply not looked at.
 *
 * A human touching anything takes over at once; the autopilot stays out until that game is
 * over and the attract has been left alone for a while again.
 */
#ifndef AUTOPLAY_H
#define AUTOPLAY_H
#include <stdint.h>
#include "avg.h"
#include "starwars.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AP_IDLE = 0, AP_STARTING, AP_PLAYING, AP_HUMAN } ap_state_t;

typedef struct {
    uint32_t idle_us;          /* how long the attract runs untouched before the autopilot starts */
    uint32_t lost_us;          /* no crosshair for this long during play = the game is over */
    uint32_t max_game_us;      /* never fly longer than this in one go */
} ap_config_t;

void ap_init(const ap_config_t *cfg);
/* Feed every completed vector list. Cheap: a pass over the points. */
void ap_frame(const avg_t *avg);
/* Call once per input poll with the wall clock and whether a person is touching the controls.
 * Writes yaw/pitch/fire into `in` while the autopilot is flying; leaves them alone otherwise. */
void ap_update(sw_input_t *in, uint64_t now_us, int human_active);
ap_state_t ap_state(void);
/* diagnostics */
int  ap_targets(void);
void ap_crosshair(int *x, int *y);
int  ap_have_cross(void);
int  ap_port_ahead(void);
int  ap_debug_yellow(int i, int *n, int *x0, int *y0, int *x1, int *y1);
int  ap_in_trench(void);
void ap_target(int *x, int *y, int *have);

#ifdef __cplusplus
}
#endif
#endif
