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
#define PORT_Y   200             /* low and centred: where the port comes up the trench */

/* what things are made of */
#define COL_GREEN 2
#define COL_CYAN  3
#define COL_RED   4
#define COL_WHITE 7
#define COL_YELLOW 6
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
static int port_ahead;               /* the "EXHAUST PORT AHEAD" banner is up */
static uint64_t port_seen_us;        /* when it was last up: the port itself comes a few seconds later */
static int last_cx, last_cy, have_last;   /* the crosshair a frame ago, for damping */
static int in_trench;                /* the trench walls are on screen */

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
int  ap_have_cross(void) { return have_cross; }
int  ap_port_ahead(void) { return port_ahead; }
static cluster_t dbg_yellow[MAX_CLUSTERS]; static int dbg_nyellow;
int ap_debug_yellow(int i, int *n, int *x0, int *y0, int *x1, int *y1)
{
    if (i >= dbg_nyellow) return 0;
    *n = dbg_yellow[i].n; *x0 = dbg_yellow[i].x0; *y0 = dbg_yellow[i].y0; *x1 = dbg_yellow[i].x1; *y1 = dbg_yellow[i].y1;
    return 1;
}
int  ap_in_trench(void) { return in_trench; }
void ap_target(int *x, int *y, int *have) { *x = tgt_x; *y = tgt_y; *have = have_tgt; }

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

    /*
     * The crosshair: sixteen cyan segments in a sixteen-pixel box. "USE THE FORCE" is cyan
     * too, at the top of the play area, and its words are about the same segment count, so
     * the shape is checked as well as the count; with more than one candidate, the nearest
     * to where it was a frame ago wins.
     */
    int n = gather(avg, COL_CYAN, cl, MAX_CLUSTERS);
    int prev_x = cross_x, prev_y = cross_y, prev_ok = have_cross;
    last_cx = cross_x; last_cy = cross_y; have_last = have_cross;
    have_cross = 0;
    int nearest = 0x7fffffff;
    for (int i = 0; i < n; i++) {
        if (cl[i].n < CROSSHAIR_SEGS - 3 || cl[i].n > CROSSHAIR_SEGS + 3) continue;
        int w = cl[i].x1 - cl[i].x0, h = cl[i].y1 - cl[i].y0;
        if (w < 11 || w > 20 || h < 11 || h > 20) continue;      /* the crosshair is a 16x16 box; text is wide and short */
        int cx = (cl[i].x0 + cl[i].x1) / 2, cy = (cl[i].y0 + cl[i].y1) / 2;
        int d = prev_ok ? abs(cx - prev_x) + abs(cy - prev_y) : 0;
        if (!have_cross || d < nearest) { cross_x = cx; cross_y = cy; have_cross = 1; nearest = d; }
    }

    /*
     * The trench, and the moment that matters in it. The walls are one green cluster spanning
     * the screen. The exhaust port has no signature of its own - its lines are green and merge
     * into the trench floor - but the game announces it: "EXHAUST PORT AHEAD" is two yellow
     * clusters of about forty segments each, just under the top band. While that banner is up
     * the port is dead ahead at the trench's vanishing point, so that is where to aim and hold
     * the trigger. Missing it is what ended every game so far.
     */
    n = gather(avg, COL_YELLOW, cl, MAX_CLUSTERS);
    memcpy(dbg_yellow, cl, sizeof(cluster_t) * n); dbg_nyellow = n;
    int banner = 0;
    for (int i = 0; i < n; i++)
        if (cl[i].n >= 28 && cl[i].n <= 60 && cl[i].y0 >= 30 && cl[i].y1 <= 70 && cl[i].x0 > 30) banner++;
    port_ahead = banner >= 2;
    n = gather(avg, COL_GREEN, cl, MAX_CLUSTERS);
    in_trench = 0;
    for (int i = 0; i < n; i++)
        if (cl[i].n >= 25 && cl[i].x1 - cl[i].x0 >= 200 && cl[i].y1 - cl[i].y0 >= 150) in_trench = 1;

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
            if (colour == COL_GREEN) {
                /* a TIE is exactly 94 segments, or 188 when two overlap, and never wider than a
                 * hand's breadth; the trench walls are green and span the screen */
                int ok = (c->n >= TIE_SEGS - 6 && c->n <= TIE_SEGS + 6) || (c->n >= 2 * TIE_SEGS - 10 && c->n <= 2 * TIE_SEGS + 10);
                if (!ok || c->x1 - c->x0 > 70 || c->y1 - c->y0 > 70) continue;
            }
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
        /* AP tuning: the yoke is a laggy rate command, so control is proportional-plus-damping
         * on the crosshair-to-target error. The two gains below (6 and 18, over 8) are the
         * whole of the feel: raise the 6 to chase harder, raise the 18 to settle rather than
         * oscillate. Measured signs: yaw 0 = right, 255 = left; pitch 255 = up, 0 = down. */
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
        /*
         * The port. The banner is up for a moment; the port itself comes up the trench a few
         * seconds later, low and centred. So the banner starts a clock, and for a while after
         * it the crosshair sits at the bottom centre with the trigger going - the turrets can
         * have their turn later.
         */
        if (port_ahead) port_seen_us = now_us;
        int port_run = port_seen_us && now_us - port_seen_us < 5000000;

        int ex = 0, ey = 0;
        if (port_run && have_cross)      { ex = CX - cross_x;    ey = PORT_Y - cross_y; }
        else if (have_tgt && have_cross) { ex = tgt_x - cross_x; ey = tgt_y - cross_y; }
        else if (have_cross)             { ex = CX - cross_x;    ey = CY - cross_y; }
        /*
         * Signs, measured from command-then-response across consecutive frames: yaw 0 moves the
         * crosshair right and yaw 255 left; pitch 255 moves it up (smaller y) and pitch 0 down.
         * Both are the opposite of the obvious guess. Note also that the crosshair's travel is
         * limited per phase - in the trench it lives in a box about fifty pixels square - so a
         * controller that is not damped simply bounces between the walls of that box.
         */
        /*
         * The yoke is a velocity command with a few frames of lag, so plain proportional
         * control on position slams the crosshair from one edge to the other and never lands.
         * This is proportional-plus-damping: push toward the target, but back off in
         * proportion to how fast the crosshair is already moving that way. Gains are in
         * eighths to stay in integers.
         */
        int vx = have_last ? cross_x - last_cx : 0;
        int vy = have_last ? cross_y - last_cy : 0;
        int yaw   = 0x80 - (ex * 6 - vx * 18) / 8;
        int pitch = 0x80 - (ey * 6 - vy * 18) / 8;
        if (yaw < 0) yaw = 0;
        if (yaw > 255) yaw = 255;
        if (pitch < 0) pitch = 0;
        if (pitch > 255) pitch = 255;
        in->yaw = (uint8_t)yaw;
        in->pitch = (uint8_t)pitch;
        /* shoot whenever there is something to shoot at and we are roughly on it; the trigger
         * is pulsed, because the game fires on the press, not while held */
        static int trig;
        int on_target = (have_tgt || port_run) && abs(ex) < 30 && abs(ey) < 30;
        in->fire = on_target ? ((trig++ >> 1) & 1) : 0;
        return;
    }
    }
}
