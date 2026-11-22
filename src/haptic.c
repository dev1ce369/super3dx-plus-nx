#include "haptic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* What the arcade's CCamEff (car_state.camfx[0], the shake's strength) did on the road in a test drive: an expansion joint raised it from nothing to
 * about 0.2 (0.22 at 120 km/h, 0.23 at 150), twice 5 frames apart (the front axle, then the rear), every 41 m; a wall hit or a scrape to 0.8-2
 * (the sparks, the change of velocity beside it). So a rise with sparks or a hit is a wall, a rise without is the road. */
static struct {
  int pct[4];
  float prev_amp;
  float wall, joint; /* the pulses' envelopes, 0..1 */
  float scrape;      /* a wall's scrape, smoothed */
  float slip;        /* the slip angle's rumble, smoothed */
  float c[3][2];     /* the three effects (wall, joint, slip), each its heavy and light motor */
} H = { .pct = { 8, 10, 10, 5 } };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void haptic_set_levels(const int pct[4]) {
  for (int i = 0; i < 4; ++i) H.pct[i] = pct[i] < 0 ? 0 : pct[i] > 10 ? 10 : pct[i];
}

void haptic_reset(void) {
  H.prev_amp = H.wall = H.joint = H.scrape = H.slip = 0.0f;
  for (int i = 0; i < 3; ++i) H.c[i][0] = H.c[i][1] = 0.0f;
}

void haptic_step(const car_state *c, int running) {
  if (!running) {
    haptic_reset();
    return;
  }
  const float amp = c->camfx[0];
  const int rise = amp > H.prev_amp * 1.08f + 0.02f;
  const int wall = c->effect.spark > 0.0f || c->impact > 0.0f;
  H.prev_amp = amp;
  const float kmh = fabsf(c->speed) * 3.6f;
  if (rise) {
    if (wall) { /* a hit: the camera's shake is 0.8 and up, the change of velocity (impact) 7 and up */
      float a = clampf(0.35f + 0.25f * amp + c->impact / 60.0f, 0.35f, 1.0f);
      if (a > H.wall) H.wall = a;
    } else { /* a joint (the zebra, the guard rail's strip): the faster, the harder, but only a tap */
      float a = clampf(0.12f + 1.5f * amp, 0.15f, 0.55f);
      if (a > H.joint) H.joint = a;
    }
  }
  H.wall *= 0.88f;  /* about 0.15 s to a tenth */
  H.joint *= 0.70f; /* about 0.06 s */
  /* the scrape along a wall: a buzz while the arcade's sparks are thrown, more with the speed */
  float scrape = c->effect.spark > 0.0f ? 0.25f + 0.35f * clampf(kmh / 150.0f, 0.0f, 1.0f) : 0.0f;
  H.scrape += (scrape - H.scrape) * 0.3f;
  /* the constant one: the body's slip angle (the angle between where the car points and where it goes; 0.06 rad is 3.5 degrees of a car that
   * holds the road, 0.45 a drift) on the heavy motor, only at speed (a car turning on the spot has no slip to feel) */
  float sa = fabsf(c->phys.slip_body);
  float s = clampf((sa - 0.06f) / (0.45f - 0.06f), 0.0f, 1.0f);
  s = s * s * (3.0f - 2.0f * s);
  float g = clampf((fabsf(c->speed) - 6.0f) / 8.0f, 0.0f, 1.0f);
  H.slip += (s * g - H.slip) * 0.2f;

  /* each effect's two motors, before the strengths (they apply in haptic_get, so a change in the menu is felt at once) */
  H.c[0][0] = fmaxf(H.wall, H.scrape), H.c[0][1] = fmaxf(H.wall * 0.7f, H.scrape * 0.6f);
  H.c[1][0] = H.joint * 0.35f, H.c[1][1] = H.joint;
  H.c[2][0] = 0.6f * H.slip, H.c[2][1] = 0.18f * H.slip;
  if (getenv("MT3DX_FXLOG") && (H.wall > 0.01f || H.joint > 0.01f || H.scrape > 0.01f || H.slip > 0.01f)) {
    static unsigned n;
    fprintf(stderr, "rumble %u: wall %.2f joint %.2f scrape %.2f slip %.2f (%.0f km/h, slip angle %.3f)\n", ++n, (double)H.wall, (double)H.joint,
            (double)H.scrape, (double)H.slip, (double)kmh, (double)c->phys.slip_body);
  }
}

void haptic_get(float *low, float *high) {
  const float overall = (float)H.pct[0] / 10.0f;
  float lo = 0.0f, hi = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float k = (float)H.pct[1 + i] / 10.0f; /* the wall's, the joints', the slip's */
    if (i < 2) { /* a hit and a tap on top of one another: the stronger */
      lo = fmaxf(lo, H.c[i][0] * k), hi = fmaxf(hi, H.c[i][1] * k);
    } else { /* the slip's constant rumble is added to what is going on */
      lo += H.c[i][0] * k, hi += H.c[i][1] * k;
    }
  }
  *low = clampf(lo * overall, 0.0f, 1.0f);
  if (*low < 0.03f) *low = 0.0f; /* (the envelopes only fall towards 0: a hair of a rumble would drive a pad's motor at its least for ever) */
  *high = clampf(hi * overall, 0.0f, 1.0f);
  if (*high < 0.03f) *high = 0.0f;
}
