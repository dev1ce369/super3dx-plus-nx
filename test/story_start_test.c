/* Story episode start as app.c does it: the episode's rivals (RIVALDB), car_story_end, car_set_rivals,
 * car_start, then car_cutin_start. A recompiled-code abort prints its "rc: ..." line on stderr.
 * usage: story_start_test <data dir> <episode> <loop> [race] [update frames] */
#include <stdio.h>
#include <stdlib.h>
#include "../src/car.h"
#include "../src/cardb.h"
#include "../src/rivaldb.h"

static int speaks, ends;
static void sp(int pos, int row, int anim, int flash_anim, int flash, int x) { ++speaks; }
static void en(int pos) { ++ends; }

static int pack_of_type(int type) {
  for (int mk = 0; mk < CARDB_ALL; ++mk)
    for (int ci = 0; ci < CARDB_NCARS[mk]; ++ci)
      for (int k = 0; k < CARDB_CARS[mk][ci].ncolors; ++k)
        if (CARDB_CARS[mk][ci].colors[k].type == type) return CARDB_CARS[mk][ci].colors[k].pack;
  return 0;
}

int main(int argc, char **argv) {
  char err[256];
  if (argc < 4) { puts("story_start_test data episode loop [race] [frames]"); return 1; }
  if (car_init(argv[1], err, sizeof err)) { puts(err); return 1; }
  int ep = atoi(argv[2]), loop = atoi(argv[3]), race = argc > 4 ? atoi(argv[4]) : 0, frames = argc > 5 ? atoi(argv[5]) : 0;
  car_rival rv[CAR_RIVALS];
  int n = 0;
  for (int i = 0; i < RIVALDB_N; ++i) {
    const rivaldb_rival *r = &RIVALDB[i];
    if (r->select != ep || r->race != race || r->slot < 0 || r->slot >= CAR_RIVALS || n >= CAR_RIVALS) continue;
    int pack = pack_of_type(r->type);
    car_rival *o = &rv[n++];
    *o = (car_rival){ (unsigned)r->type, { 0 }, (int)CARDB_TYRES[pack][6], r->character, loop, 0, r->series };
    for (int j = 0; j < 6; ++j) o->tyres[j] = CARDB_TYRES[pack][j];
  }
  car_story_end(1);
  car_set_rivals(rv, n);
  car_state c;
  car_spec me = CAR_SPEC_FD3S;
  if (car_start(&c, 0, 0, &me)) { puts("start failed"); return 1; }
  static const car_cutin_cb cb = { sp, en };
  const char *f = car_cutin_start(ep, loop, &cb);
  car_input in = { 0 };
  for (int i = 0; i < frames; ++i) { in.throttle = 1.0f; car_update(&c, &in); car_cutin_update(); }
  printf("episode %d loop %d race %d (%d rivals): file %s, lines %d ended %d\n", ep, loop, race, n, f ? f : "(none)", speaks, ends);
  return 0;
}
