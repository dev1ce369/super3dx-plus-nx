/* Renders a car's engine sound offline: the arcade physics at full throttle (automatic gearbox) with sound.c's
 * mixer, to a 44.1 kHz stereo WAV and a per-half-second log of rpm, gear, speed and the turbine's volume.
 *
 *   zig cc -O1 -ffp-contract=off -DRC_FPT=double -Isrc -Isrc/phys test/audio_render.c src/sound.c src/car.c \
 *      src/phys/rc_runtime.c src/phys/phys_rc.c -lm -o audio_render
 *   audio_render <data dir> <ECarType> <gears> <power> <seconds> <out.wav> [course]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/car.h"
#include "../src/cardb.h"
#include "../src/sound.h"

int main(int argc, char **argv) {
  if (argc < 7) return fprintf(stderr, "usage: audio_render data type gears power seconds out.wav [course]\n"), 2;
  char err[256];
  if (car_init(argv[1], err, sizeof err)) return puts(err), 1;
  snd_init(argv[1]);
  car_spec sp = CAR_SPEC_FD3S;
  sp.type = (unsigned)atoi(argv[2]);
  sp.gears = atoi(argv[3]);
  sp.power = atoi(argv[4]);
  int secs = atoi(argv[5]);
  for (int mk = 0; mk < CARDB_ALL; ++mk)
    for (int ci = 0; ci < CARDB_NCARS[mk]; ++ci)
      for (int k = 0; k < CARDB_CARS[mk][ci].ncolors; ++k) {
        const cardb_color *c = &CARDB_CARS[mk][ci].colors[k];
        if ((unsigned)c->type == sp.type && c->gears == sp.gears && sp.tyres[0] == CAR_SPEC_FD3S.tyres[0]) {
          for (int i = 0; i < 6; ++i) sp.tyres[i] = CARDB_TYRES[c->pack][i];
          sp.spokes = (int)CARDB_TYRES[c->pack][6];
        }
      }
  car_state c;
  if (car_start(&c, argc > 7 ? atoi(argv[7]) : 0, 0, &sp)) return puts("start failed"), 1;
  snd_set_car(sp.type, argc > 8 ? atoi(argv[8]) : sp.power); /* (argv[8]: the sound set\x27s power tune, 0 = stock) */
  FILE *o = fopen(argv[6], "wb");
  int total = secs * 44100;
  unsigned hdr[11] = { 0x46464952, 36 + total * 4, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, (unsigned)total * 4 };
  fwrite(hdr, 4, 11, o);
  car_input in = { 0 };
  static short buf[44100 / 60 * 2 + 8];
  double carry = 0;
  for (int f = 0; f < secs * 60; ++f) {
    in.throttle = 1.0f;
    car_update(&c, &in);
    c.impact = 0;
    snd_update(&c, &in, 1.0f / 60.0f);
    carry += 44100.0 / 60.0;
    int n = (int)carry;
    carry -= n;
    snd_mix(buf, n);
    fwrite(buf, 4, (size_t)n, o);
    if (f % 30 == 0)
      printf("t %5.1f  %6.1f km/h  %5.0f rpm  g%d  turbo %d vol %.2f hz %.0f  air %.2f\n", f / 60.0, car_kmh(&c), c.rpm, c.gear,
             c.se.turbo, c.se.turbin_vol, c.se.turbin_hz, c.se.air_vol);
  }
  fclose(o);
  return 0;
}

int net_log_on(void) { return 0; } /* (car.c's netcode log, unused here) */

