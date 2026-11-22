/* The versus host in the app (LAN versus): tools/vs_server.py's lobby and the
 * race packets' relay on UDP NET_PORT (every interface). Everything
 * runs on the caller's thread: vs_host_poll reads what has arrived, never blocking. */
#ifndef VS_HOST_H
#define VS_HOST_H

#include <stdint.h>

/* open the host; owner: the host's
 * own player's id, the lobby's host whenever it is in; 0 on success */
int vs_host_open(uint32_t owner, int online); /* online: a random high port first, not the usual one */
void vs_host_close(void);
int vs_host_is_open(void);
/* the UDP port it took (0 closed) */
int vs_host_port(void);
/* serve what has arrived; `name`: the game's name in the LAN's answers */
void vs_host_poll(double now, const char *name);

#endif
