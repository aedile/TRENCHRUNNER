/*
 * autoplay.c - see autoplay.h.
 */
#include "autoplay.h"
#include <string.h>
#include <stdlib.h>

/* beam space */
#define PLAY_X0   25
#define PLAY_X1  225
#define PLAY_Y0   50
#define PLAY_Y1  258
#define CX       125
#define CY       155

/* what things are made of */
#define COL_GREEN 2
#define COL_CYAN  3
#define COL_RED   4
#define COL_WHITE 7
#define TIE_SEGS  94             /* every TIE fighter, exactly */
#define MIN_TARGET_SEGS 8        /* smaller red or green than this is debris or a star */
#define CROSSHAIR_SEGS 16

#define MAX_CLUSTERS 48

typedef struct { int c, n, x0, y0, x1, y1; } cluster_t;

static ap_config_t cfg;
static ap_state_t state;
static uint64_t idle_since, game_since, lost_since, start_since;
static int have_frame;

/* what the last frame held */
static int cross_x, cross_y, have_cross;
static int tgt_x, tgt_y, have_tgt, ntargets;

void ap_init(const ap_config_t *c)
{
    cfg = *c;
    if (!cfg.idle_us)     cfg.idle_us = 100u * 1000000u;
    if (!cfg.lost_us)     cfg.lost_us = 5u * 1000000u;
    if (!cfg.max_game_us) cfg.max_game_us = 300u * 1000000u;
    state = AP_IDLE;
    idle_since = game_since = lost_since = start_since = 0;
    have_cross = have_tgt = ntargets = 0;
}

ap_state_t ap_state(void) { return state; }
int ap_targets(void) { return ntargets; }
void ap_crosshair(int *x, int *y) { *x = cross_x; *y = cross_y; }

/*
 * Group the drawn segments of one colour into clusters by endpoint proximity. This is the
 * whole of the "vision": a TIE fighter is a green cluster of 94, a fireball a red cluster in
 * the play area, the crosshair a cyan cluster of 16. Quadratic in the number of segments of
 * a colour, which is a few hundred at most, once a frame - cheap enough.
 */
static int gather(const avg_t *avg, int colour, cluster_t *out, int max)
{
    int n = 0;
    int px = 0, py = 0;
    for (int i = 0; i < avg->npoints; i++) {
        const avg_point_t *p = &avg->points[i];
        int x = (int)(p->x >> 16), y = (int)(p->y >> 16);
        if (i && p->intensity && p->color == colour) {
            int sx0 = px < x ? px : x, sx1 = px < x ? x : px;
            int sy0 = py < y ? py : y, sy1 = py < y ? y : py;
            /* attach to a cluster this segment touches (within 6), else start one */
            int k;
            for (k = 0; k < n; k++) {
                cluster_t *c = &out[k];
                if (sx1 >= c->x0 - 6 && sx0 <= c->x1 + 6 && sy1 >= c->y0 - 6 && sy0 <= c->y1 + 6) break;
            }
            if (k == n) {
                if (n == max) { px = x; py = y; continue; }
                out[n++] = (cluster_t){ colour, 0, sx0, sy0, sx1, sy1 };
            }
            cluster_t *c = &out[k];
            c->n++;
            if (sx0 < c->x0) c->x0 = sx0;
            if (sy0 < c->y0) c->y0 = sy0;
            if (sx1 > c->x1) c->x1 = sx1;
            if (sy1 > c->y1) c->y1 = sy1;
        }
        px = x; py = y;
    }
    /* a segment can bridge two clusters that were started apart; one merge pass is enough here */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            if (out[b].n == 0) continue;
            if (out[b].x1 >= out[a].x0 - 6 && out[b].x0 <= out[a].x1 + 6 &&
                out[b].y1 >= out[a].y0 - 6 && out[b].y0 <= out[a].y1 + 6) {
                out[a].n += out[b].n;
                if (out[b].x0 < out[a].x0) out[a].x0 = out[b].x0;
                if (out[b].y0 < out[a].y0) out[a].y0 = out[b].y0;
                if (out[b].x1 > out[a].x1) out[a].x1 = out[b].x1;
                if (out[b].y1 > out[a].y1) out[a].y1 = out[b].y1;
                out[b].n = 0;
            }
        }
    return n;
}

static int in_play(const cluster_t *c)
{
    int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
    return cx >= PLAY_X0 && cx <= PLAY_X1 && cy >= PLAY_Y0 && cy <= PLAY_Y1;
}

void ap_frame(const avg_t *avg)
{
    static cluster_t cl[MAX_CLUSTERS];
    have_frame = 1;

    /* the crosshair: sixteen cyan segments, and the only cyan thing on the screen */
    int n = gather(avg, COL_CYAN, cl, MAX_CLUSTERS);
    have_cross = 0;
    for (int i = 0; i < n; i++)
        if (cl[i].n >= CROSSHAIR_SEGS - 2 && cl[i].n <= CROSSHAIR_SEGS + 2) {
            cross_x = (cl[i].x0 + cl[i].x1) / 2; cross_y = (cl[i].y0 + cl[i].y1) / 2; have_cross = 1;
        }

    /*
     * Targets. Fireballs first - a red cluster in the play area is on its way to the shields
     * and shooting it is what the game is about - then the nearest TIE fighter. Anything
     * outside the play area is cockpit, and any small red thing is a laser bolt or a spark.
     */
    int best = -1, bestd = 0x7fffffff, bestpri = 0;
    have_tgt = 0; ntargets = 0;
    int ax = have_cross ? cross_x : CX, ay = have_cross ? cross_y : CY;
    for (int pass = 0; pass < 2; pass++) {
        int colour = pass == 0 ? COL_RED : COL_GREEN;
        n = gather(avg, colour, cl, MAX_CLUSTERS);
        for (int i = 0; i < n; i++) {
            const cluster_t *c = &cl[i];
            if (c->n < MIN_TARGET_SEGS || !in_play(c)) continue;
            if (colour == COL_GREEN && c->n < TIE_SEGS / 2) continue;   /* text and debris */
            ntargets++;
            int pri = (colour == COL_RED) ? 2 : 1;
            int cx = (c->x0 + c->x1) / 2, cy = (c->y0 + c->y1) / 2;
            int d = abs(cx - ax) + abs(cy - ay);
            if (pri > bestpri || (pri == bestpri && d < bestd)) { best = i; bestd = d; bestpri = pri; tgt_x = cx; tgt_y = cy; have_tgt = 1; }
        }
        if (have_tgt && pass == 0) break;            /* a fireball wins outright */
    }
    (void)best;
}

void ap_update(sw_input_t *in, uint64_t now_us, int human_active)
{
    if (!idle_since) idle_since = now_us;

    if (human_active) {
        /* a person: hands off, and stay off until their game is done and the attract has
         * been left alone again */
        state = AP_HUMAN;
        idle_since = now_us;
        return;
    }

    switch (state) {
    case AP_HUMAN:
        /* their game ends when the crosshair has been gone a while; then it is an attract
         * nobody is touching, and the idle clock starts over */
        if (!have_cross) { if (!lost_since) lost_since = now_us; }
        else lost_since = 0;
        if (lost_since && now_us - lost_since > cfg.lost_us) { state = AP_IDLE; idle_since = now_us; lost_since = 0; }
        return;

    case AP_IDLE:
        if (now_us - idle_since >= cfg.idle_us) { state = AP_STARTING; start_since = now_us; }
        return;

    case AP_STARTING:
        /* free play: a pull of the trigger starts the game */
        in->fire = (now_us - start_since) < 400000;
        if (now_us - start_since > 1500000) { state = AP_PLAYING; game_since = now_us; lost_since = 0; }
        return;

    case AP_PLAYING: {
        /* the game is over when the crosshair has been gone a while, or we have flown long enough */
        if (!have_cross) { if (!lost_since) lost_since = now_us; }
        else lost_since = 0;
        if ((lost_since && now_us - lost_since > cfg.lost_us) || now_us - game_since > cfg.max_game_us) {
            state = AP_IDLE; idle_since = now_us; lost_since = 0;
            in->yaw = in->pitch = 0x80; in->fire = 0;
            return;
        }
        /*
         * Steering. The yoke is a velocity-ish input and the crosshair drifts, so this is a
         * plain proportional push on the error between where the crosshair is and where the
         * target is, clamped to the yoke's throw. With nothing to shoot, ease back to centre.
         */
        int ex = 0, ey = 0;
        if (have_tgt && have_cross) { ex = tgt_x - cross_x; ey = tgt_y - cross_y; }
        else if (have_cross)        { ex = CX - cross_x;    ey = CY - cross_y; }
        int yaw   = 0x80 + ex * 3;
        int pitch = 0x80 + ey * 3;
        if (yaw < 0) yaw = 0;
        if (yaw > 255) yaw = 255;
        if (pitch < 0) pitch = 0;
        if (pitch > 255) pitch = 255;
        in->yaw = (uint8_t)yaw;
        in->pitch = (uint8_t)pitch;
        /* shoot whenever there is something to shoot at and we are roughly on it; the trigger
         * is pulsed, because the game fires on the press, not while held */
        static int trig;
        int on_target = have_tgt && abs(ex) < 22 && abs(ey) < 22;
        in->fire = on_target ? ((trig++ >> 1) & 1) : 0;
        return;
    }
    }
}
