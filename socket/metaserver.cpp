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
 * Meta-server related functions.
 * \date 2003-12-02
 */

#include "global.h"

#include <ctype.h>
#include <string>
#include <mutex>
#include <thread>
#include <system_error>

#ifndef WIN32 /* ---win32 exclude unix header files */
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#endif /* end win32 */

#include "metaserver2.h"
#include "version.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#include <curl/easy.h>
#endif

/** Mutex to protect access to ::metaserver2_updateinfo. */
static std::mutex ms2_info_mutex;
/** Mutex to signal the thread should stop. */
static std::timed_mutex ms2_signal;

int count_players() {
    /* We could use socket_info.nconns, but that is not quite as accurate,
     * as connections in the progress of being established, are listening
     * but don't have a player, etc.  The checks below are basically the
     * same as for the who commands with the addition that WIZ, AFK, and BOT
     * players are not counted.
     */
    char num_players = 0;
    for (player *pl = first_player; pl != NULL; pl = pl->next) {
        if (pl->ob->map == NULL)
            continue;
        if (pl->hidden)
            continue;
        if (QUERY_FLAG(pl->ob, FLAG_WIZ))
            continue;
        if (QUERY_FLAG(pl->ob, FLAG_AFK))
            continue;
        if (pl->state != ST_PLAYING && pl->state != ST_GET_PARTY_PASSWORD)
            continue;
        if (pl->socket->is_bot)
            continue;
        num_players++;
    }
    return num_players;
}

/**
 * Updates our info in the metaserver
 * Note that this is used for both metaserver1 and metaserver2 -
 * for metaserver2, it just copies dynamic data into private
 * data structure, doing locking in the process.
 */
void metaserver_update(void) {
#ifdef HAVE_LIBCURL
    /* Everything inside the lock/unlock is related
     * to metaserver2 synchronization.
     */
    ms2_info_mutex.lock();
    metaserver2_updateinfo.num_players = count_players();
#ifdef CS_LOGSTATS
    metaserver2_updateinfo.in_bytes = cst_tot.ibytes;
    metaserver2_updateinfo.out_bytes = cst_tot.obytes;
    metaserver2_updateinfo.uptime  = (long)time(NULL)-cst_tot.time_start;
#else
    metaserver2_updateinfo.in_bytes = 0;
    metaserver2_updateinfo.out_bytes = 0;
    metaserver2_updateinfo.uptime  = 0;
#endif
    ms2_info_mutex.unlock();
#endif
}

/*
 * Start of metaserver2 logic
 * Note: All static structures in this file should be treated as strictly
 * private.  The metaserver2 update logic runs in its own thread,
 * so if something needs to modify these structures, proper locking
 * is needed.
 */

/** Metaservers to send information to, list of hostnames. */
static std::vector<std::string> metaservers;

/**
 * LocalMeta2Info basically holds all the non server metaserver2 information
 * that we read from the metaserver2 file.  Could just have
 * individual variables, but doing it as a structure is cleaner
 * I think, and might be more flexible if in the future, we
 * want to pass this information to other functions (like plugin
 * or something)
 */
struct LocalMeta2Info {
    int     notification;  /**< If true, do updates to metaservers. */
    char    *hostname;     /**< Hostname of this server. */
    int     portnumber;    /**< Portnumber of this server. */
    char    *html_comment; /**< html comment to send to metaservers. */
    char    *text_comment; /**< text comment to send to metaservers. */
    char    *archbase;     /**< Different sources for arches, maps */
    char    *mapbase;      /**< and server. */
    char    *codebase;
    char    *flags;        /**< Short flags to send to metaserver. */
};

/** Non volatile information on the server. */
static LocalMeta2Info local_info;

/** Metaserver thread, if notifications are enabled. */
static std::thread metaserver_thread;

/** Statistics on players and such sent to the metaserver2. */
MetaServer2_UpdateInfo metaserver2_updateinfo;

#ifdef HAVE_LIBCURL

/**
 * Handles writing of HTTP request data from the metaserver2.
 * We treat the data as a string.  We should really pay attention to the
 * header data, and do something clever if we get 404 codes
 * or the like.
 * @param ptr actual data.
 * @param size size of the data.
 * @param nmemb number of elements.
 * @param data user-provided data, unused.
 * @return number of bytes processed, always the full size.
 */
static size_t metaserver2_writer(void *ptr, size_t size, size_t nmemb, void *data) {
    (void)data;
    size_t realsize = size*nmemb;
    LOG(llevError, "Message from metaserver:\n%s\n", (const char*)ptr);
    return realsize;
}

static void metaserver2_build_form(struct curl_httppost **formpost) {
    struct curl_httppost *lastptr = NULL;
    char buf[MAX_BUF];

    /* First, fill in the form - note that everything has to be a string,
     * so we convert as needed with snprintf.
     * The order of fields here really isn't important.
     * The string after CURLFORM_COPYNAME is the name of the POST variable
     * as the
     */
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "hostname",
                 CURLFORM_COPYCONTENTS, local_info.hostname,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%d", local_info.portnumber);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "port",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "html_comment",
                 CURLFORM_COPYCONTENTS, local_info.html_comment,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "text_comment",
                 CURLFORM_COPYCONTENTS, local_info.text_comment,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "archbase",
                 CURLFORM_COPYCONTENTS, local_info.archbase,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "mapbase",
                 CURLFORM_COPYCONTENTS, local_info.mapbase,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "codebase",
                 CURLFORM_COPYCONTENTS, local_info.codebase,
                 CURLFORM_END);

    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "flags",
                 CURLFORM_COPYCONTENTS, local_info.flags,
                 CURLFORM_END);

    ms2_info_mutex.lock();

    snprintf(buf, sizeof(buf), "%d", metaserver2_updateinfo.num_players);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "num_players",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%d", metaserver2_updateinfo.in_bytes);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "in_bytes",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%d", metaserver2_updateinfo.out_bytes);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "out_bytes",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%ld", (long)metaserver2_updateinfo.uptime);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "uptime",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    ms2_info_mutex.unlock();

    /* Following few fields are global variables,
     * but are really defines, so won't ever change.
     */
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "version",
                 CURLFORM_COPYCONTENTS, FULL_VERSION,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%d", VERSION_SC);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "sc_version",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);

    snprintf(buf, sizeof(buf), "%d", VERSION_CS);
    curl_formadd(formpost, &lastptr,
                 CURLFORM_COPYNAME, "cs_version",
                 CURLFORM_COPYCONTENTS, buf,
                 CURLFORM_END);
}
#endif

/**
 * This sends an update to the various metaservers.
 * It generates the form, and then sends it to the
 * server
 */
static void metaserver2_updates(void) {
#ifdef HAVE_LIBCURL
    struct curl_httppost *formpost = NULL;
    metaserver2_build_form(&formpost);

    for (auto hostname : metaservers) {
        CURL *curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30000);

            /* what URL that receives this POST */
            curl_easy_setopt(curl, CURLOPT_URL, hostname.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

            /* Almost always, we will get HTTP data returned
             * to us - instead of it going to stderr,
             * we want to take care of it ourselves.
             */
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, metaserver2_writer);

            char errbuf[CURL_ERROR_SIZE];
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

            CURLcode res = curl_easy_perform(curl);
            if (res) {
                LOG(llevError, "metaserver update failed: %s\n", errbuf);
            }

            /* always cleanup */
            curl_easy_cleanup(curl);
        } else {
            LOG(llevError, "metaserver: could not initialize curl\n");
        }
    }
    /* then cleanup the formpost chain */
    curl_formfree(formpost);
#endif
}

/**
 * Repeatedly send updates to the metaserver, stopping when ms2_signal is acquired.
 * Works in the background.
 */

void metaserver2_thread() {
    do {
        metaserver2_updates();
    } while (!ms2_signal.try_lock_for(std::chrono::seconds(60)));
    ms2_signal.unlock();
}

/**
 * This initializes the metaserver2 logic - it reads
 * the metaserver2 file, storing the values
 * away.  Note that it may be possible/desirable for the
 * server to re-read the values and restart connections (for example,
 * a new metaserver has been added and you want to start updates to
 * it immediately and not restart the server).  Because of that,
 * there is some extra logic (has_init) to try to take that
 * into account.
 *
 * @return
 * 1 if we will be updating the metaserver, 0 if no
 * metaserver updates
 */
int metaserver2_init(void) {
    FILE *fp;
    char buf[MAX_BUF], *cp, dummy[1];

    dummy[0] = '\0';

#ifdef HAVE_LIBCURL
    static int has_init = 0;
    if (!has_init) {
        memset(&local_info, 0, sizeof(LocalMeta2Info));
        memset(&metaserver2_updateinfo, 0, sizeof(MetaServer2_UpdateInfo));

        local_info.portnumber = settings.csport;
        curl_global_init(CURL_GLOBAL_ALL);
    } else {
        local_info.notification = 0;
        if (local_info.hostname)
            FREE_AND_CLEAR(local_info.hostname);
        if (local_info.html_comment)
            FREE_AND_CLEAR(local_info.html_comment);
        if (local_info.text_comment)
            FREE_AND_CLEAR(local_info.text_comment);
        if (local_info.archbase)
            FREE_AND_CLEAR(local_info.archbase);
        if (local_info.mapbase)
            FREE_AND_CLEAR(local_info.mapbase);
        if (local_info.codebase)
            FREE_AND_CLEAR(local_info.codebase);
        if (local_info.flags)
            FREE_AND_CLEAR(local_info.flags);
        metaservers.clear();
    }
#endif

    /* Now load up the values from the file */
    snprintf(buf, sizeof(buf), "%s/metaserver2", settings.confdir);

    if ((fp = fopen(buf, "rt")) == NULL) {
        LOG(llevError, "Warning: No metaserver2 file found\n");
        return 0;
    }
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (buf[0] == '#')
            continue;
        /* eliminate newline */
        if ((cp = strrchr(buf, '\n')) != NULL)
            *cp = '\0';

        /* Skip over empty lines */
        if (buf[0] == 0)
            continue;

        /* Find variable pairs */

        if ((cp = strpbrk(buf, " \t")) != NULL) {
            while (isspace(*cp))
                *cp++ = 0;
        } else {
            /* This makes it so we don't have to do NULL checks against
             * cp everyplace
             */
            cp = dummy;
        }

        if (!strcasecmp(buf, "metaserver2_notification")) {
            if (!strcasecmp(cp, "on") || !strcasecmp(cp, "true")) {
                local_info.notification = TRUE;
            } else if (!strcasecmp(cp, "off") || !strcasecmp(cp, "false")) {
                local_info.notification = FALSE;
            } else {
                LOG(llevError, "metaserver2: Unknown value for metaserver2_notification: %s\n", cp);
            }
        } else if (!strcasecmp(buf, "metaserver2_server")) {
            if (*cp != 0) {
                metaservers.push_back(cp);
            } else {
                LOG(llevError, "metaserver2: metaserver2_server must have a value.\n");
            }
        } else if (!strcasecmp(buf, "localhostname")) {
            if (*cp != 0) {
                local_info.hostname = strdup_local(cp);
            } else {
                LOG(llevError, "metaserver2: localhostname must have a value.\n");
            }
        } else if (!strcasecmp(buf, "portnumber")) {
            if (*cp != 0) {
                local_info.portnumber = atoi(cp);
            } else {
                LOG(llevError, "metaserver2: portnumber must have a value.\n");
            }
        /* For the following values, it is easier to make sure
         * the pointers are set to something, even if it is a blank
         * string, so don't care if there is data in the string or not.
         */
        } else if (!strcasecmp(buf, "html_comment")) {
            local_info.html_comment = strdup(cp);
        } else if (!strcasecmp(buf, "text_comment")) {
            local_info.text_comment = strdup(cp);
        } else if (!strcasecmp(buf, "archbase")) {
            local_info.archbase = strdup(cp);
        } else if (!strcasecmp(buf, "mapbase")) {
            local_info.mapbase = strdup(cp);
        } else if (!strcasecmp(buf, "codebase")) {
            local_info.codebase = strdup(cp);
        } else if (!strcasecmp(buf, "flags")) {
            local_info.flags = strdup(cp);
        } else {
            LOG(llevError, "Unknown value in metaserver2 file: %s\n", buf);
        }
    }
    fclose(fp);

    /* If no hostname is set, can't do updates */
    if (!local_info.hostname)
        local_info.notification = 0;

#ifndef HAVE_LIBCURL
    if (local_info.notification) {
        LOG(llevError, "metaserver2 file is set to do notification, but libcurl is not found.\n");
        LOG(llevError, "Either fix your compilation, or turn off metaserver2 notification in \n");
        LOG(llevError, "the %s/metaserver2 file.\n", settings.confdir);
        LOG(llevError, "Exiting program.\n");
        exit(1);
    }
#endif

    if (local_info.notification) {
        /* As noted above, it is much easier for the rest of the code
         * to not have to check for null pointers.  So we do that
         * here, and anything that is null, we just allocate
         * an empty string.
        */
        if (!local_info.html_comment)
            local_info.html_comment = strdup("");
        if (!local_info.text_comment)
            local_info.text_comment = strdup("");
        if (!local_info.archbase)
            local_info.archbase = strdup("");
        if (!local_info.mapbase)
            local_info.mapbase = strdup("");
        if (!local_info.codebase)
            local_info.codebase = strdup("");
        if (!local_info.flags)
            local_info.flags = strdup("");

        ms2_signal.lock();
        try {
          metaserver_thread = std::thread(metaserver2_thread);
        }
        catch (const std::system_error &err) {
          LOG(llevError, "metaserver2_init: failed to create thread, code %d, what %s\n", err.code().value(), err.what());
          /* Effectively true - we're not going to update the metaserver */
          local_info.notification = 0;
        }
    }
    return local_info.notification;
}

/**
 * Stop metaserver updates.
 */
void metaserver2_exit() {
    ms2_signal.unlock();
    if (metaserver_thread.joinable()) {
        metaserver_thread.join();
    }
}
