/* The Windows player has no EGL: the GL ES context comes from the driver's WGL (SDL creates it), and the extension
 * functions the core asks EGL for are looked up through SDL. This stands in for <EGL/egl.h> (see build.py). */
#ifndef WMMT_WIN_EGL_H
#define WMMT_WIN_EGL_H
#include <SDL.h>
#define eglGetProcAddress(name) SDL_GL_GetProcAddress(name)
#endif
