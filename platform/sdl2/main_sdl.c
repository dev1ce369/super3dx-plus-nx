/* The SDL2 side of the port: a desktop window (Linux today, the Raspberry Pi next), the same job main_android.c does
 * on a phone. The main thread owns the window and pumps events; the game runs on a thread of its own (its stack big
 * enough for the recompiled physics) with the GLES context, the loading thread has a context sharing it on a hidden
 * second window (an EGL surface can be current to one context only), and the sound goes out through SDL's audio
 * callback into sound.c's mixer.
 *
 * Milestone 1 (this file): picture, sound effects, the mouse as the touch screen (left button = a finger) and the keyboard.
 * Race: arrows steer, accelerate and brake; Z/X shift down/up; C camera; R reset; T transmission; P pause.
 * Enter is the start button; the menus speak touch and keep to the mouse. Esc quits.
 * The music is sound/bgm/<name>.ogg (platform/sdl2/bgm.c); the attract loop's movies are data/*.wmv (platform/sdl2/movie.c).
 * Not yet: keyboard or pad navigation of the menus (they are touch screens
 * underneath: see docs/ARCHITECTURE.md).
 *
 *   main_sdl <data dir> [--save DIR] [--size WxH] [--fullscreen]
 */
#include <SDL.h>
#include <SDL_thread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <shellapi.h>
#endif

#include "app.h"
#include "netdiag.h"
#include "bind.h"
#include "bgm.h"
#include "gl.h"
#include "hud.h" /* TOUCH_BEGAN, TOUCH_MOVED, TOUCH_ENDED */
#include "menu.h" /* MENU_ACT_* */
#include "movie.h"
#include "sound.h"
#include "wheel.h"

#define LAYOUT_W 1024 /* the game's layout: 1024x768, which the framebuffer is here too */
#define LAYOUT_H 768
#define EV_MAX 512

/* kind 3 a pad axis (phase the axis, x its value), 4 the readout switch; kind 0 touch, 1 start button, 2 a bound input (phase BIND_KEY or BIND_PAD, code, flags: 1 down, 2 repeat) */
typedef struct { int kind, phase; float x, y; int code, flags; } input_ev;

static struct {
  SDL_Window *win, *load_win;
  SDL_GLContext ctx, load_ctx;
  SDL_Thread *game;
  SDL_mutex *mx;
  SDL_AudioDeviceID audio;
  input_ev ev[EV_MAX];
  int n_ev, quit, win_w, win_h, exit_req;
  int angle; /* --angle: skip the driver's own OpenGL ES and use the bundled ANGLE (Direct3D 11) */
  int native_gl, via_egl; /* --native-gl: keep the driver's own OpenGL ES on AMD graphics (they get ANGLE by default); via_egl: the window came through EGL (ANGLE) already */
  int hud_touch; /* --hud touch: the race with the iPad's touch keys (hud_set_style) */
  char log_file[1200]; /* the log this run writes (scrubbed at a clean exit) */
  volatile int cursor_hide; /* the game is in a race: the event thread hides the mouse pointer */
  volatile int fs_want, fs_now; /* the window mode the app wants (0 windowed, 1 fullscreen) and the one the window is in: the event thread switches it */
  int fs_cli;                    /* --fullscreen on the command line */
  volatile int resized; /* the window changed size: the game thread tells the app (app_resize) between frames */
  char data_dir[1024], save_dir[1024];
  const char *bgm;
  int fail;
  volatile int game_done;
} P;

static void pad_rumble_frame(void);

#ifdef WMMT_WIN
int gl_load(void); /* build.py's generated lookup of the GL functions */
#endif
#ifdef WMMT_WIN
/* Laptops with two GPUs: the drivers' own switch (NVIDIA Optimus, AMD PowerXpress) reads these two exports and runs the game on the fast card instead
 * of the integrated one (issue 28). Windows' Settings > System > Display > Graphics can still override it per app. */
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif
#ifdef _WIN32
#define setenv(k, v, overwrite) ((overwrite) || !getenv(k) ? _putenv_s(k, v) : 0)
#endif

/* started when the player begins to quit: six seconds on, whatever still hangs (the audio device, a driver) is cut off */
static int exit_watchdog(void *arg) {
  (void)arg;
  SDL_Delay(6000);
  fprintf(stderr, "quitting took too long: leaving anyway\n");
  _exit(1);
  return 0;
}

static double now_s(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

static void push_input(int bkind, int code, int flags) {
  SDL_LockMutex(P.mx);
  if (P.n_ev < EV_MAX) P.ev[P.n_ev++] = (input_ev){ 2, bkind, 0, 0, code, flags };
  SDL_UnlockMutex(P.mx);
}

static void push_ev(int kind, int phase, float x, float y) {
  if (getenv("MT3DX_LOG")) fprintf(stderr, "input: %s phase %d at %.0f %.0f\n", kind ? "start button" : "touch", phase, x, y);
  SDL_LockMutex(P.mx);
  if (P.n_ev < EV_MAX) P.ev[P.n_ev++] = (input_ev){ kind, phase, x, y, 0, 0 };
  SDL_UnlockMutex(P.mx);
}

/* ---- sound: sound.c's mix, 44.1 kHz stereo int16 ---- */

static void audio_cb(void *user, Uint8 *stream, int len) {
  (void)user;
  snd_mix((short *)stream, len / 4);
  bgm_mix((short *)stream, len / 4);
  movie_mix((short *)stream, len / 4);
  if (getenv("MT3DX_AUDIO_DUMP")) { /* debugging: everything the mixer produced, raw 44.1 kHz stereo int16 */
    static FILE *dump;
    if (!dump) dump = fopen(getenv("MT3DX_AUDIO_DUMP"), "wb");
    if (dump) { fwrite(stream, 1, (size_t)len, dump); fflush(dump); }
  }
  if (getenv("MT3DX_LOG")) { /* once a second: the loudest sample the mixer produced */
    static int peak, calls;
    static double last, gap;
    short *s = (short *)stream;
    double t = now_s();
    if (last > 0 && t - last > gap) gap = t - last;
    last = t;
    for (int i = 0; i < len / 2; ++i) if (abs(s[i]) > peak) peak = abs(s[i]);
    if (++calls >= 2 * 44100 / (len / 4)) {
      fprintf(stderr, "audio: peak %d, longest wait between callbacks %.1f ms (buffer %.1f ms)\n", peak, gap * 1000, len / 4 * 1000.0 / 44100);
      peak = calls = 0;
      gap = 0;
    }
  }
}

static void audio_open(void) {
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = SND_RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = getenv("MT3DX_AUDIO_BUF") ? atoi(getenv("MT3DX_AUDIO_BUF")) : 2048; /* slack for a busy CPU */
  want.callback = audio_cb;
  P.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0); /* (no changes allowed: the mixer's format is fixed) */
  if (!P.audio) {
    fprintf(stderr, "no audio: %s\n", SDL_GetError());
    return;
  }
  fprintf(stderr, "audio: %d Hz, %d ch, %d frames per buffer\n", have.freq, have.channels, have.samples);
  SDL_PauseAudioDevice(P.audio, 0);
}

/* ---- the loading thread: a context sharing the game's, current on the hidden window ---- */

static int loader(void *arg) {
  (void)arg;
  if (SDL_GL_MakeCurrent(P.load_win, P.load_ctx) != 0) {
    fprintf(stderr, "loading thread: no context (%s)\n", SDL_GetError());
    return 0;
  }
  app_loader();
  return 0;
}

/* ---- the controls: a keyboard and pads, through the bindings (src/bind.h, docs/INPUT.md) ----
 * The platform only says which key or pad input went down or up (BIND_KEY with the SDL keycode, BIND_PAD with a BP_* code: the buttons,
 * the left stick's four directions and the two triggers as buttons); the bindings (the defaults, or the player's from the binding
 * screen) say what it does in the menus or in a race. Esc quits (while the binding screen waits for an input it cancels instead). */
static const char *key_name(int code) { return SDL_GetKeyName((SDL_Keycode)code); }

/* ---- test hooks: scripted screenshots and taps (the frame is the game's own buffer, nothing of the desktop) ---- */

/* MT3DX_SHOT=<s>[,<s>...] [MT3DX_SHOT_DIR=dir]: the frame as shot_<s>.ppm at those seconds of running */
static void shot_check(double t) {
  static int init, n;
  static int done[16];
  static double at[16];
  if (!init) {
    init = 1;
    for (const char *s = getenv("MT3DX_SHOT"); s && *s && n < 16; s = strchr(s, ',') ? strchr(s, ',') + 1 : NULL) at[n++] = atof(s);
  }
  { /* MT3DX_SHOT_BURST=<s>:<n>: n frames in a row from second s, as burst_<k>.ppm (how a race's motion looks frame to frame) */
    static double b0 = -2;
    static int bn, bk;
    if (b0 == -2) {
      b0 = -1;
      if (getenv("MT3DX_SHOT_BURST")) sscanf(getenv("MT3DX_SHOT_BURST"), "%lf:%d", &b0, &bn);
    }
    if (b0 >= 0 && t >= b0 && bk < bn) {
      const char *dir = getenv("MT3DX_SHOT_DIR");
      char path[1200];
      snprintf(path, sizeof path, "%s/burst_%03d.ppm", dir ? dir : ".", bk++);
      int w = P.win_w, h = P.win_h;
      unsigned char *px = malloc((size_t)w * h * 4), *rgb = malloc((size_t)w * h * 3);
      if (px && rgb) {
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) memcpy(rgb + ((size_t)(h - 1 - y) * w + x) * 3, px + ((size_t)y * w + x) * 4, 3);
        FILE *f = fopen(path, "wb");
        if (f) fprintf(f, "P6\n%d %d\n255\n", w, h), fwrite(rgb, 1, (size_t)w * h * 3, f), fclose(f);
      }
      free(px);
      free(rgb);
    }
  }
  for (int i = 0; i < n; ++i) {
    if (done[i] || t < at[i]) continue;
    done[i] = 1;
    const char *dir = getenv("MT3DX_SHOT_DIR");
    char path[1200];
    snprintf(path, sizeof path, "%s/shot_%03d.ppm", dir ? dir : ".", (int)at[i]);
    int w = P.win_w, h = P.win_h;
    unsigned char *px = malloc((size_t)w * h * 4), *rgb = malloc((size_t)w * h * 3);
    if (!px || !rgb) { free(px); free(rgb); continue; }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) memcpy(rgb + ((size_t)(h - 1 - y) * w + x) * 3, px + ((size_t)y * w + x) * 4, 3);
    FILE *f = fopen(path, "wb");
    if (f) {
      fprintf(f, "P6\n%d %d\n255\n", w, h);
      fwrite(rgb, 1, (size_t)w * h * 3, f);
      fclose(f);
      fprintf(stderr, "shot %s (screen %d)\n", path, app_screen());
    }
    free(px);
    free(rgb);
  }
}

/* MT3DX_TAP="<s>:<x>,<y>;<s>:<x>,<y>...": a tap (1024 x 768 px, the touch screen's) at those seconds of running */
static void tap_check(double t) {
  static int init, n;
  static struct { double at; float x, y; int stage; } tp[32];
  if (!init) {
    init = 1;
    for (const char *s = getenv("MT3DX_TAP"); s && *s && n < 32; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL) {
      double a;
      float x, y;
      if (sscanf(s, "%lf:%f,%f", &a, &x, &y) == 3) tp[n].at = a, tp[n].x = x, tp[n].y = y, ++n;
    }
  }
  for (int i = 0; i < n; ++i) {
    if (tp[i].stage == 0 && t >= tp[i].at) {
      tp[i].stage = 1;
      fprintf(stderr, "tap %.0f,%.0f at %.1f s (screen %d)\n", tp[i].x, tp[i].y, t, app_screen());
      app_touch((void *)(intptr_t)1, TOUCH_BEGAN, tp[i].x, tp[i].y);
    } else if (tp[i].stage == 1 && t >= tp[i].at + 0.15) {
      tp[i].stage = 2;
      app_touch((void *)(intptr_t)1, TOUCH_ENDED, tp[i].x, tp[i].y);
    }
  }
}

/* MT3DX_SCRIPT="<screen>:<step>[+];...": steps by screen, not by time. A step is a tap "<x>,<y>" (1024 x 768 px, the touch screen's) or a menu action
 * "@OK" (@LEFT @RIGHT @UP @DOWN @OK @BACK @VIEW: what a keyboard or a pad presses, docs/INPUT.md). It waits until the game is
 * on its screen (app_screen) and does it once; with a + it repeats every 1.5 s until the screen changes. Screen 0 is any menu
 * screen, repeated every 2 s until the race (-1). Steps go in order, a second apart at least. */
static void script_check(double t) {
  static int init, n, i, phase; /* phase: 0 waiting, 1 touch down sent, 2 repeating */
  static struct { int screen, repeat, act; float x, y; } st[96];
  static double last;
  if (!init) {
    init = 1;
    static const char *const NAMES[] = { "LEFT", "RIGHT", "UP", "DOWN", "OK", "BACK", "VIEW", "PAUSE" }; /* (PAUSE: the race's pause key) */
    for (const char *s = getenv("MT3DX_SCRIPT"); s && *s && n < 96; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL) {
      int sc;
      const char *c = strchr(s, ':');
      if (sscanf(s, "%d:", &sc) != 1 || !c) continue;
      const char *semi = strchr(s, ';'), *plus = strchr(s, '+');
      st[n].screen = sc, st[n].act = -1;
      st[n].repeat = plus && (!semi || plus < semi);
      if (c[1] == '@') {
        for (int k = 0; k < 8; ++k)
          if (!strncmp(c + 2, NAMES[k], strlen(NAMES[k]))) st[n].act = k;
        if (st[n].act < 0) continue;
      } else if (sscanf(c + 1, "%f,%f", &st[n].x, &st[n].y) != 2) {
        continue;
      }
      ++n;
    }
  }
  if (i >= n) return;
  if (phase == 1) { /* let go, a frame after */
    app_touch((void *)(intptr_t)1, TOUCH_ENDED, st[i].x, st[i].y);
    phase = st[i].repeat ? 2 : 0;
    if (!st[i].repeat && st[i].screen != 0) ++i; /* (the wildcard step goes on until the race) */
    return;
  }
  int s = app_screen();
  if (st[i].screen == 0 && phase == 0) { /* screen 0: any menu screen: every 2 s until the race (screen -1) */
    if (s == -1) {
      ++i;
      return;
    }
    if (t - last >= 2.0) {
      fprintf(stderr, "script step %d: %s on screen %d at %.1f s (until the race)\n", i, st[i].act >= 0 ? "action" : "tap", s, t);
      if (st[i].act == 7) app_key(APP_KEY_PAUSE, 1), app_key(APP_KEY_PAUSE, 0);
      else if (st[i].act >= 0) app_menu_action(st[i].act);
      else app_touch((void *)(intptr_t)1, TOUCH_BEGAN, st[i].x, st[i].y), phase = 1;
      last = t;
    }
    return;
  }
  if (phase == 2 && s != st[i].screen) { /* a repeating step ends when its screen does */
    phase = 0;
    ++i;
    return;
  }
  if (i < n && s == st[i].screen && t - last >= (phase == 2 ? 1.5 : 1.0)) {
    if (st[i].act >= 0) {
      fprintf(stderr, "script step %d: action %d on screen %d at %.1f s\n", i, st[i].act, s, t);
      if (st[i].act == 7) app_key(APP_KEY_PAUSE, 1), app_key(APP_KEY_PAUSE, 0);
      else app_menu_action(st[i].act);
      last = t;
      if (st[i].repeat) phase = 2;
      else ++i;
    } else {
      fprintf(stderr, "script step %d: tap %.0f,%.0f on screen %d at %.1f s\n", i, st[i].x, st[i].y, s, t);
      app_touch((void *)(intptr_t)1, TOUCH_BEGAN, st[i].x, st[i].y);
      last = t;
      phase = 1;
    }
  }
}
static void fit_window(int ww, int wh, int *fw, int *fh);

/* ---- the game thread ---- */

/* ---- the CRT output (--crt): the game drawn 640x480 into a texture, then onto the window (a 480i composite frame) inside a safe area
 * with a vertical flicker filter. A television's overscan hides the edge of the frame, and a one-pixel line in an interlaced frame
 * shows in one field only (the menus' flicker): the pass takes both away. Settings in crt.txt (next to where the player is run,
 * or MT3DX_CRT_CFG): safe_x, safe_y (the part of the frame the picture covers, 0.5 .. 1), flicker (0 off .. 1 full: each line
 * with a quarter of the lines above and below it), width, height (the game's drawing size). */
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
static struct {
  int on, w, h;
  float sx, sy, flicker;
  GLuint fbo, tex, rb, prog, vbo;
  GLint u_rect, u_dy, u_f, a_p;
} CRT = { 0, 640, 480, 0.92f, 0.92f, 0.5f };

static void crt_config(void) {
  const char *path = getenv("MT3DX_CRT_CFG") ? getenv("MT3DX_CRT_CFG") : "crt.txt";
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[128], key[64];
  float v;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || sscanf(line, "%63s %f", key, &v) != 2) continue;
    if (!strcmp(key, "safe_x")) CRT.sx = v;
    else if (!strcmp(key, "safe_y")) CRT.sy = v;
    else if (!strcmp(key, "flicker")) CRT.flicker = v;
    else if (!strcmp(key, "width")) CRT.w = (int)v;
    else if (!strcmp(key, "height")) CRT.h = (int)v;
  }
  fclose(f);
  if (CRT.sx < 0.5f) CRT.sx = 0.5f;
  if (CRT.sx > 1.0f) CRT.sx = 1.0f;
  if (CRT.sy < 0.5f) CRT.sy = 0.5f;
  if (CRT.sy > 1.0f) CRT.sy = 1.0f;
  if (CRT.flicker < 0.0f) CRT.flicker = 0.0f;
  if (CRT.flicker > 1.0f) CRT.flicker = 1.0f;
  if (CRT.w < 160) CRT.w = 160;
  if (CRT.h < 120) CRT.h = 120;
  fprintf(stderr, "crt: %dx%d, safe %.3f x %.3f, flicker %.2f (%s)\n", CRT.w, CRT.h, CRT.sx, CRT.sy, CRT.flicker, path);
}

static GLuint crt_shader(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    fprintf(stderr, "crt shader: %s\n", log);
  }
  return sh;
}

/* the texture the game draws into (the game thread, context current) */
static void crt_init(void) {
  glGenTextures(1, &CRT.tex);
  glBindTexture(GL_TEXTURE_2D, CRT.tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CRT.w, CRT.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenRenderbuffers(1, &CRT.rb);
  glBindRenderbuffer(GL_RENDERBUFFER, CRT.rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, CRT.w, CRT.h);
  glGenFramebuffers(1, &CRT.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, CRT.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, CRT.tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, CRT.rb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, CRT.rb);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "crt: the framebuffer is incomplete\n");
  GLuint vs = crt_shader(GL_VERTEX_SHADER,
    "attribute vec2 p; uniform vec4 rect; varying vec2 uv;\n"
    "void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(mix(rect.xy, rect.zw, uv), 0.0, 1.0); }\n");
  GLuint fs = crt_shader(GL_FRAGMENT_SHADER,
    "precision mediump float; uniform sampler2D t; uniform float dy, f; varying vec2 uv;\n"
    "void main() {\n"
    "  vec3 c = texture2D(t, uv).rgb;\n"
    "  vec3 u = texture2D(t, uv + vec2(0.0, dy)).rgb, d = texture2D(t, uv - vec2(0.0, dy)).rgb;\n"
    "  gl_FragColor = vec4(c * (1.0 - 0.5 * f) + (u + d) * 0.25 * f, 1.0);\n"
    "}\n");
  CRT.prog = glCreateProgram();
  glAttachShader(CRT.prog, vs);
  glAttachShader(CRT.prog, fs);
  glBindAttribLocation(CRT.prog, 0, "p");
  glLinkProgram(CRT.prog);
  CRT.u_rect = glGetUniformLocation(CRT.prog, "rect");
  CRT.u_dy = glGetUniformLocation(CRT.prog, "dy");
  CRT.u_f = glGetUniformLocation(CRT.prog, "f");
  static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
  glGenBuffers(1, &CRT.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, CRT.vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* the game's texture onto the window's frame (the game's GL state is put back after) */
static void crt_present(int win_w, int win_h) {
  GLint prog, abuf, vao, tex, vp[4], act;
  GLboolean blend, depth, stencil, cull, scissor, dither;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
  glGetIntegerv(GL_VIEWPORT, vp);
  blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
  cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST), dither = glIsEnabled(GL_DITHER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, win_w, win_h);
  glDisable(GL_BLEND), glDisable(GL_DEPTH_TEST), glDisable(GL_STENCIL_TEST), glDisable(GL_CULL_FACE), glDisable(GL_SCISSOR_TEST), glDisable(GL_DITHER);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  /* the picture: sx x sy of the frame, centred, in clip space */
  float x0 = -CRT.sx, x1 = CRT.sx, y0 = -CRT.sy, y1 = CRT.sy;
  glBindVertexArrayOES(0);
  glUseProgram(CRT.prog);
  glUniform4f(CRT.u_rect, x0, y0, x1, y1);
  glUniform1f(CRT.u_dy, 1.0f / ((float)win_h * CRT.sy));
  glUniform1f(CRT.u_f, CRT.flicker);
  glBindTexture(GL_TEXTURE_2D, CRT.tex);
  glBindBuffer(GL_ARRAY_BUFFER, CRT.vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glActiveTexture((GLenum)act);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
  glBindVertexArrayOES((GLuint)vao);
  glUseProgram((GLuint)prog);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (blend) glEnable(GL_BLEND);
  if (depth) glEnable(GL_DEPTH_TEST);
  if (stencil) glEnable(GL_STENCIL_TEST);
  if (cull) glEnable(GL_CULL_FACE);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  if (dither) glEnable(GL_DITHER);
}


/* The BLACK LEVEL option (the PC player): the frame drawn into a texture of the drawing's size and put on the window through a
 * levels curve, black point b up and white point 1 - 1.25 b, so a display or driver that lifts the blacks (a limited range
 * RGB signal on a full range monitor, an Auto HDR or colour "enhancement" setting) can be undone from inside the game. Level 5
 * is the 16..235 of a limited range signal. Off (0): the game draws straight to the window and none of this runs. */
static struct {
  int w, h;
  GLuint fbo, tex, rb, prog, vbo;
  GLint u_k;
} LV;

static void lv_ensure(int w, int h) {
  if (!LV.fbo) {
    glGenTextures(1, &LV.tex);
    glGenRenderbuffers(1, &LV.rb);
    glGenFramebuffers(1, &LV.fbo);
    GLuint vs = crt_shader(GL_VERTEX_SHADER,
      "attribute vec2 p; varying vec2 uv;\n"
      "void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n");
    GLuint fs = crt_shader(GL_FRAGMENT_SHADER,
      "precision mediump float; uniform sampler2D t; uniform vec2 k; varying vec2 uv;\n"
      "void main() { gl_FragColor = vec4(clamp((texture2D(t, uv).rgb - k.x) * k.y, 0.0, 1.0), 1.0); }\n");
    LV.prog = glCreateProgram();
    glAttachShader(LV.prog, vs);
    glAttachShader(LV.prog, fs);
    glBindAttribLocation(LV.prog, 0, "p");
    glLinkProgram(LV.prog);
    LV.u_k = glGetUniformLocation(LV.prog, "k");
    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers(1, &LV.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, LV.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  if (LV.w == w && LV.h == h) return;
  glBindTexture(GL_TEXTURE_2D, LV.tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindRenderbuffer(GL_RENDERBUFFER, LV.rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, LV.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, LV.tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, LV.rb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, LV.rb);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "levels: the framebuffer is incomplete\n");
  LV.w = w, LV.h = h;
}

/* the frame in the texture onto the window's bottom left w x h (the game's GL state is put back after) */
static void lv_present(int level, int w, int h, int win_w, int win_h) {
  GLint prog, abuf, vao, tex, vp[4], act;
  GLboolean blend, depth, stencil, cull, scissor, dither;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
  glGetIntegerv(GL_VIEWPORT, vp);
  blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
  cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST), dither = glIsEnabled(GL_DITHER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, win_w, win_h);
  glDisable(GL_BLEND), glDisable(GL_DEPTH_TEST), glDisable(GL_STENCIL_TEST), glDisable(GL_CULL_FACE), glDisable(GL_SCISSOR_TEST), glDisable(GL_DITHER);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, w, h);
  float b = (float)level * 0.0125f, white = 1.0f - 1.25f * b;
  glBindVertexArrayOES(0);
  glUseProgram(LV.prog);
  glUniform2f(LV.u_k, b, 1.0f / (white - b));
  glBindTexture(GL_TEXTURE_2D, LV.tex);
  glBindBuffer(GL_ARRAY_BUFFER, LV.vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glActiveTexture((GLenum)act);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
  glBindVertexArrayOES((GLuint)vao);
  glUseProgram((GLuint)prog);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (blend) glEnable(GL_BLEND);
  if (depth) glEnable(GL_DEPTH_TEST);
  if (stencil) glEnable(GL_STENCIL_TEST);
  if (cull) glEnable(GL_CULL_FACE);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  if (dither) glEnable(GL_DITHER);
}

#ifdef _WIN32
/* Windows asks "allow this program through the firewall?" once, when it first listens, and remembers a No (a block rule, no second prompt): friends then cannot join a
 * hosted game. This removes the program's rules and adds allow rules for it (UDP and TCP, every profile) in an elevated command window: Windows shows its own
 * administrator prompt, and the player says Yes there. */
static void firewall_repair(void) {
  char exe[MAX_PATH];
  if (!GetModuleFileNameA(NULL, exe, sizeof exe)) return;
  char args[1800];
  snprintf(args, sizeof args,
           "/c netsh advfirewall firewall delete rule name=all program=\"%s\" & "
           "netsh advfirewall firewall add rule name=\"Super3DX+ (UDP)\" dir=in action=allow program=\"%s\" protocol=udp profile=any enable=yes & "
           "netsh advfirewall firewall add rule name=\"Super3DX+ (TCP)\" dir=in action=allow program=\"%s\" protocol=tcp profile=any enable=yes",
           exe, exe, exe);
  HINSTANCE r = ShellExecuteA(NULL, "runas", "cmd.exe", args, NULL, SW_HIDE);
  fprintf(stderr, "firewall: asked Windows to allow %s (%s)\n", exe, (INT_PTR)r > 32 ? "prompt shown" : "refused or cancelled");
}
#endif
static int game(void *arg) {
  (void)arg;
  if (SDL_GL_MakeCurrent(P.win, P.ctx) != 0) {
    fprintf(stderr, "game thread: no context (%s)\n", SDL_GetError());
    P.fail = 1;
    return 0;
  }
  /* The game's clock is 60 Hz. On a 60 Hz display one vblank is a frame; on a faster one (119 Hz, 144 Hz ...) a frame is shown for
   * as many vblanks as the display has per 60th of a second (a swap interval of 2 on a 120 Hz display), else the frames beat against
   * the refresh and judder. A refresh that is no multiple of 60 (144 Hz) keeps the interval 1 and the 60 Hz hold below. */
  int interval = 1;
  double refresh_hz = 60.0;
  int smooth_now = -1;
  {
    SDL_DisplayMode mode;
    int di = SDL_GetWindowDisplayIndex(P.win);
    if (di >= 0 && SDL_GetCurrentDisplayMode(di, &mode) == 0 && mode.refresh_rate > 0) {
      refresh_hz = mode.refresh_rate;
      int n = (mode.refresh_rate + 30) / 60;
      if (n >= 2 && abs(mode.refresh_rate - 60 * n) <= 4) interval = n;
      fprintf(stderr, "display %d Hz\n", mode.refresh_rate);
    }
  }
  app_display_hz((float)refresh_hz);
  /* ANGLE's swap interval 2 on a 120 Hz screen gave a game at 37 fps with 40 ms frames in it (the driver's own: an even 60): its races are smooth-motion races then, one frame a refresh,
   * whatever the SMOOTH MOTION option says; the menus too at an interval of 1 */
  int fast_menus = 0;
  if ((P.angle || P.via_egl) && interval > 1) {
    interval = 1, fast_menus = 1;
    app_force_smooth(1);
    fprintf(stderr, "ANGLE on a %d Hz screen: a frame every refresh, in the menus too, and smooth motion in the races whatever the option says\n", (int)refresh_hz);
  }
  int sw = SDL_GL_SetSwapInterval(interval);
  if (sw != 0 && interval != 1) interval = 1, sw = SDL_GL_SetSwapInterval(1);
  fprintf(stderr, "swap interval: asked %d, set %s, now %d\n", interval, sw == 0 ? "ok" : SDL_GetError(), SDL_GL_GetSwapInterval());
  app_save_dir(P.save_dir);
  { /* what the window got (a washed out picture report: the bits of its buffer, its display's pixel format) */
    int rb = 0, gb = 0, bb = 0, ab = 0;
    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &rb), SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &gb);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &bb), SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &ab);
    SDL_DisplayMode dm;
    int dok = SDL_GetWindowDisplayMode(P.win, &dm) == 0;
    fprintf(stderr, "framebuffer: %d%d%d%d bits, display format %s\n", rb, gb, bb, ab, dok ? SDL_GetPixelFormatName(dm.format) : "?");
  }
  app_console(); /* a machine that can draw the maps far */
  /* The phone's extras (multisampled half-float 3D, bloom, 16x anisotropic) are on for a desktop PC. A CRT's 480 lines or a
   * Pi's GPU have no use for them (and a software renderer cannot pay for them): off there, unless MT3DX_HQ=1; MT3DX_HQ=0
   * turns them off on a PC too. */
  int hq = 1;
#if defined(__aarch64__) || defined(__arm__)
  hq = 0;
#endif
  if (CRT.on) hq = 0;
  if (getenv("MT3DX_HQ")) hq = atoi(getenv("MT3DX_HQ")) != 0;
  if (!hq) {
    setenv("WMMT_NO_HQ", "1", 0);
    setenv("WMMT_ANISO", "1", 0);
  }
  int fb_w, fb_h;
  fit_window(P.win_w, P.win_h, &fb_w, &fb_h);
  if (CRT.on) fb_w = CRT.w, fb_h = CRT.h, crt_init();
  P.resized = 0;
  hud_set_style(P.hud_touch ? HUD_STYLE_TOUCH : HUD_STYLE_ARCADE);
  app_resize(fb_w, fb_h); /* the window's shape (a wide one: app_wide) before the app starts */
  fprintf(stderr, "window %dx%d, drawing %dx%d\n", P.win_w, P.win_h, fb_w, fb_h);
  if (app_init(P.data_dir, fb_w, fb_h, LAYOUT_W, LAYOUT_H) != 0) {
    fprintf(stderr, "%s\n", app_error());
    P.fail = 1;
    return 0;
  }
  if (P.fs_cli) app_set_fullscreen(1); /* (the command line over the saved mode) */
#if !defined(__aarch64__) && !defined(__arm__)
  if (!CRT.on) app_window_row(1); /* the game options get a WINDOW MODE row (the Pi's kiosk has no windows) */
#ifdef _WIN32
  app_firewall_row(1); /* (the versus lobby's ALLOW THROUGH FIREWALL) */
#endif
#endif
  bgm_init(P.data_dir);
  audio_open();
  if (P.load_ctx) {
    SDL_Thread *t = SDL_CreateThread(loader, "loader", NULL);
    if (t) SDL_DetachThread(t);
  } else {
    fprintf(stderr, "no loading thread: the menu frames load in their spare time\n");
  }
  double last = 0;
  int movie = -1;
  /* tests (MT3DX_AUTOSTART=n): the start button each second from n s on. The game ignores it while it boots through its
   * logos and prologue, and afterwards in every menu but the attract loop, so repeating it is harmless. */
  double start_at = getenv("MT3DX_AUTOSTART") ? now_s() + atof(getenv("MT3DX_AUTOSTART")) : 0;
  const double run0 = now_s();
  while (!P.quit) {
    const double t_top = now_s();
    if (start_at > 0 && now_s() > start_at) {
      start_at = now_s() + 1.0;
      app_press_start();
    }
    if (P.resized) { /* the window changed: the app's framebuffer and its wide-screen layout follow */
      P.resized = 0;
      fit_window(P.win_w, P.win_h, &fb_w, &fb_h);
      if (CRT.on) fb_w = CRT.w, fb_h = CRT.h;
      app_resize(fb_w, fb_h);
      fprintf(stderr, "window %dx%d, drawing %dx%d\n", P.win_w, P.win_h, fb_w, fb_h);
    }
    input_ev ev[EV_MAX];
    int n;
    SDL_LockMutex(P.mx);
    n = P.n_ev;
    memcpy(ev, P.ev, n * sizeof *ev);
    P.n_ev = 0;
    SDL_UnlockMutex(P.mx);
    for (int i = 0; i < n; ++i) {
      if (ev[i].kind == 1) app_press_start();
      else if (ev[i].kind == 3) app_axis(ev[i].phase, ev[i].x);
      else if (ev[i].kind == 4) app_readout(-1);
      else if (ev[i].kind == 5) app_phys(-1);
      else if (ev[i].kind == 6) app_tune(-1);
      else if (ev[i].kind == 7) app_set_fullscreen(!app_fullscreen()); /* F11, Alt+Enter */
      else if (ev[i].kind == 2) app_input(ev[i].phase, ev[i].code, ev[i].flags & 1, ev[i].flags >> 1); /* (the bindings) */
      else app_touch((void *)(intptr_t)1, ev[i].phase, ev[i].x, ev[i].y);
    }
    double now = now_s();
    float dt = last > 0 ? (float)(now - last) : 1.0f / 60.0f;
    last = now;
    int level = CRT.on ? 0 : app_black_level();
    if (getenv("MT3DX_BLACK")) level = atoi(getenv("MT3DX_BLACK")); /* test: this black level */
    if (level > 0) lv_ensure(fb_w, fb_h);
    glBindFramebuffer(GL_FRAMEBUFFER, CRT.on ? CRT.fbo : level > 0 ? LV.fbo : 0);
    if (getenv("MT3DX_TAP")) tap_check(now - run0);
    if (getenv("MT3DX_SCRIPT")) script_check(now - run0);
    if (getenv("MT3DX_AUTODRIVE") && app_racing()) { /* tests: the accelerator held in a race, the brake from MT3DX_BRAKE_AT s of the run on */
      int brake = getenv("MT3DX_BRAKE_AT") && now - run0 >= atof(getenv("MT3DX_BRAKE_AT"));
      /* MT3DX_AUTODRIVE_KMH=<n>: the accelerator only below that speed (a car to chase and touch, with WMMT_PILOT steering it) */
      int accel = !brake && (!getenv("MT3DX_AUTODRIVE_KMH") || app_speed_kmh() < (float)atof(getenv("MT3DX_AUTODRIVE_KMH")));
      app_key(APP_KEY_ACCEL, accel);
      app_key(APP_KEY_BRAKE, brake);
    }
    const double t_app0 = now_s();
    app_frame(dt);
    const double t_app1 = now_s();
    P.fs_want = app_fullscreen();
    {
      int act = app_net_act(); /* the network options' buttons */
      if (act == 3) SDL_SetClipboardText(app_net_diag());
#ifdef _WIN32
      if (act == 1) firewall_repair();
      if (act == 2) ShellExecuteA(NULL, "open", app_save_folder(), NULL, NULL, SW_SHOWNORMAL); /* (log.txt is in it) */
#endif
    }
    P.cursor_hide = app_racing();
    pad_rumble_frame();
    wheel_frame(); /* (a wheel's forces, wheel.c) */
    /* the attract loop's movies (movie.c: data_dir/intro.wmv, playdemo.wmv, tutorial.wmv); one that is not there ends at once */
    int want = app_movie();
    if (want != movie) {
      movie = want;
      movie_stop();
      if (want >= 0 && movie_start(P.data_dir, want) != 0) {
        fprintf(stderr, "movie %d: no file, skipped (python tools/pipeline.py convert --only movies makes them)\n", want);
        app_movie_done();
        movie = -1;
      }
    }
    if (movie >= 0 && movie_finished()) {
      movie_stop();
      app_movie_done();
      movie = -1;
    }
    if (getenv("MT3DX_LOG")) {
      static int screen = -2;
      int now_screen = app_screen();
      if (now_screen != screen) {
        fprintf(stderr, "screen %d -> %d\n", screen, now_screen);
        screen = now_screen;
      }
    }
    const char *track = app_bgm();
    if (track != P.bgm) {
      P.bgm = track;
      fprintf(stderr, "bgm: %s\n", track ? track : "(none)");
      bgm_play(track);
    }
    bgm_next(app_bgm_next());
    if (getenv("MT3DX_LOG")) {
      static char last[96];
      char now[96];
      bgm_current(now, sizeof now);
      if (strcmp(now, last)) fprintf(stderr, "bgm playing: %s\n", now), snprintf(last, sizeof last, "%s", now);
    }
    bgm_level(app_bgm_level());
    if (movie_active()) movie_draw(CRT.on || level > 0 ? fb_w : P.win_w, CRT.on || level > 0 ? fb_h : P.win_h); /* (over the frame, in the texture the CRT pass takes) */
    if (level > 0 && !CRT.on) lv_present(level, fb_w, fb_h, P.win_w, P.win_h); /* (before the shot: it sees what the window gets) */
    if (getenv("MT3DX_SHOT")) shot_check(now_s() - run0); /* (the movie too; in the CRT mode the game's own 640x480) */
    if (CRT.on) crt_present(P.win_w, P.win_h);
    int smooth = app_smooth(); /* (a race drawn with smooth motion: a frame every refresh of the display, not 60 a second) */
    if (smooth != smooth_now) {
      smooth_now = smooth;
      SDL_GL_SetSwapInterval(smooth ? 1 : interval);
      fprintf(stderr, "smooth motion %s: swap interval %d\n", smooth ? "on" : "off", smooth ? 1 : interval);
    }
    double p0 = now_s();
    SDL_GL_SwapWindow(P.win);
    {
      static int finish = -1;
      if (finish < 0) finish = getenv("MT3DX_FINISH") ? atoi(getenv("MT3DX_FINISH")) : 0;
      if (smooth && finish) glFinish(); /* test: wait for the swap to be done, so no frames queue up behind it */
    }
    const double t_swap1 = now_s();
    app_frame_done((t_swap1 - p0) * 1000.0);
    /* The swap interval is only a request: Mesa on WSLg ignores it and the game ran at several hundred frames a
     * second, flooding the display path. The game's clock is 60 Hz, so hold it there if the swap did not. */
    {
      static double due;
      double t = now_s();
      if (due == 0 || t - due > 0.1) due = t;
      due += smooth || fast_menus ? 1.0 / refresh_hz : 1.0 / 60.0;
      if (due > t + 0.002) SDL_Delay((Uint32)((due - t - 0.002) * 1000.0)); /* (a sleep is as coarse as the timer: the last 2 ms spin) */
      while (now_s() < due) {}
    }
    { /* test: MT3DX_HITCH=<ms> logs a frame that took longer, and where the time went (the work before the app's frame, the frame, the swap, the hold) */
      static double limit = -1;
      if (limit < 0) limit = getenv("MT3DX_HITCH") ? atof(getenv("MT3DX_HITCH")) * 1e-3 : 0;
      double t_end = now_s();
      static int flog = -1, fcount;
      if (flog < 0) flog = getenv("MT3DX_FRAMELOG") ? atoi(getenv("MT3DX_FRAMELOG")) : 0;
      if (flog > 0 && app_racing() && ++fcount > 600 && fcount <= 600 + flog) /* (test: every frame's length and parts, from the race's 600th frame) */
        fprintf(stderr, "frame: t %.3f dt %.2f total %.2f = app %.2f swap %.2f hold %.2f top %.2f\n", t_end - run0, dt * 1e3, (t_end - now) * 1e3, (t_app1 - t_app0) * 1e3, (t_swap1 - p0) * 1e3, (t_end - t_swap1) * 1e3, (now - t_top) * 1e3);
      if (limit > 0 && t_end - now > limit)
        fprintf(stderr, "hitch: %.1f ms = pre %.1f + app %.1f + after %.1f + swap %.1f + hold %.1f (dt %.1f, screen %d, smooth %d)\n", (t_end - now) * 1e3, (t_app0 - now) * 1e3, (t_app1 - t_app0) * 1e3,
                (p0 - t_app1) * 1e3, (t_swap1 - p0) * 1e3, (t_end - t_swap1) * 1e3, dt * 1e3, app_screen(), smooth);
    }
    /* MT3DX_LOG: the frame rate each 2 s, to tell a frozen game from a still screen */
    static int frames;
    static double t0;
    ++frames;
    if (getenv("MT3DX_LOG") && now_s() - t0 >= 2.0) {
      if (t0 > 0) fprintf(stderr, "fps %.1f\n", frames / (now_s() - t0));
      t0 = now_s();
      frames = 0;
    }
  }
  app_vs_leave(); /* (a versus left by closing the window: the others hear it at once) */
  wheel_quit();   /* (no force left running on the wheel) */
  return 0;
}

static int game_thread(void *arg) {
  game(arg);
  P.game_done = 1;
  return 0;
}

/* ---- the main thread: window and events ---- */

/* ---- pads (SDL's game controller layer: anything with a mapping, XInput and DirectInput pads, PlayStation, Switch...) ---- */

static SDL_GameController *PAD[8];

/* The pad's rumble (haptic.h through app_rumble): sent each frame it changes and again before the last one runs out (the pad is told how long
 * a rumble lasts, 120 ms, so a frozen game lets it die), to every pad that is open. A pad without rumble ignores it. */
static void pad_rumble_frame(void) {
  static Uint16 sent_low, sent_high;
  static Uint32 sent_at;
  float lo, hi;
  app_rumble(&lo, &hi);
  Uint16 l = (Uint16)(lo * 65535.0f), h = (Uint16)(hi * 65535.0f);
  Uint32 now = SDL_GetTicks();
  int changed = abs((int)l - (int)sent_low) > 1500 || abs((int)h - (int)sent_high) > 1500 || ((l == 0) != (sent_low == 0)) || ((h == 0) != (sent_high == 0));
  if (!changed && !((l || h) && now - sent_at >= 80)) return;
  for (int i = 0; i < 8; ++i)
    if (PAD[i]) SDL_GameControllerRumble(PAD[i], l, h, l || h ? 120 : 0);
  sent_low = l, sent_high = h, sent_at = now;
}

static void pad_open(int index) {
  if (!SDL_IsGameController(index)) return;
  for (int i = 0; i < 8; ++i)
    if (!PAD[i]) {
      PAD[i] = SDL_GameControllerOpen(index);
      if (PAD[i]) fprintf(stderr, "pad: %s\n", SDL_GameControllerName(PAD[i]));
      return;
    }
}

static void pad_close(SDL_JoystickID id) {
  for (int i = 0; i < 8; ++i)
    if (PAD[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(PAD[i])) == id) {
      SDL_GameControllerClose(PAD[i]);
      PAD[i] = NULL;
    }
}

/* MT3DX_VPAD="<s>:<button>;...": a virtual game controller (SDL's) that presses these buttons at those seconds of running,
 * for 0.15 s each: A B X Y START BACK LB RB UP DOWN LEFT RIGHT, RT and LT for the triggers. It goes through pad_button and
 * pad_axis as a real pad does: the test of the pad's path without one. "<s>:AX<n>=<v>" sets axis n (SDL's order: 0 left X, 1 left Y,
 * 2 right X, 3 right Y, 4 left trigger, 5 right trigger) to v (-1..1) and holds it. */
static SDL_Joystick *VPAD;
static struct { double at; int button, axis, down, done, set, value; } VP[64];
static int n_vp;

static void vpad_init(void) {
  const char *s = getenv("MT3DX_VPAD");
  if (!s) return;
  static const struct { const char *name; int button, axis; } NAMES[] = {
    { "A", SDL_CONTROLLER_BUTTON_A, -1 }, { "B", SDL_CONTROLLER_BUTTON_B, -1 }, { "X", SDL_CONTROLLER_BUTTON_X, -1 },
    { "Y", SDL_CONTROLLER_BUTTON_Y, -1 }, { "START", SDL_CONTROLLER_BUTTON_START, -1 }, { "BACK", SDL_CONTROLLER_BUTTON_BACK, -1 },
    { "LB", SDL_CONTROLLER_BUTTON_LEFTSHOULDER, -1 }, { "RB", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1 },
    { "UP", SDL_CONTROLLER_BUTTON_DPAD_UP, -1 }, { "DOWN", SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1 },
    { "LEFT", SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1 }, { "RIGHT", SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1 },
    { "RT", -1, SDL_CONTROLLER_AXIS_TRIGGERRIGHT }, { "LT", -1, SDL_CONTROLLER_AXIS_TRIGGERLEFT },
  };
  for (; s && *s && n_vp < 60; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL) {
    double at, v;
    int ax;
    char name[16];
    if (sscanf(s, "%lf:AX%d=%lf", &at, &ax, &v) == 3) {
      VP[n_vp].at = at, VP[n_vp].button = -1, VP[n_vp].axis = ax, VP[n_vp].set = 1, VP[n_vp++].value = (int)(v * 32767.0);
      continue;
    }
    if (sscanf(s, "%lf:%15[A-Z]", &at, name) != 2) continue;
    for (size_t k = 0; k < sizeof NAMES / sizeof *NAMES; ++k)
      if (!strcmp(name, NAMES[k].name)) {
        VP[n_vp].at = at, VP[n_vp].button = NAMES[k].button, VP[n_vp].axis = NAMES[k].axis, VP[n_vp++].down = 1;
        VP[n_vp].at = at + 0.15, VP[n_vp].button = NAMES[k].button, VP[n_vp].axis = NAMES[k].axis, VP[n_vp++].down = 0;
      }
  }
  SDL_VirtualJoystickDesc d;
  SDL_zero(d);
  d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
  d.naxes = SDL_CONTROLLER_AXIS_MAX;
  d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
  d.name = "virtual pad";
  int index = SDL_JoystickAttachVirtualEx(&d);
  VPAD = index >= 0 ? SDL_JoystickOpen(index) : NULL;
  fprintf(stderr, "virtual pad: %s (%d presses)\n", VPAD ? "attached" : SDL_GetError(), n_vp / 2);
}

/* MT3DX_VWHEEL="<s>:AX<n>=<v>;<s>:B<n>;...": a virtual steering wheel (a joystick no game controller layout knows: 6 axes, 12 buttons, the pedals' axes 1 and 2
 * resting at -1) that sets axis n to v (-1..1) or presses button n (0 based) for 0.15 s at those seconds: the test of the wheel's path (wheel.c) without one.
 * It has no force feedback; the device list and the raw axes and buttons are what it tests. */
static SDL_Joystick *VWHEEL;
static struct { double at; int axis, button, value, down, done; } VW[64];
static int n_vw;

static void vwheel_init(void) {
  const char *s = getenv("MT3DX_VWHEEL");
  if (!s) return;
  for (; s && *s && n_vw < 60; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL) {
    double at, v;
    int n;
    if (sscanf(s, "%lf:AX%d=%lf", &at, &n, &v) == 3) VW[n_vw].at = at, VW[n_vw].axis = n, VW[n_vw++].value = (int)(v * 32767.0);
    else if (sscanf(s, "%lf:B%d", &at, &n) == 2) {
      VW[n_vw].at = at, VW[n_vw].axis = -1, VW[n_vw].button = n, VW[n_vw++].down = 1;
      VW[n_vw].at = at + 0.15, VW[n_vw].axis = -1, VW[n_vw].button = n, VW[n_vw++].down = 0;
    }
  }
  SDL_VirtualJoystickDesc d;
  SDL_zero(d);
  d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  d.type = SDL_JOYSTICK_TYPE_WHEEL;
  d.naxes = 6;
  d.nbuttons = 12;
  d.name = "Virtual wheel";
  int index = SDL_JoystickAttachVirtualEx(&d);
  VWHEEL = index >= 0 ? SDL_JoystickOpen(index) : NULL;
  if (VWHEEL) SDL_JoystickSetVirtualAxis(VWHEEL, 1, -32768), SDL_JoystickSetVirtualAxis(VWHEEL, 2, -32768);
  fprintf(stderr, "virtual wheel: %s (%d steps)\n", VWHEEL ? "attached" : SDL_GetError(), n_vw);
}

static void vwheel_run(double t) {
  for (int i = 0; VWHEEL && i < n_vw; ++i) {
    if (VW[i].done || t < VW[i].at) continue;
    VW[i].done = 1;
    if (VW[i].axis >= 0) SDL_JoystickSetVirtualAxis(VWHEEL, VW[i].axis, VW[i].value);
    else SDL_JoystickSetVirtualButton(VWHEEL, VW[i].button, VW[i].down);
  }
}

static void vpad_run(double t) {
  for (int i = 0; VPAD && i < n_vp; ++i) {
    if (VP[i].done || t < VP[i].at) continue;
    VP[i].done = 1;
    if (VP[i].set) SDL_JoystickSetVirtualAxis(VPAD, VP[i].axis, VP[i].value);
    else if (VP[i].button >= 0) SDL_JoystickSetVirtualButton(VPAD, VP[i].button, VP[i].down);
    else SDL_JoystickSetVirtualAxis(VPAD, VP[i].axis, VP[i].down ? 32767 : 0);
  }
}

/* a pad input (a BP_* code) went down or up; held ones repeat (0.4 s, then every 0.12 s: a menu moves on; the race ignores it) */
static double PAD_NEXT[BP_COUNT];
static int PAD_HELD[BP_COUNT];

static void pad_input(int code, int down) {
  if (code < 0 || code >= BP_COUNT || PAD_HELD[code] == down) return;
  PAD_HELD[code] = down;
  if (down) PAD_NEXT[code] = now_s() + 0.4;
  push_input(BIND_PAD, code, down);
}

static void pad_repeat(void) {
  double t = now_s();
  for (int c = 0; c < BP_COUNT; ++c)
    if (PAD_HELD[c] && t >= PAD_NEXT[c]) {
      push_input(BIND_PAD, c, 1 | 2);
      PAD_NEXT[c] = t + 0.12;
    }
}

static void pad_button(int button, int down) {
  if (getenv("MT3DX_LOG")) fprintf(stderr, "input: pad button %d %s\n", button, down ? "down" : "up");
  static const signed char CODE[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = BP_A, [SDL_CONTROLLER_BUTTON_B] = BP_B, [SDL_CONTROLLER_BUTTON_X] = BP_X, [SDL_CONTROLLER_BUTTON_Y] = BP_Y,
    [SDL_CONTROLLER_BUTTON_BACK] = BP_BACK, [SDL_CONTROLLER_BUTTON_START] = BP_START, [SDL_CONTROLLER_BUTTON_LEFTSTICK] = BP_LSTICK,
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = BP_RSTICK, [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = BP_LB,
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = BP_RB, [SDL_CONTROLLER_BUTTON_DPAD_UP] = BP_DUP, [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = BP_DDOWN,
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = BP_DLEFT, [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = BP_DRIGHT,
  };
  if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX || button == SDL_CONTROLLER_BUTTON_GUIDE) return;
  if (CODE[button] == 0 && button != SDL_CONTROLLER_BUTTON_A) return; /* (a button with no code) */
  pad_input(CODE[button], down);
}

/* an axis past a threshold is a pad input (with a hysteresis: on at `on`, off again inside `off`) */
static void pad_axis_input(int code, int v, int on, int off) { pad_input(code, PAD_HELD[code] ? v > off : v > on); }

static void push_axis(int axis, int v) {
  SDL_LockMutex(P.mx);
  if (P.n_ev < EV_MAX) P.ev[P.n_ev++] = (input_ev){ 3, axis, v / 32767.0f, 0, 0, 0 };
  SDL_UnlockMutex(P.mx);
}

/* a steering wheel's axis and button (wheel.c): the same events the pad's make (the bindings say what they steer) */
static void wheel_axis_sink(int axis, int v) {
  if (getenv("MT3DX_LOG")) fprintf(stderr, "input: wheel axis %d %d\n", axis - AXIS_W0, v);
  push_axis(axis, v);
}
static void wheel_button_sink(int code, int down) {
  if (getenv("MT3DX_LOG")) fprintf(stderr, "input: wheel button %d %s\n", code, down ? "down" : "up");
  pad_input(code, down);
}

static void pad_axis(int axis, int v) {
  if (getenv("MT3DX_LOG")) fprintf(stderr, "input: pad axis %d %d\n", axis, v);
  push_axis(axis, v); /* the analog value (the bindings: steering, gas, brake), then the thresholds for the menus' directions */
  switch (axis) {
  case SDL_CONTROLLER_AXIS_LEFTX:
    pad_axis_input(BP_STICK_LEFT, -v, 14000, 8000);
    pad_axis_input(BP_STICK_RIGHT, v, 14000, 8000);
    return;
  case SDL_CONTROLLER_AXIS_LEFTY:
    pad_axis_input(BP_STICK_UP, -v, 14000, 8000);
    pad_axis_input(BP_STICK_DOWN, v, 14000, 8000);
    return;
  case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: pad_axis_input(BP_RT, v, 16000, 8000); return;
  case SDL_CONTROLLER_AXIS_TRIGGERLEFT: pad_axis_input(BP_LT, v, 16000, 8000); return;
  }
}

/* The part of the window the game draws in. The game's layout is 4:3 (1024x768): a wider window shows more at the sides
 * (app_wide, the app's own handling of an iPhone's or a phone's wide screen); a taller one keeps a 4:3 at its bottom (the game's
 * viewport starts at the window's lower left), which the window's resizing keeps from happening in a window. */
static void fit_window(int ww, int wh, int *fw, int *fh) {
  *fw = ww > 1 ? ww : 1;
  *fh = wh > 1 ? wh : 1;
  if ((long)*fw * 3 < (long)*fh * 4) *fh = *fw * 3 / 4 > 0 ? *fw * 3 / 4 : 1;
}

/* a mouse position in the layout's px, as the touch screen gives them: x from -edge on a wide window */
static float to_layout_x(int x) {
  int fw, fh;
  fit_window(P.win_w, P.win_h, &fw, &fh);
  float width = (float)fw * LAYOUT_H / (float)fh, edge = (width - LAYOUT_W) * 0.5f;
  if (edge < 1.0f) edge = 0;
  return (float)x * (LAYOUT_W + 2.0f * edge) / (float)fw - edge;
}
static float to_layout_y(int y) {
  int fw, fh;
  fit_window(P.win_w, P.win_h, &fw, &fh);
  return (float)(y - (P.win_h - fh)) * LAYOUT_H / (float)fh;
}

#if defined(_WIN32) && !defined(WMMT_NO_AUTO_ANGLE)
/* AMD graphics start on the bundled ANGLE (see main): wanted only if ANGLE's libEGL.dll is next to the program, and no file named native-gl.txt is
 * (an empty one is the way to keep the driver's own OpenGL ES without a command line) */
static int angle_wanted(void) {
  char *base = SDL_GetBasePath();
  int ok = 0;
  if (base) {
    char p[1200];
    FILE *f;
    snprintf(p, sizeof p, "%slibEGL.dll", base);
    if ((f = fopen(p, "rb"))) fclose(f), ok = 1;
    snprintf(p, sizeof p, "%snative-gl.txt", base);
    if ((f = fopen(p, "rb"))) fclose(f), ok = 0;
    SDL_free(base);
  }
  return ok;
}

/* this program again with --angle (1: started, the caller leaves; 0: could not, the caller goes on). The log is let go first: the new run keeps it as
 * log-prev.txt, so the line saying why stays in the report */
static int restart_on_angle(void) {
  static char cmd[4096];
  snprintf(cmd, sizeof cmd, "%s --angle", GetCommandLineA());
  fprintf(stderr, "AMD graphics (%s): starting again on ANGLE (--native-gl keeps the driver's OpenGL ES)\n", (const char *)glGetString(GL_RENDERER));
  SDL_setenv("MT3DX_ANGLE_AUTO", "1", 1);
  fflush(stderr);
  if (P.log_file[0]) fclose(stderr);
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    if (P.log_file[0] && freopen(P.log_file, "a", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
    fprintf(stderr, "could not start again (error %lu): staying on the driver's OpenGL ES\n", (unsigned long)GetLastError());
    return 0;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 1;
}
#endif

int main(int argc, char **argv) {
  int w = LAYOUT_W, h = LAYOUT_H, fullscreen = 0, sized = 0;
  int use_loader_thread = getenv("MT3DX_LOADER_THREAD") != NULL;
  const char *data = NULL, *save = NULL;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--loader-thread")) use_loader_thread = 1;
    else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h), sized = 1;
    else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
    else if (!strcmp(argv[i], "--crt")) CRT.on = 1, crt_config();
    else if (!strcmp(argv[i], "--angle")) P.angle = 1;
    else if (!strcmp(argv[i], "--native-gl")) P.native_gl = 1;
    else if (!strcmp(argv[i], "--hud") && i + 1 < argc) P.hud_touch = !strcmp(argv[++i], "touch");
    else if (argv[i][0] != '-') data = argv[i];
    else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
  }
  /* Started with nothing (a double-click on the exe): the data and the saves are the folders next to the program. */
  static char base_data[1024], base_save[1024];
  char *base = SDL_GetBasePath();
  if (!data && base) {
    snprintf(base_data, sizeof base_data, "%sdata", base);
    data = base_data;
  }
  if (!save && base && data == base_data) {
    snprintf(base_save, sizeof base_save, "%ssave", base);
    save = base_save;
  }
  if (base) SDL_free(base);
  if (!data) {
    fprintf(stderr, "usage: %s [data dir] [--save DIR] [--size WxH] [--fullscreen] [--crt] [--angle | --native-gl] [--hud arcade|touch] [--loader-thread]\n", argv[0]);
    return 2;
  }
  if (data == base_data) {
    char probe[1100];
    snprintf(probe, sizeof probe, "%s/menu.bin", data);
    FILE *f = fopen(probe, "rb");
    if (!f) {
      static char msg[1400];
      snprintf(msg, sizeof msg, "The game's data was not found in:\n%s\n\nBuild it from your own copy of the game (see the README), "
               "or start the program with the data folder as its argument.", data);
      fprintf(stderr, "%s\n", msg);
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super3DX+", msg, NULL);
      return 2;
    }
    fclose(f);
  }
  if (save == base_save) {
#ifdef _WIN32
    _mkdir(save);
#else
    mkdir(save, 0755);
#endif
  }
  /* a desktop window opens at a size worth looking at (the layout is 4:3); --size, the CRT and the Pi's fullscreen set their own */
  if (!sized && !fullscreen && !CRT.on) w = 1280, h = 960;
  P.fs_cli = P.fs_now = P.fs_want = fullscreen;
  snprintf(P.data_dir, sizeof P.data_dir, "%s", data);
  snprintf(P.save_dir, sizeof P.save_dir, "%s", save ? save : data);
  setvbuf(stderr, NULL, _IOLBF, 0);
#ifdef _WIN32
  winport_binary_files();
  /* Started by a double-click (stderr is a console window or nowhere): what the game says goes to <save>/log.txt, the last run's kept as log-prev.txt,
   * so a hang or a crash (a versus that stops, issue reports) leaves something to read. Started with its stderr sent somewhere (`2> log.txt`, a pipe) or
   * with MT3DX_STDERR set, it is left alone. */
  if (!getenv("MT3DX_STDERR")) {
    const DWORD ft = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
    if (ft != FILE_TYPE_DISK && ft != FILE_TYPE_PIPE) {
      char lp[1200], pp[1200];
      snprintf(lp, sizeof lp, "%s/log.txt", P.save_dir);
      snprintf(pp, sizeof pp, "%s/log-prev.txt", P.save_dir);
      log_scrub_file(lp); /* (the last run's log, a crashed one too: no public address, room code or user name stays in it) */
      remove(pp);
      rename(lp, pp);
      snprintf(P.log_file, sizeof P.log_file, "%s", lp);
      if (freopen(lp, "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
    }
  }
#endif

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); /* a pad works with the window behind another */
#ifdef _WIN32
  /* Windows scales a program that does not say it knows about the screen's scaling (150 %, 200 %): the window's pixels are stretched, which is what
   * made the fullscreen picture blocky. Per-monitor awareness hands the game the real pixels (the window's size and the drawing are in them). */
  SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
  /* A racing wheel's forces only go through DirectInput. SDL's RawInput and Windows.Gaming.Input drivers (on by default) take such a wheel first
   * and have no forces: the wheel is listed, but "forces not available" (a Fanatec on the maker's driver). Off, a wheel comes through DirectInput
   * and an Xbox pad still through XInput. Default priority: the same names in the environment (SDL_JOYSTICK_RAWINPUT=1) still win. */
  SDL_SetHintWithPriority("SDL_JOYSTICK_RAWINPUT", "0", SDL_HINT_DEFAULT);
  SDL_SetHintWithPriority("SDL_JOYSTICK_WGI", "0", SDL_HINT_DEFAULT);
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) fprintf(stderr, "no force feedback (SDL haptic: %s)\n", SDL_GetError()); /* (a wheel still steers) */
  {
    static const wheel_sink sink = { wheel_axis_sink, wheel_button_sink };
    wheel_init(&sink);
    wheel_diag_dir(P.save_dir);
  }
  P.mx = SDL_CreateMutex();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); /* the cars' shadows */
  if (!sized && !fullscreen && !CRT.on) { /* the window's own size: 1280 x 960 at 96 dpi, more on a scaled screen, but within the desktop */
    float ddpi = 96.0f;
    SDL_Rect area;
    SDL_GetDisplayDPI(0, &ddpi, NULL, NULL);
    float sc = ddpi > 96.0f ? ddpi / 96.0f : 1.0f;
    w = (int)(1280.0f * sc), h = (int)(960.0f * sc);
    if (SDL_GetDisplayUsableBounds(0, &area) == 0 && h > area.h * 92 / 100) h = area.h * 92 / 100, w = h * 4 / 3;
  }
  /* (Windows: the window stays hidden until the graphics card is known: an AMD one goes to ANGLE, which means starting again) */
  int check_amd = 0;
#if defined(_WIN32) && !defined(WMMT_NO_AUTO_ANGLE)
  check_amd = !P.angle && !P.native_gl && !getenv("MT3DX_NATIVE_GL") && !getenv("MT3DX_NOGL") && angle_wanted();
#endif
#if defined(_WIN32) && !defined(WMMT_NO_AUTO_ANGLE)
  if (check_amd) { /* a screen of 90 Hz or more starts on ANGLE too: the NVIDIA driver's own OpenGL ES let the frame queue run empty and refill every 3 s there (a hitch in smooth motion's
                    * 120 fps), ANGLE's Direct3D 11 swap chain did not (5 races in a row: every 10 s average within 8.34-8.40 ms at 120 Hz) */
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.refresh_rate >= 90) {
      P.angle = 1, check_amd = 0;
      fprintf(stderr, "the screen runs at %d Hz: starting on ANGLE (--native-gl or a native-gl.txt beside the exe keeps the driver's OpenGL ES)\n", dm.refresh_rate);
    }
  }
#endif
  Uint32 flags = SDL_WINDOW_OPENGL | (check_amd ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (P.angle) fprintf(stderr, "--angle: skipping the driver's OpenGL ES, using ANGLE\n");
  if (getenv("MT3DX_ANGLE_AUTO")) fprintf(stderr, "ANGLE chosen automatically: the graphics card is an AMD one (--native-gl or MT3DX_NATIVE_GL=1 keeps the driver's own OpenGL ES)\n");
  P.win = P.angle || getenv("MT3DX_NOGL") ? NULL : SDL_CreateWindow("Super3DX+", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags); /* (MT3DX_NOGL=1: test the no-driver message) */
  if (!P.win) {
    /* the graphics driver offers no OpenGL ES (a virtual machine, a remote desktop, a very old or generic driver): try the EGL route (it finds an
     * ANGLE next to the game, if one is there), then say what is wrong instead of just closing */
    char first[256];
    snprintf(first, sizeof first, "%s", SDL_GetError());
    fprintf(stderr, "no window: %s\n", first);
    if (P.angle || getenv("MT3DX_NOGL")) snprintf(first, sizeof first, "test: no driver"), !P.angle && atoi(getenv("MT3DX_NOGL")) == 2 ? SDL_setenv("SDL_VIDEO_EGL_DRIVER", "bogus_egl.dll", 1) : 0; /* (2: and no ANGLE either) */
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    P.win = SDL_CreateWindow("Super3DX+", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!P.win) {
      fprintf(stderr, "no window with EGL either: %s\n", SDL_GetError());
      char msg[900];
      snprintf(msg, sizeof msg,
               "The game could not start: your graphics driver does not provide OpenGL ES (%s).\n\n"
               "This usually means the graphics driver is missing or very old, or the game is running in a virtual machine or over remote desktop.\n\n"
               "Please install the latest driver from your graphics card maker (AMD, NVIDIA or Intel) and try again. If it still fails, "
               "attach log.txt from the game's folder to a GitHub issue.",
               first);
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super3DX+", msg, NULL);
      return 1;
    }
    fprintf(stderr, "window made through EGL\n");
    P.via_egl = 1;
  }
  /* OpenGL ES 3 where there is, else 2 */
  static const int versions[2][2] = { { 3, 0 }, { 2, 0 } };
  for (int v = 0; v < 2 && !P.ctx; ++v) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, versions[v][0]);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, versions[v][1]);
    P.ctx = SDL_GL_CreateContext(P.win);
  }
  if (!P.ctx) {
    fprintf(stderr, "no OpenGL ES context: %s\n", SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super3DX+",
                             "The game could not start: your graphics driver refused to create an OpenGL ES context.\n\n"
                             "Please install the latest driver from your graphics card maker (AMD, NVIDIA or Intel) and try again. If it still fails, "
                             "attach log.txt from the game's folder to a GitHub issue.",
                             NULL);
    return 1;
  }
#ifdef WMMT_WIN
  { /* no GL library to link: the entry points are looked up in the driver (build.py's trampolines) */
    int missing = gl_load();
    if (missing) fprintf(stderr, "%d GL functions missing from the driver\n", missing);
  }
#endif
  fprintf(stderr, "GL %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
#if defined(_WIN32) && !defined(WMMT_NO_AUTO_ANGLE)
  if (check_amd && !P.via_egl) { /* AMD's own OpenGL ES drew every other frame of a race from a stale buffer on a Radeon RX 9070 XT (issue 33) and lifted the blacks on others (issue 15):
                                  * ANGLE (Direct3D 11) shows neither. An AMD card therefore starts again on it, unless told not to */
    const char *vendor = (const char *)glGetString(GL_VENDOR), *rend = (const char *)glGetString(GL_RENDERER);
    int amd = (vendor && (strstr(vendor, "ATI") || strstr(vendor, "AMD"))) || (rend && (strstr(rend, "AMD") || strstr(rend, "Radeon"))) || getenv("MT3DX_FAKE_AMD"); /* (MT3DX_FAKE_AMD=1: test the switch on any card) */
    if (amd && restart_on_angle()) return 0;
  }
  if (check_amd) SDL_ShowWindow(P.win);
#endif
#ifdef WMMT_WIN
  { /* AMD's own OpenGL ES showed the picture with lifted, grey blacks where ANGLE (Direct3D 11) did not (issue 15): a default framebuffer the driver encodes as sRGB is the
     * suspect. Say what it has, and switch the encoding off if it is on (the game's colours are already final: nothing here converts them) */
    int cap = -1, r = 0, g = 0, b = 0;
    SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &cap);
    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &r), SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &g), SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &b);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    int has = ext && strstr(ext, "GL_EXT_sRGB_write_control") != NULL;
    while (glGetError() != GL_NO_ERROR) {}
    int on = glIsEnabled(0x8DB9) ? 1 : 0; /* GL_FRAMEBUFFER_SRGB(_EXT) */
    GLenum bad = glGetError();
    fprintf(stderr, "framebuffer: %d%d%d bits, sRGB capable pixel format %d, GL_EXT_sRGB_write_control %d, GL_FRAMEBUFFER_SRGB %s\n", r, g, b, cap, has,
            bad ? "unknown to this driver" : on ? "on" : "off");
    if (!bad && on && !getenv("MT3DX_SRGB_KEEP")) { /* (MT3DX_SRGB_KEEP=1: leave it, to compare) */
      glDisable(0x8DB9);
      while (glGetError() != GL_NO_ERROR) {}
      fprintf(stderr, "GL_FRAMEBUFFER_SRGB was on: switched off\n");
    }
  }
#endif
  if (!fullscreen && w * 3 < h * 4) SDL_SetWindowSize(P.win, w, h = w * 3 / 4); /* (narrower than 4:3: made 4:3) */
  SDL_SetWindowMinimumSize(P.win, 320, 240);
  SDL_GetWindowSize(P.win, &P.win_w, &P.win_h);

  /* A loading thread, if asked for (MT3DX_LOADER_THREAD=1 or --loader-thread): its context shares the game's, on a window
   * of its own. Off by default: with Mesa's Direct3D 12 driver on WSLg the game thread's swap and the loader's texture
   * upload deadlock (the game waits for a GPU fence, the loader for a driver lock), freezing the whole player at the first
   * Start press. Without it the game loads in the menu frames' spare time, as it is made to. */
  if (use_loader_thread) {
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    P.load_win = SDL_CreateWindow("loader", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (P.load_win) P.load_ctx = SDL_GL_CreateContext(P.load_win);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
  }
  SDL_GL_MakeCurrent(P.win, NULL); /* (the game thread takes it) */

  /* 8 MB of stack: the recompiled physics has deep frames */
  P.game = SDL_CreateThreadWithStackSize(game_thread, "game", 8 << 20, NULL);
  if (!P.game) {
    fprintf(stderr, "no game thread: %s\n", SDL_GetError());
    return 1;
  }

  bind_set_key_namer(key_name);
  vpad_init();
  vwheel_init();
  const double vpad_t0 = now_s();
  int left_down = 0;
  int finger_down = 0; /* a touchscreen (Switch): if switch-sdl2 also makes mouse events from a touch, ignore them */
  const double quit_at = getenv("MT3DX_AUTOQUIT") ? now_s() + atof(getenv("MT3DX_AUTOQUIT")) : 0; /* tests: leave after n s */
  while (!P.quit && !P.fail) {
    SDL_Event e;
    if (quit_at > 0 && now_s() > quit_at) {
      fprintf(stderr, "MT3DX_AUTOQUIT: leaving\n");
      break;
    }
    if (app_exit_requested()) { /* the main menu's EXIT */
      P.exit_req = 1;
      break;
    }
    pad_repeat();
    vpad_run(now_s() - vpad_t0);
    vwheel_run(now_s() - vpad_t0);
    /* tests: MT3DX_CLICK="<s>:<x>,<y>;..." clicks at those window pixels (the mouse path, for the window-to-layout mapping),
     * MT3DX_RESIZE="<s>:<w>x<h>;..." sizes the window then (the live resize) */
    {
      static int ci, ri;
      double t = now_s() - vpad_t0;
      const char *s = getenv("MT3DX_CLICK");
      for (int k = 0; s && *s; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL, ++k) {
        double at;
        int x, y;
        if (k < ci || sscanf(s, "%lf:%d,%d", &at, &x, &y) != 3 || t < at) continue;
        ci = k + 1;
        SDL_Event ev = { 0 };
        ev.type = SDL_MOUSEBUTTONDOWN, ev.button.button = SDL_BUTTON_LEFT, ev.button.x = x, ev.button.y = y;
        SDL_PushEvent(&ev);
        ev.type = SDL_MOUSEBUTTONUP;
        SDL_PushEvent(&ev);
        break;
      }
      /* MT3DX_KEYS="<s>:<key name>;..." presses and lets go of that keyboard key (SDL_GetKeyFromName: Return, Left, Delete, Escape, A ...)
       * at those seconds: the keyboard path, bindings and all */
      s = getenv("MT3DX_KEYS");
      for (int k = 0; s && *s; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL, ++k) {
        static int ki;
        double at;
        char name[48];
        if (k < ki || sscanf(s, "%lf:%47[^;]", &at, name) != 2 || t < at) continue;
        ki = k + 1;
        SDL_Event ev = { 0 };
        ev.type = SDL_KEYDOWN, ev.key.keysym.sym = SDL_GetKeyFromName(name), ev.key.state = SDL_PRESSED;
        SDL_PushEvent(&ev);
        ev.type = SDL_KEYUP, ev.key.state = SDL_RELEASED;
        SDL_PushEvent(&ev);
        break;
      }
      s = getenv("MT3DX_RESIZE");
      for (int k = 0; s && *s; s = strchr(s, ';') ? strchr(s, ';') + 1 : NULL, ++k) {
        double at;
        int rw, rh;
        if (k < ri || sscanf(s, "%lf:%dx%d", &at, &rw, &rh) != 3 || t < at) continue;
        ri = k + 1;
        SDL_SetWindowSize(P.win, rw, rh);
        break;
      }
    }
    if (P.fs_want != P.fs_now) { /* the window mode the game options (or F11) chose */
      P.fs_now = P.fs_want;
      fprintf(stderr, "window mode: %s\n", P.fs_now ? "fullscreen" : "windowed");
      SDL_SetWindowFullscreen(P.win, P.fs_now ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    {
      static int shown = 1;
      if (P.cursor_hide == shown) shown = !P.cursor_hide, SDL_ShowCursor(shown ? SDL_ENABLE : SDL_DISABLE); /* (the pointer out of the way in a race) */
    }
    if (!SDL_WaitEventTimeout(&e, 50)) continue;
    switch (e.type) {
    case SDL_QUIT: P.quit = 1; break;
    case SDL_CONTROLLERDEVICEADDED: pad_open(e.cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: pad_close(e.cdevice.which); break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
      if (e.cbutton.which != wheel_instance()) pad_button(e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN); /* (the chosen wheel is read raw) */
      break;
    case SDL_CONTROLLERAXISMOTION:
      if (e.caxis.which != wheel_instance()) pad_axis(e.caxis.axis, e.caxis.value);
      break;
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
    case SDL_JOYAXISMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYHATMOTION: wheel_event(&e); break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        SDL_GetWindowSize(P.win, &P.win_w, &P.win_h);
        /* a window narrower than 4:3 is made 4:3 (the layout has nothing to show above it) */
        if (!(SDL_GetWindowFlags(P.win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) && P.win_w * 3 < P.win_h * 4)
          SDL_SetWindowSize(P.win, P.win_w, P.win_w * 3 / 4);
        else
          P.resized = 1;
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (!finger_down && e.button.button == SDL_BUTTON_LEFT) {
        left_down = 1;
        push_ev(0, TOUCH_BEGAN, to_layout_x(e.button.x), to_layout_y(e.button.y));
      }
      break;
    case SDL_MOUSEMOTION:
      if (!finger_down && left_down) push_ev(0, TOUCH_MOVED, to_layout_x(e.motion.x), to_layout_y(e.motion.y));
      break;
    case SDL_MOUSEBUTTONUP:
      if (!finger_down && e.button.button == SDL_BUTTON_LEFT && left_down) {
        left_down = 0;
        push_ev(0, TOUCH_ENDED, to_layout_x(e.button.x), to_layout_y(e.button.y));
      }
      break;
    /* Touchscreens (Switch, tablets): SDL reports the position normalised to
     * the window, so it goes through the same letterbox mapping as the mouse. */
    case SDL_FINGERDOWN:
      finger_down = 1;
      push_ev(0, TOUCH_BEGAN, to_layout_x((int)(e.tfinger.x * P.win_w)), to_layout_y((int)(e.tfinger.y * P.win_h)));
      break;
    case SDL_FINGERMOTION:
      if (finger_down) push_ev(0, TOUCH_MOVED, to_layout_x((int)(e.tfinger.x * P.win_w)), to_layout_y((int)(e.tfinger.y * P.win_h)));
      break;
    case SDL_FINGERUP:
      if (finger_down) {
        finger_down = 0;
        push_ev(0, TOUCH_ENDED, to_layout_x((int)(e.tfinger.x * P.win_w)), to_layout_y((int)(e.tfinger.y * P.win_h)));
      }
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int down = e.type == SDL_KEYDOWN, sym = (int)e.key.keysym.sym;
      if (down && sym == SDLK_ESCAPE && !app_bind_capturing()) P.quit = 1;
      if (down && !e.key.repeat && sym == SDLK_F3 && !app_bind_capturing()) push_ev(4, 0, 0, 0); /* F3: the input readout on and off */
      if (down && !e.key.repeat && sym == SDLK_F4 && !app_bind_capturing()) push_ev(5, 0, 0, 0); /* F4: the physics panel on and off */
      if (down && !e.key.repeat && sym == SDLK_F5 && !app_bind_capturing()) push_ev(6, 0, 0, 0); /* F5: the steering filter's tuning panel */
      if (down && !e.key.repeat && (sym == SDLK_F11 || (sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) && !app_bind_capturing()) push_ev(7, 0, 0, 0); /* F11 or Alt+Enter: windowed or fullscreen */
      if (getenv("MT3DX_LOG")) fprintf(stderr, "input: key %d %s\n", sym, down ? "down" : "up");
      push_input(BIND_KEY, sym, down | (e.key.repeat ? 2 : 0));
      break;
    }
    }
  }
  P.quit = 1;
  /* whatever hangs below (the audio device, the driver), the process still ends */
  SDL_Thread *dog = SDL_CreateThread(exit_watchdog, "exit watchdog", NULL);
  if (dog) SDL_DetachThread(dog);
  /* Give the game thread three seconds to stop. A thread stuck in a graphics driver never does, and a window that
   * cannot be closed is worse than an unclean exit. */
  for (int i = 0; i < 60 && !P.game_done; ++i) SDL_Delay(50);
  if (!P.game_done) {
    fprintf(stderr, "the game thread did not stop: leaving anyway\n");
    _exit(1);
  }
  SDL_WaitThread(P.game, NULL);
  if (P.audio) SDL_CloseAudioDevice(P.audio);
  SDL_Quit();
  if (P.log_file[0]) { /* (the log closed and scrubbed of public addresses, room codes and user names: it is attached to bug reports) */
    fflush(stderr);
    if (freopen("NUL", "w", stderr)) log_scrub_file(P.log_file);
  }
#ifdef __linux__
  /* the console (the game run by its systemd service, as the Pi's: INVOCATION_ID) shuts down with EXIT; a player run by hand just closes */
  if (P.exit_req && getenv("INVOCATION_ID") && !getenv("MT3DX_NO_POWEROFF")) {
    fprintf(stderr, "EXIT: shutting the console down\n");
    if (system("sudo -n systemctl poweroff") != 0) fprintf(stderr, "the shutdown did not start\n");
  }
#endif
  return P.fail ? 1 : 0;
}
