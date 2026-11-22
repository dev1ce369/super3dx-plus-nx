/* The menu and race music of the SDL2 player: sound/bgm/<name>.ogg in the data folder (tools/pipeline.py convert, step bgm, writes
 * them; the .m4a files are Android's), played over and over. A thread of its own decodes a track whole into memory, the
 * audio callback adds it to sound.c's mix. */
#ifndef BGM_H
#define BGM_H

/* Start the decoding thread; data_dir holds sound/bgm/ */
void bgm_init(const char *data_dir);
/* The track to play (app_bgm's name, without extension), NULL for none. The same name again is no change. */
void bgm_play(const char *name);
/* The track to switch to at once (the 10-in-a-row's next loop): decoded ahead, then crossfaded in over 0.6 s from its start. NULL, or
 * the one playing, drops the queue. */
void bgm_next(const char *name);
/* the track playing now, into out (n bytes) */
void bgm_current(char *out, int n);
/* The music's level over its own (app_bgm_level), 0..1 */
void bgm_level(float level);
/* The audio callback's: add `frames` of the music to out (interleaved stereo int16, sound.c's mix already in it) */
void bgm_mix(short *out, int frames);

#endif
