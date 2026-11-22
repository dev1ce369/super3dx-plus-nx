/* Controller vibration for the player's car: what the car's sim and the arcade's own road-effect requests say, as two rumble motors' strengths.
 *   - a wall hit: a heavy pulse on both motors (strength from the arcade's camera-shake request and the hit's change of velocity), and a
 *     lighter buzz for as long as the arcade's scrape (sparks) lasts;
 *   - an expansion joint (and the zebra / guard-rail strip): a short sharp tap on the light motor, the car's front axle then its rear, as the
 *     arcade's CMachine::Set_SE_Effect_Reactive asks them (CheckGap, CheckZebra) from the road's area data;
 *   - a constant rumble on the heavy motor that grows with the body's slip angle.
 * The platform layer reads haptic_get() once a frame and sends it to the pad. */
#ifndef HAPTIC_H
#define HAPTIC_H

#include "car.h"

/* the strengths in tenths, 0..10: overall, wall hits, expansion joints, tyre slip */
void haptic_set_levels(const int pct[4]);
/* forget everything (a race starts, the pause, the menus) */
void haptic_reset(void);
/* one arcade frame of the player's car, after car_update; running = the race is on (not the countdown, the pause or the end) */
void haptic_step(const car_state *c, int running);
/* the motors now, 0..1: the heavy (low frequency) and the light (high frequency) */
void haptic_get(float *low, float *high);

#endif
