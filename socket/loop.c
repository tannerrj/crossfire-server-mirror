/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 1999-2014 Mark Wedel and the Crossfire Development Team
 * Copyright (c) 1992 Frank Tore Johansen
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

/**
 * \file
 * Main client/server loops.
 *
 * \date 2003-12-02
 *
 * Mainly deals with initialization and higher level socket
 * maintenance (checking for lost connections and if data has arrived.)
 * The reading of data is handled in lowlevel.c
 */

#include "global.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#ifndef WIN32 /* ---win32 exclude unix headers */
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#else
#include <winsock2.h>
#endif /* end win32 */

#include "image.h"
#include "newserver.h"
#include "sockproto.h"
#include "sproto.h"
#include "server.h"

extern int listen_socket_count;
static void send_delayed_buffers(player *pl);
static void send_updates(player *pl);

pthread_mutex_t global_lock;

/*****************************************************************************
 * Start of command dispatch area.
 * The commands here are protocol commands.
 ****************************************************************************/

/* Either keep this near the start or end of the file so it is
 * at least reasonablye easy to find.
 * There are really 2 commands - those which are sent/received
 * before player joins, and those happen after the player has joined.
 * As such, we have function types that might be called, so
 * we end up having 2 tables.
 */

/** Prototype for functions the client sends without player interaction. */
typedef void (*func_uint8_int_ns)(char *, int, socket_struct *);

/** Definition of a function the client sends without player interaction. */
struct client_cmd_mapping {
    const char *cmdname;        /**< Command name. */
    const func_uint8_int_ns cmdproc;  /**< Function to call. */
};

/** Prototype for functions used to handle player actions. */
typedef void (*func_uint8_int_pl)(char *, int, player *);
/** Definition of a function called in reaction to player's action. */
struct player_cmd_mapping {
    const char *cmdname;             /**< Command name. */
    const func_uint8_int_pl cmdproc; /**< Function to call. */
    const uint8_t flag;                /**< If set, the player must be in the ST_PLAYING state for this command to be available. */
};

/**
 * Dispatch tables for the server.
 *
 * CmdMapping is the dispatch table for the server, used in handle_client,
 * which gets called when the client has input.  All commands called here
 * use the same parameter form (char *data, int len, int clientnum.
 * We do implicit casts, because the data that is being passed is
 * unsigned (pretty much needs to be for binary data), however, most
 * of these treat it only as strings, so it makes things easier
 * to cast it here instead of a bunch of times in the function itself.
 * flag is 1 if the player must be in the playing state to issue the
 * command, 0 if they can issue it at any time.
 */

/** Commands sent by the client reacting to player's actions. */
static const struct player_cmd_mapping player_commands[] = {
    { "examine",    examine_cmd,                       1 },
    { "apply",      apply_cmd,                         1 },
    { "move",       move_cmd,                          1 },
    { "reply",      reply_cmd,                         0 },
    { "ncom",       (func_uint8_int_pl)new_player_cmd, 1 },
    { "lookat",     look_at_cmd,                       1 },
    { "lock",       (func_uint8_int_pl)lock_item_cmd,  1 },
    { "mark",       (func_uint8_int_pl)mark_item_cmd,  1 },
    { "inscribe",   inscribe_scroll_cmd,               0 },
    { NULL,         NULL,                              0 }   /* terminator */
};

/** Commands sent directly by client, when connecting or when needed. */
static const struct client_cmd_mapping client_commands[] = {
    { "addme",               add_me_cmd },
    { "askface",             send_face_cmd },             /* Added: phil */
    { "beat",                NULL },
    { "requestinfo",         request_info_cmd },
    { "setup",               set_up_cmd },
    { "version",             version_cmd },
    { "asksmooth",           ask_smooth_cmd },            /*Added: tchize (smoothing technologies)*/
    { "accountlogin",       account_login_cmd },
    { "accountnew",         account_new_cmd },
    { "accountaddplayer",   account_add_player_cmd },
    { "accountplay",        account_play_cmd },
    { "accountpw",          account_password },
    { "createplayer",       create_player_cmd },
    { NULL,                  NULL }                       /* terminator (I, II & III)*/
};

/**
 * request_info_cmd is sort of a meta command. There is some specific
 * request of information, but we call other functions to provide
 * that information.
 * @param buf buffer containing the information requested.
 * @param len length of buf, ignored.
 * @param ns socket to write data to.
 */
void request_info_cmd(char *buf, int len, socket_struct *ns) {
    char *params = NULL, *cp;
    /* No match */
    SockList sl;

    if (len <= 0 || !buf) {
        LOG(llevDebug, "IP '%s' sent bogus request_info_cmd information\n", ns->host);
        return;
    }

    /* Set up replyinfo before we modify any of the buffers - this is used
     * if we don't find a match.
     */
    SockList_Init(&sl);
    SockList_AddString(&sl, "replyinfo ");
    SockList_AddString(&sl, buf);

    /* find the first space, make it null, and update the
     * params pointer.
     */
    for (cp = buf; *cp != '\0'; cp++)
        if (*cp == ' ') {
            *cp = '\0';
            params = cp+1;
            break;
        }
    if (!strcmp(buf, "image_info"))
        send_image_info(ns);
    else if (!strcmp(buf, "image_sums"))
        send_image_sums(ns, params);
    else if (!strcmp(buf, "skill_info"))
        send_skill_info(ns, params);
    else if (!strcmp(buf, "spell_paths"))
        send_spell_paths(ns);
    else if (!strcmp(buf, "exp_table"))
        send_exp_table(ns);
    else if (!strcmp(buf, "race_list"))
        send_race_list(ns);
    else if (!strcmp(buf, "race_info"))
        send_race_info(ns, params);
    else if (!strcmp(buf, "class_list"))
        send_class_list(ns);
    else if (!strcmp(buf, "class_info"))
        send_class_info(ns, params);
    else if (!strcmp(buf, "rules"))
        send_file(ns, "rules");
    else if (!strcmp(buf, "motd"))
        send_file(ns, "motd");
    else if (!strcmp(buf, "news"))
        send_file(ns, "news");
    else if (!strcmp(buf,"newcharinfo"))
        send_new_char_info(ns);
    else if (!strcmp(buf,"startingmap"))
        send_map_info(ns);
    else if (!strcmp(buf, "knowledge_info"))
        knowledge_send_info(ns);
    else
        Send_With_Handling(ns, &sl);
    SockList_Term(&sl);
}

/**
 * Handle a command, either directly sent by the client, or sent for a player's action.
 * @param ns socket sending the command, must not be NULL.
 * @param pl player associated to the socket, may be NULL.
 * @param cmd command name.
 * @param data additional data, can be NULL.
 * @param len length of data.
 * @return 1 if cmd is a command sent for a player's action, 0 else.
 */
static int
handle_cmd(socket_struct *ns, player *pl, char *cmd, char *data, int len) {
    /* Fuzz testing indicated a way to get a null command here
     * --> make an empty command, but have a length.
     * So, if we get here with a null command, log it and exit the function.
     * Daniel Hawkins 2020-01-16
     */
    if (cmd == NULL) {
        LOG(llevDebug, "%s: missing command. Sending garbage?\n", ns->host);
        return 0;
    }
    for (int i = 0; client_commands[i].cmdname != NULL; i++) {
        if (strcmp(cmd, client_commands[i].cmdname) == 0) {
            if (client_commands[i].cmdproc != NULL) {
                client_commands[i].cmdproc(data, len, ns);
            }
            return 0;
        }
    }
    /* Player must be in the playing state or the flag on the
     * the command must be zero for the user to use the command -
     * otherwise, a player cam save, be in the play_again state, and
     * the map they were on getsswapped out, yet things that try to look
     * at the map causes a crash.  If the command is valid, but
     * one they can't use, we still swallow it up.
     */
    if (pl) {
        for (int i = 0; player_commands[i].cmdname != NULL; i++) {
            if (strcmp(cmd, player_commands[i].cmdname) == 0) {
                if (pl->state == ST_PLAYING || player_commands[i].flag == 0) {
                    player_commands[i].cmdproc(data, len, pl);
                }
                return 1;
            }
        }
    }
    LOG(llevDebug, "%s: invalid command '%s'\n", ns->host, cmd);
    return 0;
}

/**
 * Handle commands from a client. This function should only be called when
 * the socket is readable. If an error occurs, the socket status will be set
 * to Ns_Dead; it is up to the caller to clean it up.
 *
 * @param ns Socket sending the command
 * @param pl Player associated with this socket, or NULL
 * @return If the main loop should continue processing this client.
 */
bool handle_client(socket_struct *ns) {
    /* Loop through this - maybe we have several complete packets here. */
    /* Command_count is used to limit the number of requests from
     * clients that have not logged in - we do not want an unauthenticated
     * connection to spew us with hundreds of requests.  As such,
     * this counter is only increased in the case of socket level commands.
     * Note that this also has the effect of throttling down face and other
     * socket commands from the client.  As such, if we have a player attached,
     * we will process more of these, as getting a fair number when entering
     * a map may not be uncommon.
     */
    int command_count = 0;
    while (command_count < 5 || (ns->pl && command_count < 25)) {
        pthread_mutex_lock(&global_lock);
        // Acquire global_lock to access pl, which is visible from other threads
        if (ns->pl && ns->pl->state == ST_PLAYING && ns->pl->ob != NULL && ns->pl->ob->speed_left < 0) {
            pthread_mutex_unlock(&global_lock);
            // Skip processing players with no turns left.
            usleep(tick_duration/2);
            return true;
        }
        pthread_mutex_unlock(&global_lock);

        int status = SockList_ReadPacket(ns->fd, &ns->inbuf, sizeof(ns->inbuf.buf)-1);
        if (status < 0) {
            // Disconnect
            pthread_mutex_lock(&global_lock);
            ns->status = Ns_Dead;
            LOG(llevInfo, "Disconnected from %s\n", ns->host);
            if (ns->pl) {
                save_player(ns->pl->ob, 0);
                leave(ns->pl, 1);
                final_free_player(ns->pl);
            } else {
                free_newsocket(ns);
            }
            pthread_mutex_unlock(&global_lock);
            return false;
        }

        if (status != 1) {
            continue;
        }

        /* Since we have a full packet, reset last tick time. */
        ns->last_tick = 0;

        SockList_NullTerminate(&ns->inbuf);
        assert(ns->inbuf.len >= 2);
        char *data;
        char *cmd = strtok_r((char *)ns->inbuf.buf + 2, " ", &data);

        int got_player_cmd;
        if (data != NULL) {
            int rem = ns->inbuf.len - ((unsigned char *)data - ns->inbuf.buf);
            pthread_mutex_lock(&global_lock);
            got_player_cmd = handle_cmd(ns, ns->pl, cmd, data, rem);
            pthread_mutex_unlock(&global_lock);
        } else {
            pthread_mutex_lock(&global_lock);
            got_player_cmd = handle_cmd(ns, ns->pl, cmd, NULL, 0);
            pthread_mutex_unlock(&global_lock);
        }

        SockList_ResetRead(&ns->inbuf);
        if (got_player_cmd) {
            return true;
        }

        command_count += 1;
    }

    if (ns->pl) {
        pthread_mutex_lock(&global_lock);
        send_delayed_buffers(ns->pl);
        send_updates(ns->pl);
        pthread_mutex_unlock(&global_lock);
    }
    return true;
}

/*****************************************************************************
 *
 * Low level socket looping - select calls and watchdog udp packet
 * sending.
 *
 ******************************************************************************/

#ifdef WATCHDOG
/**
 * Tell watchdog that we are still alive
 *
 * I put the function here since we should hopefully already be getting
 * all the needed include files for socket support
 */
void watchdog(void) {
    static int fd = -1;
    static struct sockaddr_in insock;

    if (fd == -1) {
        struct protoent *protoent;

        if ((protoent = getprotobyname("udp")) == NULL
        || (fd = socket(PF_INET, SOCK_DGRAM, protoent->p_proto)) == -1) {
            return;
        }
        insock.sin_family = AF_INET;
        insock.sin_port = htons((unsigned short)13325);
        insock.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    char buf[MAX_BUF];
    snprintf(buf, sizeof(buf), "%d\n", pticks);
    sendto(fd, buf, strlen(buf), 0, (struct sockaddr *)&insock, sizeof(insock));
}
#endif

/**
 * Waits for new connection when there is no one connected.
 */
#if 0
static void block_until_new_connection(void) {
    struct timeval Timeout;
    fd_set readfs;
    int cycles;
    int i;

    LOG(llevInfo, "Waiting for connections...\n");

    cycles = 1;
    do {
        /* Every minutes is a bit often for updates - especially if nothing is going
         * on.  This slows it down to every 6 minutes.
         */
        cycles++;
        if (cycles%2 == 0)
            tick_the_clock();

        FD_ZERO(&readfs);
        for (i = 0; i < socket_info.allocated_sockets && init_sockets[i].listen; i++)
            if (init_sockets[i].status == Ns_Add)
                FD_SET((uint32_t)init_sockets[i].fd, &readfs);

        /* If fastclock is set, we need to seriously slow down the updates
         * to the metaserver as well as watchdog.  Do same for flush_old_maps() -
         * that is time sensitive, so there is no good reason to call it 2000 times
         * a second.
         */
        if (settings.fastclock > 0) {
#ifdef WATCHDOG
            if (cycles%120000 == 0) {
                watchdog();
                flush_old_maps();
            }
#endif
            if (cycles == 720000) {
                metaserver_update();
                cycles = 1;
            }
            Timeout.tv_sec = 0;
            Timeout.tv_usec = 50;
        } else {
            Timeout.tv_sec = 60;
            Timeout.tv_usec = 0;
            if (cycles == 7) {
                metaserver_update();
                cycles = 1;
            }
            flush_old_maps();
        }
    } while (select(socket_info.max_filedescriptor, &readfs, NULL, NULL, &Timeout) == 0);

    reset_sleep(); /* Or the game would go too fast */
}
#endif

socket_struct *socket_struct_new(int fd) {
    socket_struct *ns = calloc(1, sizeof(socket_struct));
    if (ns == NULL) {
        fatal(OUT_OF_MEMORY);
    }
    ns->fd = fd;
    ns->status = Ns_Add;
    SockList_ResetRead(&ns->inbuf);
    return ns;
}

static void *client_thread(void *data) {
    socket_struct *ns = (socket_struct *)data;
    init_connection(ns, ns->host);
    while (handle_client(ns));
    return NULL;
}

/**
 * Try to accept a new client connection.
 * @param listen_fd File descriptor to to accept() on
 */
static void new_connection(int listen_fd) {
#ifdef HAVE_GETNAMEINFO
    struct sockaddr_storage addr;
#else
    struct sockaddr_in addr;
#endif
    socklen_t addrlen = sizeof(addr);
    int fd = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return;
        }
        return;
    }

    socket_struct *ns = socket_struct_new(fd);
    char buf[MAX_BUF];
#ifdef HAVE_GETNAMEINFO
    getnameinfo((struct sockaddr *) &addr, addrlen, buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
#else
    long ip;
    ip = ntohl(addr.sin_addr.s_addr);
    snprintf(buf, sizeof(buf), "%ld.%ld.%ld.%ld", (ip>>24)&255, (ip>>16)&255, (ip>>8)&255, ip&255);
#endif
    ns->host = strdup(buf);

    // Disconnect client if banned.
    if (checkbanned(NULL, ns->host)) {
        LOG(llevInfo, "Banned host tried to connect: [%s]\n", buf);
        close(fd);
        return;
    }

    pthread_t thread;
    pthread_create(&thread, NULL, client_thread, ns);
}

/**
 * Check whether the given socket's connection is alive or not.
 *
 * @return True if connection is active, false if not.
 */
bool connection_alive(socket_struct socket) {
    // If the client doesn't send heartbeats, assume it's connected.
    if (!socket.heartbeat) {
        return true;
    }

    // If a client message was received recently, it's connected.
    if (socket.last_tick < tick_length(BEAT_INTERVAL + 1)) {
        return true;
    }

    return false;
}

/**
 * Send all delayed buffers for a player.
 * @param pl player to send buffers for.
 */
static void send_delayed_buffers(player *pl) {
    for (uint8_t buf = 0; buf < pl->delayed_buffers_used; buf++) {
        Send_With_Handling(pl->socket, pl->delayed_buffers[buf]);
    }
    pl->delayed_buffers_used = 0;
}

/**
 * Send updated stats, map, look, and inventory to the player.
 */
static void send_updates(player *pl) {
    /* Update the players stats once per tick.  More efficient than
     * sending them whenever they change, and probably just as useful
     */
    esrv_update_stats(pl);
    if (pl->last_weight != -1 && pl->last_weight != WEIGHT(pl->ob)) {
        esrv_update_item(UPD_WEIGHT, pl->ob, pl->ob);
        if (pl->last_weight != WEIGHT(pl->ob))
            LOG(llevError, "esrv_update_item(UPD_WEIGHT) did not set player weight: is %lu, should be %lu\n", (unsigned long)pl->last_weight, (unsigned long)WEIGHT(pl->ob));
    }
    /* draw_client_map does sanity checking that map is
     * valid, so don't do it here.
     */
    draw_client_map(pl->ob);
    if (pl->socket->update_look)
        esrv_draw_look(pl->ob);
    if (pl->socket->update_inventory) {
        if (pl->ob->container != NULL)
            esrv_send_inventory(pl->ob, pl->ob->container);
        pl->socket->update_inventory = 0;
    }
}

/**
 * This checks the sockets for input and exceptions, does the right thing.
 *
 * A bit of this code is grabbed out of socket.c
 * There are 2 lists we need to look through - init_sockets is a list
 *
 */
void do_server(void) {
    for (int i = 0; i < listen_socket_count; i++) {
        new_connection(listeners[i].fd);
    }
    if (first_player == NULL) {
        //block_until_new_connection();
    }
    long sleep_time = get_sleep_remaining();
    if (sleep_time < 0) {
        LOG(llevInfo, "skipping time (over by %ld ms)\n", -sleep_time/1000);
        jump_time();
    } else {
        pthread_mutex_unlock(&global_lock);
        usleep(sleep_time);
        pthread_mutex_lock(&global_lock);
    }
}

/**
 * Send updates to players. Called once per tick.
 */
void update_players() {
    player *pl, *next;
    for (pl = first_player; pl != NULL; pl = next) {
        next = pl->next;
        send_updates(pl);
        /* Increment time since last contact only if logged in. */
        if (pl->state == ST_PLAYING) {
            pl->socket->last_tick++;

            if (!connection_alive(*pl->socket)) {
                // TODO: Handle a lost client connection.
                LOG(llevDebug, "Lost client connection!\n");
            }

            if (pl->socket->tick)
                send_tick(pl);
        }
    }
}
