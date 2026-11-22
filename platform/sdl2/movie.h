/* The attract loop's movies (the arcade's seqAtructMovie: atruct.bik, playdemo.bik, tutorial.bik over their soundtracks) for the SDL2 player:
 * data_dir/intro.wmv, playdemo.wmv, tutorial.wmv, made by tools/pipeline.py (convert, step movies) (JPEG frames with the soundtrack in
 * Ogg Vorbis; the file's layout is in movie.c). Android and iOS play the same movies as MP4 through the system's player. */
#ifndef MOVIE_H
#define MOVIE_H

/* Open movie `index` (app_movie's: 0 intro, 1 playdemo, 2 tutorial) and start it; 0 if it plays, -1 if there is no file or it is bad
 * (the caller then ends it at once) */
int movie_start(const char *data_dir, int index);
/* Stop it and free it (a press on START, or its end) */
void movie_stop(void);
/* Whether one is open, and whether it has played to its end (the last frame shown and the soundtrack over) */
int movie_active(void);
int movie_finished(void);
/* The game thread, GL context current: decode the frame its soundtrack is at into a texture and draw it over the whole
 * framebuffer (width x height), centred in 4:3 with black bars, GL state put back after */
void movie_draw(int width, int height);
/* The audio callback's: add `frames` of its soundtrack to out (interleaved stereo int16, the mix already in it) */
void movie_mix(short *out, int frames);

#endif
