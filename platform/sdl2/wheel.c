#include "wheel.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "bind.h"
#include "ffb.h"

#define MAXDEV 16

static struct {
  wheel_sink sink;
  volatile int rescan;     /* a device came or went (set by the event loop) */
  volatile int instance;   /* the open wheel's instance id, -1 for none */
  int n, index[MAXDEV];    /* the list: the SDL index of each */
  SDL_Joystick *j;
  SDL_Haptic *h;
  char name[128];          /* the open device's name */
  Uint32 caps;             /* SDL_HapticQuery */
  int fx_const, fx_spring, fx_damper, fx_sine;
  SDL_HapticEffect e_const, e_spring, e_damper, e_sine;
  int hat;                 /* the hat's last directions (SDL_HAT_*) */
  /* what was last sent, to send only a change */
  int s_force, s_spring, s_damper, s_vib, s_hz, s_gain;
  int playing;
  /* keeping the forces alive (a wheel's driver can drop them after a while: the T300RS, issue 2) */
  int failed;              /* a force update was refused since the last look */
  Uint32 watch_at;         /* when the effects were last asked whether they still play */
  Uint32 reopened_at;      /* when the device was last opened again by the watchdog */
  Uint32 restart_t0;       /* the window the restarts are counted in */
  int restarts;
  int seen;                /* bit i: force i has been seen playing (a driver that never says so is not asked again) */
  Uint32 reopen_t0;        /* the minute the reopenings are counted in */
  int reopens;
  Uint32 alive_at;         /* when the forces were last sent again as a keepalive */
  Uint32 zero_log;         /* when the steering force's change to or from nothing was last logged */
} W = { .instance = -1, .rescan = 1, .fx_const = -1, .fx_spring = -1, .fx_damper = -1, .fx_sine = -1 };

/* ---- the game devices that feed the bindings (a shifter, a button box, pedals, a wheel's second interface): slot 0 is the chosen wheel, the
 * others the rest of the list in SDL's order; a slot's buttons and axes are bind.h's BP_W0 + slot * BP_PER_DEV.., AXIS_W0 + slot * AXIS_PER_DEV.. ---- */
static struct {
  SDL_Joystick *j[WHEEL_DEVS]; /* the ones this layer opened for the slots (not the chosen wheel's own handle) */
  int inst[WHEEL_DEVS];        /* each slot's instance id, -1 for none */
  int hat[WHEEL_DEVS];
  int last_instance, relists, seen_relists;
} S = { .inst = { -1, -1, -1, -1 }, .last_instance = -2 };

static void slots_close(void) {
  for (int s = 0; s < WHEEL_DEVS; ++s) {
    if (S.j[s]) SDL_JoystickClose(S.j[s]);
    S.j[s] = NULL, S.inst[s] = -1, S.hat[s] = 0;
  }
}

static void slots_refresh(void) {
  slots_close();
  int s = 0;
  if (W.instance >= 0) S.inst[s++] = W.instance; /* (slot 0: the wheel whose forces are chosen) */
  for (int i = 0; i < W.n && s < WHEEL_DEVS; ++i) {
    SDL_Joystick *j = SDL_JoystickOpen(W.index[i]);
    if (!j) continue;
    const int inst = (int)SDL_JoystickInstanceID(j);
    if (inst == W.instance) { /* (the chosen one's own handle is W.j) */
      SDL_JoystickClose(j);
      continue;
    }
    S.j[s] = j, S.inst[s] = inst;
    const int na = SDL_JoystickNumAxes(j);
    for (int a = 0; a < na && a < AXIS_PER_DEV; ++a) W.sink.axis(AXIS_W0 + s * AXIS_PER_DEV + a, SDL_JoystickGetAxis(j, a)); /* (where it rests) */
    fprintf(stderr, "wheel: input device %d: %s (%d axes, %d buttons)\n", s + 1, SDL_JoystickName(j) ? SDL_JoystickName(j) : "?", na, SDL_JoystickNumButtons(j));
    ++s;
  }
}

static int slot_of(int instance) {
  for (int s = 0; s < WHEEL_DEVS; ++s)
    if (S.inst[s] == instance && instance >= 0) return s;
  return -1;
}

void wheel_init(const wheel_sink *sink) { W.sink = *sink; }

int wheel_instance(void) { return W.instance; }

/* ---- the list ---- */

static int is_virtual(int index) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
  return SDL_JoystickIsVirtual(index);
#else
  (void)index;
  return 0;
#endif
}

static void relist(void) {
  const char *names[MAXDEV];
  int haptic[MAXDEV];
  W.n = 0;
  const int count = SDL_NumJoysticks();
  for (int i = 0; i < count && W.n < MAXDEV; ++i) {
    if (is_virtual(i) && !getenv("MT3DX_VWHEEL")) continue;
    const char *name = SDL_JoystickNameForIndex(i);
    SDL_Joystick *j = SDL_JoystickOpen(i); /* (to ask; the same device is shared with the pad layer's open one) */
    int hap = j && SDL_JoystickIsHaptic(j) > 0;
    if (j) SDL_JoystickClose(j);
    if (SDL_IsGameController(i) && !hap) continue; /* a plain pad: the pad layer has it */
    names[W.n] = name ? name : "UNKNOWN";
    haptic[W.n] = hap;
    W.index[W.n++] = i;
  }
  ffb_set_devices(W.n, names, haptic);
  ++S.relists;
}

/* ---- the open device ---- */

static void say(const char *fmt, const char *a, int b, int c) {
  char s[160];
  snprintf(s, sizeof s, fmt, a, b, c);
  ffb_set_status(s);
}

static void fx_zero(void) {
  if (!W.h) return;
  SDL_HapticStopAll(W.h);
  W.playing = 0;
  W.s_force = W.s_spring = W.s_damper = W.s_vib = W.s_hz = 0;
}

static void close_wheel(void) {
  if (W.h) {
    SDL_HapticStopAll(W.h);
    if (W.caps & SDL_HAPTIC_AUTOCENTER) SDL_HapticSetAutocenter(W.h, 100); /* (the driver's own centering back) */
    SDL_HapticClose(W.h);
  }
  if (W.j) SDL_JoystickClose(W.j);
  W.h = NULL, W.j = NULL, W.instance = -1, W.name[0] = 0, W.caps = 0;
  W.fx_const = W.fx_spring = W.fx_damper = W.fx_sine = -1;
  W.playing = 0;
  W.s_force = W.s_spring = W.s_damper = W.s_vib = W.s_hz = 0, W.s_gain = 0; /* (new effects start at nothing, so what is sent next is a change) */
  W.failed = 0, W.seen = 0;
}

static void cartesian(SDL_HapticDirection *d) {
  memset(d, 0, sizeof *d);
  d->type = SDL_HAPTIC_CARTESIAN;
  d->dir[0] = 1;
}

static int make(Uint32 caps, Uint32 bit, SDL_HapticEffect *e) {
  if (!(caps & bit)) return -1;
  return SDL_HapticNewEffect(W.h, e);
}

/* ---- the report: <save dir>/wheel-diag.txt, rewritten each time a wheel is opened (and the same lines on stderr), what a wheel that
 * "has no forces" needs to be understood from: the device's ids, SDL's lists and hints, and what each way of opening it said ---- */
static char diag_dir[1024];
static FILE *D;

void wheel_diag_dir(const char *dir) { snprintf(diag_dir, sizeof diag_dir, "%s", dir ? dir : ""); }

static void dlog(const char *fmt, ...) {
  char s[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s, sizeof s, fmt, ap);
  va_end(ap);
  fprintf(stderr, "wheel: %s\n", s);
  if (D) fprintf(D, "%s\n", s);
}

static void diag_begin(void) {
  char path[1100];
  static int first = 1; /* (the file is the whole session's: later openings, the watchdog's included, are added to it) */
  snprintf(path, sizeof path, "%s/wheel-diag.txt", diag_dir[0] ? diag_dir : ".");
  D = fopen(path, first ? "w" : "a");
  if (!first && D) fprintf(D, "\n---- the wheel was opened again ----\n");
  first = 0;
  SDL_version v;
  SDL_GetVersion(&v);
  dlog("Super3DX+ wheel report; SDL %d.%d.%d on %s; joystick driver hints: RAWINPUT=%s WGI=%s HIDAPI=%s DIRECTINPUT=%s XINPUT=%s", v.major, v.minor,
       v.patch, SDL_GetPlatform(), SDL_GetHint("SDL_JOYSTICK_RAWINPUT") ? SDL_GetHint("SDL_JOYSTICK_RAWINPUT") : "-",
       SDL_GetHint("SDL_JOYSTICK_WGI") ? SDL_GetHint("SDL_JOYSTICK_WGI") : "-", SDL_GetHint("SDL_JOYSTICK_HIDAPI") ? SDL_GetHint("SDL_JOYSTICK_HIDAPI") : "-",
       SDL_GetHint("SDL_DIRECTINPUT_ENABLED") ? SDL_GetHint("SDL_DIRECTINPUT_ENABLED") : "-", SDL_GetHint("SDL_XINPUT_ENABLED") ? SDL_GetHint("SDL_XINPUT_ENABLED") : "-");
  dlog("SDL haptic subsystem: %s", SDL_WasInit(SDL_INIT_HAPTIC) ? "started" : "NOT started (no forces for anything)");
  const int nj = SDL_NumJoysticks();
  dlog("%d joystick(s):", nj);
  for (int i = 0; i < nj; ++i) {
    char guid[40];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
    dlog("  [%d] %s  vid %04x pid %04x  guid %s  %s", i, SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "?", SDL_JoystickGetDeviceVendor(i),
         SDL_JoystickGetDeviceProduct(i), guid, SDL_IsGameController(i) ? "(a game controller)" : "");
  }
  const int nh = SDL_NumHaptics();
  dlog("%d force feedback device(s) in SDL's list (DirectInput's, on Windows)%s", nh, nh <= 0 ? ": none, so the wheel's driver does not offer forces to DirectInput" : ":");
  for (int i = 0; i < nh; ++i) dlog("  haptic [%d] %s", i, SDL_HapticName(i) ? SDL_HapticName(i) : "?");
}

static void diag_end(void) {
  if (D) fclose(D);
  D = NULL;
}

/* SDL's haptic device for the joystick, by name, when the joystick's own link to it fails (a wheel can be opened as a joystick by one of SDL's
 * drivers and listed as a force device by DirectInput under the same name) */
static SDL_Haptic *haptic_by_name(const char *jname) {
  if (!jname) return NULL;
  const int nh = SDL_NumHaptics();
  for (int i = 0; i < nh; ++i) {
    const char *hn = SDL_HapticName(i);
    if (hn && (!strcmp(hn, jname) || strstr(hn, jname) || strstr(jname, hn))) {
      SDL_Haptic *h = SDL_HapticOpen(i);
      dlog("  SDL_HapticOpen(%d, \"%s\"): %s%s%s", i, hn, h ? "opened" : "failed: ", h ? "" : SDL_GetError(), "");
      if (h) return h;
    }
  }
  return NULL;
}

static void open_wheel_inner(int list_i);
static void open_wheel(int list_i) {
  diag_begin();
  open_wheel_inner(list_i);
  diag_end();
}

static void open_wheel_inner(int list_i) {
  close_wheel();
  W.j = SDL_JoystickOpen(W.index[list_i]);
  if (!W.j) {
    say("COULD NOT OPEN IT: %s", SDL_GetError(), 0, 0);
    dlog("SDL_JoystickOpen failed: %s", SDL_GetError());
    return;
  }
  W.instance = (int)SDL_JoystickInstanceID(W.j);
  snprintf(W.name, sizeof W.name, "%s", ffb_device_name(list_i));
  W.hat = 0;
  /* where every axis rests now (a pedal that is not moved sends nothing: the bindings must not take it for the middle of its throw) */
  const int na = SDL_JoystickNumAxes(W.j);
  for (int a = 0; a < na && a < AXIS_PER_DEV; ++a) W.sink.axis(AXIS_W0 + a, SDL_JoystickGetAxis(W.j, a));
  char info[160];
  snprintf(info, sizeof info, "%d AXES, %d BUTTONS", na, SDL_JoystickNumButtons(W.j));
  {
    char guid[40];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(W.j), guid, sizeof guid);
    dlog("opened %s: vid %04x pid %04x, guid %s, %s, %d hats", W.name, SDL_JoystickGetVendor(W.j), SDL_JoystickGetProduct(W.j), guid, info,
         SDL_JoystickNumHats(W.j));
  }
  if (SDL_JoystickIsHaptic(W.j) <= 0) {
    say("%s; NO FORCE FEEDBACK ON IT (SEE wheel-diag.txt IN THE SAVE FOLDER)", info, 0, 0);
    dlog("SDL_JoystickIsHaptic: no (%s): SDL does not see forces on this device", SDL_GetError());
    return;
  }
  W.h = SDL_HapticOpenFromJoystick(W.j);
  char err[96] = "";
  if (!W.h) {
    snprintf(err, sizeof err, "%s", SDL_GetError()); /* (what SDL says, on the screen: the one thing a report needs) */
    dlog("SDL_HapticOpenFromJoystick failed: %s; trying the force feedback list by the joystick's name", err);
    W.h = haptic_by_name(SDL_JoystickName(W.j));
    if (W.h) dlog("opened through the list instead: forces on");
  }
  if (!W.h) {
    snprintf(info + strlen(info), sizeof info - strlen(info), "; FORCES NOT AVAILABLE (%.60s); SEE wheel-diag.txt IN THE SAVE FOLDER", err);
    say("%s", info, 0, 0);
    dlog("no way of opening its forces worked");
    return;
  }
  dlog("force feedback open; features %08x (constant %d, spring %d, damper %d, sine %d, gain %d, autocenter %d); %d effects at once", SDL_HapticQuery(W.h),
       !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_CONSTANT), !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_SPRING), !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_DAMPER),
       !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_SINE), !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_GAIN), !!(SDL_HapticQuery(W.h) & SDL_HAPTIC_AUTOCENTER),
       SDL_HapticNumEffects(W.h));
  W.caps = SDL_HapticQuery(W.h);
  if (W.caps & SDL_HAPTIC_GAIN) SDL_HapticSetGain(W.h, 100);
  if (W.caps & SDL_HAPTIC_AUTOCENTER) SDL_HapticSetAutocenter(W.h, 0); /* (the driver's spring would fight the game's) */

  SDL_HapticEffect *e = &W.e_const;
  memset(e, 0, sizeof *e);
  e->type = SDL_HAPTIC_CONSTANT;
  cartesian(&e->constant.direction);
  e->constant.length = SDL_HAPTIC_INFINITY;
  W.fx_const = make(W.caps, SDL_HAPTIC_CONSTANT, e);

  e = &W.e_spring;
  memset(e, 0, sizeof *e);
  e->type = SDL_HAPTIC_SPRING;
  cartesian(&e->condition.direction);
  e->condition.length = SDL_HAPTIC_INFINITY;
  e->condition.right_sat[0] = e->condition.left_sat[0] = 0xFFFF;
  W.fx_spring = make(W.caps, SDL_HAPTIC_SPRING, e);

  e = &W.e_damper;
  memset(e, 0, sizeof *e);
  e->type = SDL_HAPTIC_DAMPER;
  cartesian(&e->condition.direction);
  e->condition.length = SDL_HAPTIC_INFINITY;
  e->condition.right_sat[0] = e->condition.left_sat[0] = 0xFFFF;
  W.fx_damper = make(W.caps, SDL_HAPTIC_DAMPER, e);

  e = &W.e_sine;
  memset(e, 0, sizeof *e);
  e->type = SDL_HAPTIC_SINE;
  cartesian(&e->periodic.direction);
  e->periodic.length = SDL_HAPTIC_INFINITY;
  e->periodic.period = 30;
  W.fx_sine = make(W.caps, SDL_HAPTIC_SINE, e);

  char what[96] = "";
  if (W.fx_const >= 0) strcat(what, "STEERING ");
  if (W.fx_spring >= 0) strcat(what, "SPRING ");
  if (W.fx_damper >= 0) strcat(what, "DAMPER ");
  if (W.fx_sine >= 0) strcat(what, "VIBRATION ");
  if (!what[0]) {
    snprintf(info + strlen(info), sizeof info - strlen(info), "; NO USABLE FORCE TYPES (SEE wheel-diag.txt)");
    say("%s", info, 0, 0);
    dlog("no usable effect (features %08x): %s", W.caps, SDL_GetError());
    return;
  }
  char s[200];
  snprintf(s, sizeof s, "%s; FORCES: %s", info, what);
  ffb_set_status(s);
  fprintf(stderr, "wheel: %s (%s)\n", W.name, s);
  for (int i = 0; i < 4; ++i) {
    int id = i == 0 ? W.fx_const : i == 1 ? W.fx_spring : i == 2 ? W.fx_damper : W.fx_sine;
    if (id >= 0) SDL_HapticRunEffect(W.h, id, 1);
  }
  W.playing = 1;
}

/* the settings' device against the open one */
static void sync_device_inner(void) {
  if (W.rescan) {
    W.rescan = 0;
    relist();
    if (W.instance >= 0) { /* (still plugged in? the list says) */
      int there = 0;
      for (int i = 0; i < W.n; ++i) there |= !strcmp(ffb_device_name(i), W.name);
      if (!there) close_wheel();
    }
  }
  const char *want = ffb_selected_name();
  if (!want) {
    if (W.j) close_wheel();
    return;
  }
  if (W.j && !strcmp(want, W.name)) return;
  for (int i = 0; i < W.n; ++i)
    if (!strcmp(ffb_device_name(i), want)) {
      open_wheel(i);
      return;
    }
}

static void sync_device(void) {
  sync_device_inner();
  if (W.instance != S.last_instance || S.relists != S.seen_relists) { /* (the chosen wheel or the list changed: the input slots again) */
    S.last_instance = W.instance, S.seen_relists = S.relists;
    slots_refresh();
  }
}

static Sint16 s16(float v) { return (Sint16)(fmaxf(-1.0f, fminf(1.0f, v)) * 32767.0f); }

/* a line in the report (wheel-diag.txt, added to) for something that happened while playing */
static void rlog(const char *fmt, ...) {
  char s[300], path[1100];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s, sizeof s, fmt, ap);
  va_end(ap);
  fprintf(stderr, "wheel: %s\n", s);
  snprintf(path, sizeof path, "%s/wheel-diag.txt", diag_dir[0] ? diag_dir : ".");
  FILE *f = fopen(path, "a");
  if (f) fprintf(f, "[%u s] %s\n", (unsigned)(SDL_GetTicks() / 1000), s), fclose(f);
}

static void effect_update(int id, SDL_HapticEffect *e) {
  if (id >= 0 && SDL_HapticUpdateEffect(W.h, id, e) != 0) {
    static Uint32 last_log;
    if (SDL_GetTicks() - last_log > 2000) last_log = SDL_GetTicks(), rlog("a force update was refused: %s", SDL_GetError());
    W.failed = 1;
  }
}

/* the device again, as turning the wheel off and on in the settings does (at most once a second, and not for ever: the reason is logged) */
static void reopen(const char *why) {
  const Uint32 now = SDL_GetTicks();
  if (now - W.reopened_at < 1000) return;
  W.reopened_at = now;
  if (now - W.reopen_t0 > 60000) W.reopen_t0 = now, W.reopens = 0;
  if (++W.reopens > 5) { /* (it is not helping: leave it to the settings, and say so once) */
    if (W.reopens == 6) rlog("%s: the wheel was opened again 5 times in a minute and keeps failing; not trying any more until it is chosen again", why);
    W.failed = 0;
    return;
  }
  char name[sizeof W.name];
  snprintf(name, sizeof name, "%s", W.name);
  for (int i = 0; i < W.n; ++i)
    if (!strcmp(ffb_device_name(i), name)) {
      rlog("%s: opening %s again", why, name);
      open_wheel(i);
      return;
    }
}

/* every half second: do the forces still play? An infinite effect that is not running was dropped by the driver: start it again, and if that keeps
 * happening (or a force update is refused) open the device again */
static void watchdog(void) {
  const Uint32 now = SDL_GetTicks();
  if (now - W.watch_at < 500) return;
  W.watch_at = now;
  if (W.failed) {
    W.failed = 0;
    reopen("a force update failed");
    return;
  }
  if (!(W.caps & SDL_HAPTIC_STATUS)) return;
  const int ids[4] = { W.fx_const, W.fx_spring, W.fx_damper, W.fx_sine };
  for (int i = 0; i < 4; ++i) {
    if (ids[i] < 0) continue;
    const int st = SDL_HapticGetEffectStatus(W.h, ids[i]);
    if (st == 1) W.seen |= 1 << i;
    if (st == 0 && (W.seen & (1 << i))) { /* (1 playing, -1 the driver cannot say; one never seen playing is not trusted to say) */
      if (now - W.restart_t0 > 10000) W.restart_t0 = now, W.restarts = 0;
      ++W.restarts;
      rlog("force %d had stopped (%d in 10 s): starting it again", i, W.restarts);
      if (SDL_HapticRunEffect(W.h, ids[i], 1) != 0) rlog("  it would not start: %s", SDL_GetError());
      if (W.restarts >= 3) {
        reopen("the forces keep stopping");
        return;
      }
    }
  }
}

void wheel_frame(void) {
  sync_device();
  if (!W.h || !W.playing) return;
  watchdog();
  if (!W.h || !W.playing) return; /* (it may have been opened again) */
  ffb_out o;
  app_ffb(&o);
  if (!o.on) {
    if (W.s_force || W.s_spring || W.s_damper || W.s_vib) { /* (switched off: the forces go to nothing, the effects keep running) */
      memset(&o, 0, sizeof o);
      o.gain = 1.0f;
    } else return;
  }
  const int force = (int)(o.force * 32767.0f), spring = (int)(o.spring * 32767.0f), damper = (int)(o.damper * 32767.0f), vib = (int)(o.vib * 32767.0f);
  const int hz = (int)o.vib_hz, gain = (int)(o.gain * 100.0f);
  const int FORCE_STEP = 330; /* 1 %: a change smaller than this is not sent (a wheel's bus is slow) */
  if (gain != W.s_gain && (W.caps & SDL_HAPTIC_GAIN)) SDL_HapticSetGain(W.h, gain < 1 ? 1 : gain), W.s_gain = gain;
  /* a keepalive (issue 2, a T300RS whose force went away while the driver still said it played): every half second the steering force is sent
   * again and every effect is started again, and the steering force is never exactly 0 (a driver may park an effect at 0 and not wake it again) */
  const Uint32 now = SDL_GetTicks();
  const int alive = now - W.alive_at >= 500;
  if (alive) {
    W.alive_at = now;
    const int ids[4] = { W.fx_const, W.fx_spring, W.fx_damper, W.fx_sine };
    for (int i = 1; i < 4; ++i)
      if (ids[i] >= 0) SDL_HapticRunEffect(W.h, ids[i], 1);
    if (W.fx_const >= 0) SDL_HapticRunEffect(W.h, W.fx_const, 1);
  }
  if ((force == 0) != (W.s_force == 0)) { /* (what the driver is given at the changes to and from nothing, for the report) */
    if (now - W.zero_log > 3000) W.zero_log = now, rlog("steering force %s", force == 0 ? "to nothing (sent as the smallest force)" : "back");
  }
  if (alive || abs(force - W.s_force) >= FORCE_STEP || (force == 0) != (W.s_force == 0)) {
    W.e_const.constant.level = force == 0 ? 1 : s16(o.force);
    effect_update(W.fx_const, &W.e_const);
    W.s_force = force;
  }
  if (abs(spring - W.s_spring) >= FORCE_STEP || (spring == 0) != (W.s_spring == 0)) {
    W.e_spring.condition.right_coeff[0] = W.e_spring.condition.left_coeff[0] = s16(o.spring);
    effect_update(W.fx_spring, &W.e_spring);
    W.s_spring = spring;
  }
  if (abs(damper - W.s_damper) >= FORCE_STEP || (damper == 0) != (W.s_damper == 0)) {
    W.e_damper.condition.right_coeff[0] = W.e_damper.condition.left_coeff[0] = s16(o.damper);
    effect_update(W.fx_damper, &W.e_damper);
    W.s_damper = damper;
  }
  if (abs(vib - W.s_vib) >= FORCE_STEP || hz != W.s_hz || (vib == 0) != (W.s_vib == 0)) {
    W.e_sine.periodic.magnitude = s16(o.vib);
    W.e_sine.periodic.period = (Uint16)(hz > 0 ? 1000 / hz : 30);
    effect_update(W.fx_sine, &W.e_sine);
    W.s_vib = vib, W.s_hz = hz;
  }
}

void wheel_quit(void) {
  close_wheel();
  slots_close();
}

/* ---- events ---- */

void wheel_event(const SDL_Event *e) {
  switch (e->type) {
  case SDL_JOYDEVICEADDED:
  case SDL_JOYDEVICEREMOVED: W.rescan = 1; break;
  case SDL_JOYAXISMOTION: {
    const int s = slot_of(e->jaxis.which);
    if (s >= 0 && e->jaxis.axis < AXIS_PER_DEV) W.sink.axis(AXIS_W0 + s * AXIS_PER_DEV + e->jaxis.axis, e->jaxis.value);
    break;
  }
  case SDL_JOYBUTTONDOWN:
  case SDL_JOYBUTTONUP: {
    const int s = slot_of(e->jbutton.which);
    if (s >= 0 && e->jbutton.button < BP_PER_DEV) W.sink.button(BP_W0 + s * BP_PER_DEV + e->jbutton.button, e->type == SDL_JOYBUTTONDOWN);
    break;
  }
  case SDL_JOYHATMOTION: {
    const int s = slot_of(e->jhat.which);
    if (s >= 0 && e->jhat.hat == 0) { /* the first hat of a device: the D-pad */
      static const struct { int dir, code; } H[4] = { { SDL_HAT_UP, BP_DUP }, { SDL_HAT_DOWN, BP_DDOWN }, { SDL_HAT_LEFT, BP_DLEFT }, { SDL_HAT_RIGHT, BP_DRIGHT } };
      for (int i = 0; i < 4; ++i)
        if ((S.hat[s] & H[i].dir) != (e->jhat.value & H[i].dir)) W.sink.button(H[i].code, (e->jhat.value & H[i].dir) != 0);
      S.hat[s] = e->jhat.value;
    }
    break;
  }
  }
}
