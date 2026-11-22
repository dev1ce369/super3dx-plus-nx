/* Nintendo Switch entry point for Super3DX+.
 *
 * libnx wants a plain main(); the shared SDL2 player in platform/sdl2/main_sdl.c
 * already has one. The Makefile renames that one to sdl_main_entry() and this
 * file supplies the real main(), calling it with the SD-card paths used by
 * Switch homebrew. Everything else (window, GLES2 context, audio, input, the
 * game loop) is the normal SDL2 backend; there is no game logic here.
 *
 * Installed layout:
 *   sdmc:/switch/super3dx+nx/super3dx+nx.nro
 *   sdmc:/switch/super3dx+nx/data/     converted game data (not in the NRO)
 *   sdmc:/switch/super3dx+nx/save/     cards, settings and the log
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* switch_rules passes -Dmain=sdl_main_entry to every source. SDL_MAIN_HANDLED
 * (also on the command line) stops SDL_main.h from renaming it again. */
#undef main
extern int sdl_main_entry(int argc, char **argv);

#define BASE_DIR "sdmc:/switch/super3dx+nx"
#define DATA_DIR BASE_DIR "/data"
#define SAVE_DIR BASE_DIR "/save"

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  /* libnx's newlib accepts sdmc:/ paths in the POSIX calls. The save folder is
   * ours; the data folder must be copied to the card by the player. */
  mkdir(BASE_DIR, 0755);
  mkdir(SAVE_DIR, 0755);

  /* Menus are touch screens in the arcade game. switch-sdl2 reports the
   * touchscreen as SDL finger events; main_sdl.c maps those to game taps. */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  /* Start the log in the save folder: a Switch has no console, so a failed
   * start (missing data, for example) would otherwise be invisible. */
  {
    char log[512];
    snprintf(log, sizeof log, "%s/log.txt", SAVE_DIR);
    if (freopen(log, "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
  }

  /* The backend's own argv parser handles these, exactly as on the desktop. */
  char *args[] = {
    (char *)"super3dx+nx",
    (char *)DATA_DIR,
    (char *)"--save", (char *)SAVE_DIR,
    (char *)"--fullscreen",
    NULL
  };
  return sdl_main_entry((int)(sizeof args / sizeof args[0]) - 1, args);
}
