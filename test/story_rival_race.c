/* A story episode's first race against its CPU rivals, set up as app.c does (RIVALDB rows, car_story_end,
 * car_set_rivals, car_start), the player holding the throttle. One summary line per episode.
 * usage: story_rival_race <data dir> <episode> [frames] */
#include <stdio.h>
#include <stdlib.h>
#include "../src/car.h"
#include "../src/cardb.h"
#include "../src/rivaldb.h"

static int pack_of_type(int type) {
  for (int mk = 0; mk < CARDB_ALL; ++mk)
    for (int ci = 0; ci < CARDB_NCARS[mk]; ++ci)
      for (int k = 0; k < CARDB_CARS[mk][ci].ncolors; ++k)
        if (CARDB_CARS[mk][ci].colors[k].type == type) return CARDB_CARS[mk][ci].colors[k].pack;
  return 0;
}

int main(int argc, char **argv) {
  char err[256];
  if (argc < 3) { puts("story_rival_race data episode [frames]"); return 1; }
  if (car_init(argv[1], err, sizeof err)) { puts(err); return 1; }
  int ep = atoi(argv[2]), frames = argc > 3 ? atoi(argv[3]) : 1200;
  car_rival rv[CAR_RIVALS];
  int n = 0, first_series = -1;
  for (int i = 0; i < RIVALDB_N; ++i) {
    const rivaldb_rival *r = &RIVALDB[i];
    if (r->select != ep || r->race != 0 || r->slot < 0 || r->slot >= CAR_RIVALS || n >= CAR_RIVALS) continue;
    int pack = pack_of_type(r->type);
    car_rival *o = &rv[n++];
    *o = (car_rival){ (unsigned)r->type, { 0 }, (int)CARDB_TYRES[pack][6], r->character, 0, 0, r->series };
    for (int j = 0; j < 6; ++j) o->tyres[j] = CARDB_TYRES[pack][j];
    if (first_series < 0) first_series = r->series;
  }
  if (!n) { printf("episode %d: no rivals\n", ep); return 0; }
  car_story_end(1);
  car_set_rivals(rv, n);
  car_state c;
  car_spec me = CAR_SPEC_FD3S;
  if (car_start(&c, 0, 0, &me)) { printf("episode %d: start failed\n", ep); return 1; }
  car_input in = { 0 };
  for (int i = 0; i < frames; ++i) { in.throttle = 1.0f; car_update(&c, &in); c.impact = 0; }
  printf("episode %d series %d (%d rivals, types", ep, first_series, n);
  for (int i = 0; i < n; ++i) printf(" %u/c%d", rv[i].type, rv[i].character);
  printf("): me %.0f %.0f %.1f km/h, gap %.1f, rival %.0f %.0f, state %d\n", c.pos[0], c.pos[2], car_kmh(&c), c.race.gap,
         c.rival_body[0][12], c.rival_body[0][14], c.race.state);
  return 0;
}
