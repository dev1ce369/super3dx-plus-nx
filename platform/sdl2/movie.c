#include "movie.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

/* the .wmv file (little endian): "WMV1", u32 width, height, fps (frames a second), frames, the soundtrack's offset and size (an Ogg
 * Vorbis file, 44.1 kHz stereo), then the frames' u32 offsets (frames + 1 of them, the last the file's end), then the data: the
 * soundtrack and one baseline JPEG per frame. */
#define RATE 44100

static struct {
  FILE *f;
  int open, done, w, h, fps, n, last;
  unsigned *off;
  short *pcm;
  long frames; /* the soundtrack's length in frames */
  volatile long pos; /* the frames of it mixed out so far (the audio thread) */
  SDL_mutex *mx;
  double t0; /* the wall clock at the start: the clock until the audio runs */
  unsigned char *jpg;
  size_t jpg_cap;
  unsigned tex, prog, vbo;
  int u_rect, texw, texh;
} M;

static double now_s(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

static unsigned rd32(FILE *f) {
  unsigned char b[4];
  if (fread(b, 1, 4, f) != 4) return 0;
  return b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24;
}

/* (bgm.c holds stb_vorbis) */
extern int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);

static unsigned shader(GLenum type, const char *src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  return s;
}

int movie_active(void) { return M.open; }
int movie_finished(void) { return M.open && M.done; }

void movie_stop(void) {
  if (!M.open) return;
  SDL_LockMutex(M.mx);
  M.open = 0;
  free(M.pcm);
  M.pcm = NULL;
  M.frames = 0;
  SDL_UnlockMutex(M.mx);
  if (M.f) fclose(M.f);
  M.f = NULL;
  free(M.off);
  M.off = NULL;
}

int movie_start(const char *data_dir, int index) {
  static const char *const NAMES[3] = { "intro", "playdemo", "tutorial" };
  if (index < 0 || index > 2) return -1;
  movie_stop();
  if (!M.mx) M.mx = SDL_CreateMutex();
  char path[1100];
  snprintf(path, sizeof path, "%s/%s.wmv", data_dir, NAMES[index]);
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  unsigned hdr[7];
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "WMV1", 4)) { fclose(f); return -1; }
  for (int i = 0; i < 7; ++i) hdr[i] = rd32(f);
  unsigned w = hdr[0], h = hdr[1], fps = hdr[2], n = hdr[3], aoff = hdr[4], asize = hdr[5];
  if (!w || !h || w > 2048 || h > 2048 || !fps || !n || n > 200000 || !asize) { fclose(f); return -1; }
  unsigned *off = malloc(4 * (size_t)(n + 1));
  unsigned char *ogg = malloc(asize);
  if (!off || !ogg) { free(off); free(ogg); fclose(f); return -1; }
  for (unsigned i = 0; i <= n; ++i) off[i] = rd32(f);
  short *pcm = NULL;
  int ch = 0, rate = 0, got = 0;
  if (fseek(f, (long)aoff, SEEK_SET) == 0 && fread(ogg, 1, asize, f) == asize) got = stb_vorbis_decode_memory(ogg, (int)asize, &ch, &rate, &pcm);
  free(ogg);
  if (got <= 0 || ch != 2 || rate != RATE) { /* (the soundtrack is made 44.1 kHz stereo) */
    fprintf(stderr, "movie %s: no usable soundtrack (%d frames, %d ch, %d Hz)\n", path, got, ch, rate);
    free(pcm);
    free(off);
    fclose(f);
    return -1;
  }
  M.f = f, M.off = off, M.pcm = pcm, M.frames = got;
  M.w = (int)w, M.h = (int)h, M.fps = (int)fps, M.n = (int)n, M.last = -1, M.done = 0, M.pos = 0;
  M.t0 = now_s();
  M.open = 1;
  fprintf(stderr, "movie %s: %dx%d, %d frames at %d fps, %.1f s of sound\n", path, M.w, M.h, M.n, M.fps, (double)got / RATE);
  return 0;
}

void movie_mix(short *out, int frames) {
  if (!M.mx) return;
  SDL_LockMutex(M.mx);
  if (M.open && M.pcm) {
    long left = M.frames - M.pos;
    int k = left < frames ? (int)(left > 0 ? left : 0) : frames;
    const short *p = M.pcm + 2 * M.pos;
    for (int i = 0; i < 2 * k; ++i) {
      int v = out[i] + p[i];
      out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    M.pos += k;
  }
  SDL_UnlockMutex(M.mx);
}

void movie_draw(int width, int height) {
  if (!M.open) return;
  /* the clock: the soundtrack once it runs (the audio device's own timing), else the wall clock */
  long pos = M.pos;
  double t = pos > 0 ? (double)pos / RATE : now_s() - M.t0;
  int frame = (int)(t * M.fps);
  if (frame >= M.n) {
    if (pos >= M.frames || pos == 0) M.done = 1; /* (no audio device: the picture alone decides) */
    frame = M.n - 1;
  }
  /* the GL state we touch, put back after */
  GLint prog, abuf, vao, tex, vp[4], act;
  GLboolean blend, depth, stencil, cull, scissor;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
  glGetIntegerv(GL_VIEWPORT, vp);
  blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
  cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST);
  if (!M.prog) {
    unsigned vs = shader(GL_VERTEX_SHADER, "attribute vec2 p; uniform vec4 rect; varying vec2 uv;\n"
                                           "void main() { uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); gl_Position = vec4(mix(rect.xy, rect.zw, p * 0.5 + 0.5), 0.0, 1.0); }\n");
    unsigned fs = shader(GL_FRAGMENT_SHADER, "precision mediump float; uniform sampler2D t; varying vec2 uv;\n"
                                             "void main() { gl_FragColor = vec4(texture2D(t, uv).rgb, 1.0); }\n");
    M.prog = glCreateProgram();
    glAttachShader(M.prog, vs);
    glAttachShader(M.prog, fs);
    glBindAttribLocation(M.prog, 0, "p");
    glLinkProgram(M.prog);
    M.u_rect = glGetUniformLocation(M.prog, "rect");
    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers(1, &M.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, M.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glGenTextures(1, &M.tex);
  }
  glBindTexture(GL_TEXTURE_2D, M.tex);
  if (frame != M.last && frame >= 0) {
    unsigned size = M.off[frame + 1] - M.off[frame];
    if (size > M.jpg_cap) {
      free(M.jpg);
      M.jpg = malloc(M.jpg_cap = size + size / 2);
    }
    int w, h, comp;
    unsigned char *px = NULL;
    if (M.jpg && fseek(M.f, (long)M.off[frame], SEEK_SET) == 0 && fread(M.jpg, 1, size, M.f) == size)
      px = stbi_load_from_memory(M.jpg, (int)size, &w, &h, &comp, 3);
    if (px && w == M.w && h == M.h) {
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      if (M.texw != w || M.texh != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        M.texw = w, M.texh = h;
      } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
      }
      glPixelStorei(GL_UNPACK_ALIGNMENT, 2); /* (the app's) */
      M.last = frame;
    }
    stbi_image_free(px);
  }
  /* centred at the movie's own shape, black around */
  float sx = 1.0f, sy = 1.0f, a = (float)width / (float)height, m = (float)M.w / (float)M.h;
  if (a > m) sx = m / a;
  else sy = a / m;
  glViewport(0, 0, width, height);
  glDisable(GL_BLEND), glDisable(GL_DEPTH_TEST), glDisable(GL_STENCIL_TEST), glDisable(GL_CULL_FACE), glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindVertexArrayOES(0);
  glUseProgram(M.prog);
  glUniform4f(M.u_rect, -sx, -sy, sx, sy);
  glBindBuffer(GL_ARRAY_BUFFER, M.vbo);
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
}
