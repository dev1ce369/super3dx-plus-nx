/* A steering wheel through SDL's joystick and haptic layers (docs/INPUT.md, "Force feedback"). Wheels are mostly not game controllers (SDL has
 * no button layout for them), so the chosen device is read as a raw joystick: its first eight axes are bind.h's AXIS_W0.. (bound in KEY BINDINGS,
 * AXES), its buttons BP_W0.., its hat the D-pad; and its forces (ffb.h) are played through SDL_Haptic. */
#ifndef WHEEL_H
#define WHEEL_H

#include <SDL.h>

typedef struct {
  void (*axis)(int axis, int v);     /* bind.h AXIS_*, SDL's -32768..32767 */
  void (*button)(int code, int down); /* bind.h BP_* */
} wheel_sink;

void wheel_init(const wheel_sink *sink);
/* the folder the wheel report (wheel-diag.txt: the device, SDL's lists, what opening its forces said) is written to: the save folder */
void wheel_diag_dir(const char *dir);
/* from the event loop (any thread that pumps events): the joystick events */
void wheel_event(const SDL_Event *e);
/* the chosen wheel's instance id, -1 for none (its game controller events are ignored: the raw ones are the wheel's) */
int wheel_instance(void);
/* once a frame, on the game's thread: lists the devices, opens the chosen one, plays the forces */
void wheel_frame(void);
/* stops every force and lets the device go (before SDL_Quit: a force left running would pull the wheel for ever) */
void wheel_quit(void);

#endif
