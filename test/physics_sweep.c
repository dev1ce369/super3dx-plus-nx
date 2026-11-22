/* Physics sweep, for comparing builds of the arcade `main` (tools/dev/compare_builds.sh): every distinct car of
 * the car select (ECarType, gears, its tyres) driven on three courses with the automatic gearbox, full throttle,
 * 900 frames. One line per car and course, keyed by the car's label then its type, so two builds' outputs diff;
 * ignore the label (regions name some cars differently) and compare the rest.
 *
 *   cc -O1 -ffp-contract=off -DRC_FPT=double -Isrc -Isrc/phys test/physics_sweep.c src/car.c \
 *      src/phys/rc_runtime.c src/phys/phys_rc.c -lm
 *   physics_sweep <data dir>
 *
 * The story rival races are story_rival_race.c's. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../src/car.h"
#include "../src/cardb.h"

/* cars only one region has (the export build ships the Gemballa cars' folders empty) */
static int skipped(const char *label) { return strstr(label, "Gemballa") || strstr(label, "GEMBALLA"); }

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  alarm(900); /* a stuck run must not hang whoever runs this */
  char err[256];
  if (car_init(argc > 1 ? argv[1] : "data", err, sizeof err)) { puts(err); return 1; }
  int seen[512][2], nseen = 0, n_cars = 0, n_fail = 0;
  for (int mk = 0; mk < CARDB_ALL; ++mk)
    for (int ci = 0; ci < CARDB_NCARS[mk]; ++ci) {
      const cardb_car *car = &CARDB_CARS[mk][ci];
      if (skipped(car->label)) continue;
      for (int k = 0; k < car->ncolors; ++k) {
        const cardb_color *col = &car->colors[k];
        int dup = 0;
        for (int s = 0; s < nseen; ++s) dup |= seen[s][0] == col->type && seen[s][1] == col->pack;
        if (dup || nseen >= 512) continue;
        seen[nseen][0] = col->type;
        seen[nseen][1] = col->pack;
        ++nseen;
        car_spec sp = CAR_SPEC_FD3S;
        sp.type = (unsigned)col->type;
        sp.gears = col->gears;
        for (int i = 0; i < 6; ++i) sp.tyres[i] = CARDB_TYRES[col->pack][i];
        sp.spokes = (int)CARDB_TYRES[col->pack][6];
        for (int course = 0; course < 3; ++course) {
          car_state c;
          if (car_start(&c, course, 0, &sp)) {
            printf("%s|t%d|c%d start failed\n", car->label, col->type, course);
            ++n_fail;
            continue;
          }
          car_input in = { 0 };
          printf("%s|t%d|c%d", car->label, col->type, course);
          for (int i = 0; i < 900; ++i) {
            in.throttle = 1.0f;
            car_update(&c, &in);
            c.impact = 0;
            if (i % 300 == 299) printf(" | %.2f km/h %.0f rpm g%d %.0f %.0f", car_kmh(&c), c.rpm, c.gear, c.pos[0], c.pos[2]);
          }
          printf("\n");
        }
        ++n_cars;
      }
    }
  printf("cars swept: %d, failed starts: %d\n", n_cars, n_fail);
  return n_fail != 0;
}
