/* The versus's client (a port extra): UDP to the versus server (tools/vs_server.py, or a LAN
 * game's host, vs_host.c); its lobby, and the
 * race packets of the arcade's cabinet link (car_set_versus) through it. And the LAN's games
 * found. Everything runs on the caller's thread: net_poll reads what has arrived, never
 * blocking. */
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PORT 27015
#define NET_RAMPS 22 /* the join ramps (menu.h MENU_VS_RAMPS) a GO may name */
#define NET_PORT_TRIES 10 /* a host takes the first free port of NET_PORT .. NET_PORT + 9, or any if all are taken */
#define NET_PLAYERS 4
#define NET_NAME 16
#define NET_PROFILE 64
#define NET_FOUND 4

/* a player as the lobby shows it: its name and its own description of its car (net_profile) */
typedef struct {
  uint32_t id;
  int status; /* 0 in the lobby, 1 racing */
  char name[NET_NAME + 1];
  unsigned char profile[NET_PROFILE];
  int profile_len;
} net_player;

typedef struct {
  int connected;   /* the server has answered */
  int n, me, host; /* players; this one's and the host's place among them */
  net_player player[NET_PLAYERS];
} net_lobby;

/* a race the host started: the machines in it (this one's PCB id `pcb`), its start ramp, km and
 * random number */
typedef struct {
  uint32_t race;
  uint32_t seed;
  int ramp, km;
  int n, pcb;
  net_player player[NET_PLAYERS]; /* by PCB id */
  int song; /* the host's song + 2 for everyone's race (0: a host from before it) */
} net_go;

/* open the client to the server at `host` (dotted IPv4); 0 on success */
int net_open(const char *host, int port, uint32_t id);
void net_close(void);
int net_is_open(void);
/* read everything that has arrived; in the lobby say hello twice a second (status 0 lobby,
 * 1 racing) as `name` with `profile` */
void net_poll(double now, int status, const char *name, const unsigned char *profile, int profile_len);
const net_lobby *net_lobby_now(void);
/* seconds since the server last answered (large before it ever has) */
double net_silence(double now);
/* the host's start: the lobby races on `ramp` for `km`, everyone's BGM `song` (the GO's) */
void net_start(int ramp, int km, int song);
/* the host (an app's, vs_host) has closed its game: CLOSED came */
int net_host_closed(void);
/* a GO the server sent since the last call: 1 and *go filled in */
int net_take_go(net_go *go);
/* the race's packets: send this frame's; hand the newest received from each machine of the race to
 * car_link_deliver (the car_link_io's receive) */
void net_race_send(const unsigned char *packet, int len);
void net_race_receive(void);

/* The LAN's games: while on, ask the Wi-Fi who hosts (a broadcast a second) and keep the
 * answers; net_found_list copies them (their count) */
typedef struct {
  char ip[16];
  int port;
  char name[NET_NAME + 1];
  uint32_t id; /* its host's player */
  int n;       /* its players */
  double seen;
} net_found;
/* a game's room code from its address and port (15 digits), and the other way: an address typed with dots (the usual port) or a code; 0 on success */
void net_room_code(const char *ip, int port, char out[24]);
int net_room_parse(const char *text, char ip[16], int *port);
/* a random number from the system's own source (a player's id, a port) */
uint32_t net_random32(void);
void net_scan(double now, int on);
int net_found_list(net_found *out, int max);

#endif
