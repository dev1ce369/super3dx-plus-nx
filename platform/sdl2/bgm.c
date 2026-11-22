#include "bgm.h"

#ifdef __ANDROID__
/* Android: no SDL, the same few calls on pthreads (the music decoded whole and wrapped sample-accurately, as the SDL player's: a MediaPlayer's
 * loop leaves a gap) */
#include <pthread.h>
#include <stdlib.h>
typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
typedef pthread_t SDL_Thread;
static SDL_mutex *SDL_CreateMutex(void) {
  SDL_mutex *m = malloc(sizeof *m);
  pthread_mutex_init(m, NULL);
  return m;
}
static SDL_cond *SDL_CreateCond(void) {
  SDL_cond *c = malloc(sizeof *c);
  pthread_cond_init(c, NULL);
  return c;
}
#define SDL_LockMutex(m) pthread_mutex_lock(m)
#define SDL_UnlockMutex(m) pthread_mutex_unlock(m)
#define SDL_CondWait(c, m) pthread_cond_wait(c, m)
#define SDL_CondSignal(c) pthread_cond_signal(c)
typedef int (*sdl_thread_fn)(void *);
typedef struct { sdl_thread_fn fn; void *arg; } sdl_thread_start;
static void *sdl_thread_main(void *p) {
  sdl_thread_start s = *(sdl_thread_start *)p;
  free(p);
  s.fn(s.arg);
  return NULL;
}
static SDL_Thread *SDL_CreateThread(sdl_thread_fn fn, const char *name, void *arg) {
  (void)name;
  sdl_thread_start *s = malloc(sizeof *s);
  s->fn = fn, s->arg = arg;
  SDL_Thread *t = malloc(sizeof *t);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, 1 << 20);
  if (pthread_create(t, &at, sdl_thread_main, s) != 0) {
    free(s);
    free(t);
    return NULL;
  }
  return t;
}
#define SDL_DetachThread(t) pthread_detach(*(t))
#else
#include <SDL.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define OUT_RATE 44100
#define CHUNK 4096        /* source frames decoded at a time */
#define FADE_IN_S 0.3f    /* a new track comes up over this long */
#define XFADE_S 0.6f      /* the 10-in-a-row's next loop: crossfaded over this long, as soon as it is decoded */

/* Everything below `m` is shared by the game thread (play, level), the audio callback (mix) and the decoder. A track is
 * made whole in `buf` (interleaved stereo int16 at OUT_RATE): the decoder appends under the lock and the callback plays
 * what is filled so far; once complete it wraps to the start. */
static struct {
  SDL_mutex *m;
  SDL_cond *cv;
  char dir[1024], want[96];
  char next[96];  /* the track queued to follow this one when it ends (bgm_next) */
  short *nbuf;    /* ...decoded whole ahead, with its length, once ready */
  int nlen, nserial;
  short *obuf;    /* the track going out under a crossfade, its length, place and the frames of the fade done */
  int olen, opos, xf;
  int serial; /* each play request */
  short *buf;
  int len, filled, pos, complete;
  float level, fade;
  float gain; /* the tracks are mastered close to full scale: headroom for the effects (MT3DX_BGM_GAIN) */
  volatile int ready;
} B;

static void *xmalloc_frames(int frames) { return malloc((size_t)frames * 4 + 16); }

static int decoder(void *arg) {
  (void)arg;
  int done = 0;
  static short tmp[CHUNK * 2];
  static short out[(CHUNK * 2 + 64) * 2];
  for (;;) {
    char name[96], path[1200];
    SDL_LockMutex(B.m);
    while (B.serial == done) SDL_CondWait(B.cv, B.m);
    int my = done = B.serial;
    snprintf(name, sizeof name, "%s", B.want);
    SDL_UnlockMutex(B.m);
    if (!name[0]) continue;

    snprintf(path, sizeof path, "%s/sound/bgm/%s.ogg", B.dir, name);
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) {
      fprintf(stderr, "bgm: cannot play %s (no .ogg: run the BGM step of the converter) [%d]\n", name, err);
      continue;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    unsigned total_src = stb_vorbis_stream_length_in_samples(v);
    double ratio = (double)info.sample_rate / OUT_RATE; /* source frames per output frame */
    int cap = (int)((double)total_src / ratio) + 64;
    short *buf = xmalloc_frames(cap);
    if (!buf) {
      stb_vorbis_close(v);
      continue;
    }
    SDL_LockMutex(B.m);
    if (B.serial != my) { /* asked for another already */
      SDL_UnlockMutex(B.m);
      free(buf);
      stb_vorbis_close(v);
      continue;
    }
    free(B.buf);
    B.buf = buf;
    B.len = cap;
    B.filled = B.pos = B.complete = 0;
    B.fade = 0;
    SDL_UnlockMutex(B.m);

    double rpos = 0; /* in source frames of the chunk; -1 is the one before it */
    short prev[2] = { 0, 0 };
    int aborted = 0;
    for (;;) {
      int n = stb_vorbis_get_samples_short_interleaved(v, 2, tmp, CHUNK * 2);
      if (n <= 0) break;
      int m = 0;
      if (info.sample_rate == OUT_RATE) {
        memcpy(out, tmp, (size_t)n * 4);
        m = n;
      } else { /* linear interpolation between the source frames either side */
        for (;;) {
          int i = (int)floor(rpos);
          if (i + 1 > n - 1) break;
          float f = (float)(rpos - i);
          for (int c = 0; c < 2; ++c) {
            float a = i < 0 ? prev[c] : tmp[2 * i + c], b = tmp[2 * (i + 1) + c];
            out[2 * m + c] = (short)(a + (b - a) * f);
          }
          ++m;
          rpos += ratio;
        }
        rpos -= n;
        prev[0] = tmp[2 * (n - 1)];
        prev[1] = tmp[2 * (n - 1) + 1];
      }
      SDL_LockMutex(B.m);
      if (B.serial != my) {
        aborted = 1;
      } else {
        if (m > B.len - B.filled) m = B.len - B.filled;
        memcpy(B.buf + 2 * B.filled, out, (size_t)m * 4);
        B.filled += m;
      }
      SDL_UnlockMutex(B.m);
      if (aborted) break;
    }
    stb_vorbis_close(v);
    SDL_LockMutex(B.m);
    if (B.serial == my) {
      B.len = B.filled;
      B.complete = 1;
    }
    SDL_UnlockMutex(B.m);
  }
  return 0;
}

/* A whole track decoded in one go (for the queued next one): interleaved stereo at OUT_RATE, its frames in *len; NULL if it cannot. */
static short *decode_full(const char *name, int *len) {
  char path[1200];
  snprintf(path, sizeof path, "%s/sound/bgm/%s.ogg", B.dir, name);
  int err = 0;
  stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
  if (!v) return NULL;
  stb_vorbis_info info = stb_vorbis_get_info(v);
  double ratio = (double)info.sample_rate / OUT_RATE;
  int cap = (int)((double)stb_vorbis_stream_length_in_samples(v) / ratio) + 64, filled = 0;
  short *buf = xmalloc_frames(cap);
  static short tmp2[CHUNK * 2];
  short out2[(CHUNK * 2 + 64) * 2];
  double rpos = 0;
  short prev[2] = { 0, 0 };
  while (buf) {
    int n = stb_vorbis_get_samples_short_interleaved(v, 2, tmp2, CHUNK * 2);
    if (n <= 0) break;
    int m = 0;
    if (info.sample_rate == OUT_RATE) {
      memcpy(out2, tmp2, (size_t)n * 4);
      m = n;
    } else {
      for (;;) {
        int i = (int)floor(rpos);
        if (i + 1 > n - 1) break;
        float f = (float)(rpos - i);
        for (int c = 0; c < 2; ++c) {
          float a = i < 0 ? prev[c] : tmp2[2 * i + c], b2 = tmp2[2 * (i + 1) + c];
          out2[2 * m + c] = (short)(a + (b2 - a) * f);
        }
        ++m;
        rpos += ratio;
      }
      rpos -= n;
      prev[0] = tmp2[2 * (n - 1)];
      prev[1] = tmp2[2 * (n - 1) + 1];
    }
    if (m > cap - filled) m = cap - filled;
    memcpy(buf + 2 * filled, out2, (size_t)m * 4);
    filled += m;
  }
  stb_vorbis_close(v);
  *len = filled;
  return buf;
}

typedef struct { char name[96]; int serial; } next_job;
static int next_decoder(void *arg) {
  next_job *j = arg;
  static SDL_mutex *one; /* (one at a time: the decode buffers above are shared) */
  (void)one;
  int len = 0;
  short *buf = decode_full(j->name, &len);
  SDL_LockMutex(B.m);
  if (buf && B.nserial == j->serial) {
    free(B.nbuf);
    B.nbuf = buf;
    B.nlen = len;
  } else {
    free(buf);
  }
  SDL_UnlockMutex(B.m);
  free(j);
  return 0;
}

void bgm_next(const char *name) {
  if (!B.ready) return;
  SDL_LockMutex(B.m);
  if (!name || !strcmp(name, B.want)) { /* nothing to follow: the one playing, or none */
    if (B.next[0]) B.nserial++, free(B.nbuf), B.nbuf = NULL;
    B.next[0] = 0;
    SDL_UnlockMutex(B.m);
    return;
  }
  if (!strcmp(name, B.next)) {
    SDL_UnlockMutex(B.m);
    return;
  }
  snprintf(B.next, sizeof B.next, "%s", name);
  B.nserial++;
  free(B.nbuf);
  B.nbuf = NULL;
  next_job *j = malloc(sizeof *j);
  snprintf(j->name, sizeof j->name, "%s", name);
  j->serial = B.nserial;
  SDL_UnlockMutex(B.m);
  SDL_Thread *t = j ? SDL_CreateThread(next_decoder, "bgm next", j) : NULL;
  if (t) SDL_DetachThread(t);
  else free(j);
}

void bgm_init(const char *data_dir) {
  snprintf(B.dir, sizeof B.dir, "%s", data_dir);
  B.m = SDL_CreateMutex();
  B.cv = SDL_CreateCond();
  B.level = 1.0f;
  B.gain = getenv("MT3DX_BGM_GAIN") ? (float)atof(getenv("MT3DX_BGM_GAIN")) : 0.6f;
  SDL_Thread *t = SDL_CreateThread(decoder, "bgm decoder", NULL);
  if (t) SDL_DetachThread(t);
  B.ready = t != NULL;
}

void bgm_play(const char *name) {
  if (!B.ready) return;
  SDL_LockMutex(B.m);
  const char *now = B.want;
  if ((!name && !now[0]) || (name && !strcmp(name, now))) {
    SDL_UnlockMutex(B.m);
    return;
  }
  snprintf(B.want, sizeof B.want, "%s", name ? name : "");
  free(B.obuf);
  B.obuf = NULL;
  B.next[0] = 0;
  B.nserial++;
  free(B.nbuf);
  B.nbuf = NULL;
  B.serial++;
  free(B.buf); /* (the decoder writes only under this lock and for its own serial) */
  B.buf = NULL;
  B.len = B.filled = B.pos = B.complete = 0;
  SDL_CondSignal(B.cv);
  SDL_UnlockMutex(B.m);
}

/* the name of the track playing now (with the queued next one it moves to), into out */
void bgm_current(char *out, int n) {
  if (!B.ready) { out[0] = 0; return; }
  SDL_LockMutex(B.m);
  snprintf(out, (size_t)n, "%s", B.want);
  SDL_UnlockMutex(B.m);
}

void bgm_level(float level) { B.level = level < 0 ? 0 : level > 1 ? 1 : level; }

void bgm_mix(short *out, int frames) {
  if (!B.ready) return;
  SDL_LockMutex(B.m);
  if (B.next[0] && B.nbuf && B.buf && B.complete) { /* the next loop is ready: from its start, the one playing fading out under it */
    free(B.obuf);
    B.obuf = B.buf;
    B.olen = B.filled;
    B.opos = B.pos;
    B.xf = 0;
    B.buf = B.nbuf;
    B.len = B.filled = B.nlen;
    B.pos = 0;
    B.nbuf = NULL;
    snprintf(B.want, sizeof B.want, "%s", B.next);
    B.next[0] = 0;
    B.fade = 1.0f;
  }
  if (B.buf) {
    const float fade_step = 1.0f / (FADE_IN_S * OUT_RATE);
    for (int i = 0; i < frames; ++i) {
      if (B.pos >= B.filled) {
        if (!B.complete || B.len <= 0) break; /* (still being decoded: a gap, not a stall) */
        B.pos = 0;                            /* the loop */
      }
      float g = B.level * B.fade * B.gain, go = 0, xin = 1.0f;
      if (B.fade < 1.0f) B.fade += fade_step;
      if (B.obuf) { /* the crossfade: equal power */
        float x = (float)B.xf / (XFADE_S * OUT_RATE);
        xin = sinf(x * 1.5707963f);
        go = B.level * B.gain * cosf(x * 1.5707963f);
        if (B.opos >= B.olen) B.opos = 0;
        ++B.xf;
      }
      for (int c = 0; c < 2; ++c) {
        float old = B.obuf && B.olen > 0 ? B.obuf[2 * B.opos + c] * go : 0;
        int s = out[2 * i + c] + (int)(B.buf[2 * B.pos + c] * g * xin + old);
        out[2 * i + c] = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
      }
      ++B.pos;
      if (B.obuf) {
        ++B.opos;
        if (B.xf >= (int)(XFADE_S * OUT_RATE)) {
          free(B.obuf);
          B.obuf = NULL;
        }
      }
    }
  }
  SDL_UnlockMutex(B.m);
}
