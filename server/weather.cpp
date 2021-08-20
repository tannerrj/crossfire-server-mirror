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
 * @file
 * Time-related functions.
 */

#include <global.h>
#include <tod.h>
#include <map.h>
#ifndef __CEXTRACT__
#include <sproto.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern unsigned long todtick;
weathermap_t **weathermap;

static void dawn_to_dusk(const timeofday_t *tod);
static void read_pressuremap(void);
static void init_pressure(void);
static void init_weatheravoid (weather_avoids_t wa[]);
static object *avoid_weather(int *av, mapstruct *m, int x, int y, int *gs, int grow);
static void calculate_temperature(mapstruct *m);
static void let_it_snow(mapstruct *m);
static void singing_in_the_rain(mapstruct *m);
static void plant_a_garden(mapstruct *m);
static void change_the_world(mapstruct *m);
void process_rain(void);
static void weather_effect(mapstruct *m);

/** How to alter darkness, based on time of day and season. */
static const int season_timechange[5][HOURS_PER_DAY] = {
/*    0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  1  2  3  4  5  6  7  8  9 10 11 12 13 */
    { 0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 }
};

/**
 * The table below is used to set which tiles the weather will avoid
 * processing.  This keeps it from putting snow on snow, and putting snow
 * on the ocean, and other things like that.
 */
static weather_avoids_t weather_avoids[] = {
    {"snow", 1, NULL},
    {"snow2", 1, NULL},
    {"snow3", 1, NULL},
    {"snow4", 1, NULL},
    {"snow5", 1, NULL},
    {"mountain1_snow", 1, NULL},
    {"mountain2_snow", 1, NULL},
    {"rain1", 1, NULL},
    {"rain1_weather", 1, NULL},
    {"rain2", 1, NULL},
    {"rain2_weather", 1, NULL},
    {"rain3", 1, NULL},
    {"rain3_weather", 1, NULL},
    {"rain4", 1, NULL},
    {"rain4_weather", 1, NULL},
    {"rain5", 1, NULL},
    {"rain5_weather", 1, NULL},
    {"mountain1_rivlets", 1, NULL},
    {"mountain2_rivlets", 1, NULL},
    {"mountain1_rivlets_weather", 1, NULL},
    {"mountain2_rivlets_weather", 1, NULL},
    {"ipond", 1, NULL},
    {"biglake_4", 0, NULL},
    {"biglake_center", 0 , NULL},
    {"drifts", 0, NULL},
    {"glacier", 0, NULL},
    {"cforest1", 0, NULL},
    {"sea", 0, NULL},
    {"sea1", 0, NULL},
    {"deep_sea", 0, NULL},
    {"shallow_sea", 0, NULL},
    {"lava", 0, NULL},
    {"permanent_lava", 0, NULL},
    /* Mountain cave are weird archetypes: floor, but exit. So we shouldn't cover them. */
    {"mountain_cave", 0, NULL},
    {"mountain_cave2", 0, NULL},
    {NULL, 0, NULL}
};

/**
 * This table is identical to ::weather_avoids, except these are tiles to avoid
 * when processing growth. IE, don't grow herbs in the ocean.  The second
 * field is unused.
 */
static weather_avoids_t growth_avoids[] = {
    {"cobblestones", 0, NULL},
    {"cobblestones2", 0, NULL},
    {"flagstone", 0, NULL},
    {"stonefloor2", 0, NULL},
    {"lava", 0, NULL},
    {"permanent_lava", 0, NULL},
    {"sea", 0, NULL},
    {"sea1", 0, NULL},
    {"deep_sea", 0, NULL},
    {"shallow_sea", 0, NULL},
    {"farmland", 0, NULL},
    {"dungeon_magic", 0, NULL},
    {"dungeon_floor", 0, NULL},
    {"lake", 0, NULL},
    {"grasspond", 0, NULL},
    /* Mountain cave are weird archetypes: floor, but exit. So we shouldn't cover them. */
    {"mountain_cave", 0, NULL},
    {"mountain_cave2", 0, NULL},
    /* Avoid growing things on the rivers. Its janky-looking */
    //{"river", 1, NULL},
    //{"river junction", 1, NULL},
    {NULL, 0, NULL}
};

/**
 * The table below is used in let_it_snow() and singing_in_the_rain() to
 * decide what type of snow/rain/etc arch to put down.  The first field is the
 * name of the arch we want to match.  The second field is the special snow
 * type we use to cover that arch.  The third field is the doublestack arch,
 * NULL if none, used to stack over the snow after covering the tile.
 * The fourth field is 1 if you want to match arch->name, 0 to match ob->name.
 */
static weather_replace_t weather_replace[] = {
    {"impossible_match", "snow5", NULL, 0},
    {"impossible_match2", "snow4", NULL, 0}, /* placeholders */
    {"impossible_match3", "snow3", NULL, 0},
    {"hills", "drifts", NULL, 0},
    {"treed_hills", "drifts", "woods5", 1},
    {"grass", "snow", NULL, 0},
    {"sand", "snow", NULL, 0},
    {"stones", "snow2", NULL, 0},
    {"steppe", "snow2", NULL, 0},
    {"blackrock", "snow2", NULL, 1},
    {"brush", "snow2", NULL, 0},
    {"cyanbrush", "snow2", NULL, 1},
    {"farmland", "snow3", NULL, 0},
    {"wasteland", "glacier", NULL, 0},
    {"mountain5", "glacier", NULL, 1},
    {"mountain", "mountain1_snow", NULL, 1},
    {"mountain2", "mountain2_snow", NULL, 1},
    {"mountain4", "mountain2_snow", NULL, 1},
    {"s_mountain", "mountain1_snow", NULL, 1},
    {"grasspond", "ipond", NULL, 1},
    {"cyangrasspond", "ipond", NULL, 1},
    {"evergreens", "snow", "evergreens2", 1},
    {"evergreen","snow", "tree5", 1},
    {"tree", "snow", "tree3", 0},
    {"woods", "snow3", "woods4", 1},
    {"woods_3", "snow", "woods5", 1},
    {"darkforest", "snow3", "woods4", 1},
    {NULL, NULL, NULL, 0},
};

// Table to do snow melt when temp is warm enough or it is getting rained on.
static weather_replace_t weather_snowmelt[] = {
    {"mountain", "mountain1_rivlets_weather", NULL, 0},
    {"mountain2", "mountain2_rivlets_weather", NULL, 0},
    {"mountain4", "mountain2_rivlets_weather", NULL, 0},
    {NULL, NULL, NULL, 0},
};

/**
 * The table below is used to grow things on the map. See include/tod.h for
 * the meanings of all of the fields.
 */
static const weather_grow_t weather_grow[] = {
    /* herb, tile, random, rfmin, rfmax, humin, humax, tempmin, tempmax, elevmin, elevmax, season */
    {"mint", "grass", 10, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    {"rose_red", "grass", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    {"rose_red", "hills", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    //{"rose_yellow", "grass", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    //{"rose_yellow", "hills", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    //{"rose_pink", "grass", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    //{"rose_pink", "hills", 15, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    {"mint", "brush", 8, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 2},
    {"blackroot", "swamp", 15, 1.6, 2.0, 60, 100, 20, 30, -100, 1500, 0},
    {"mushroom_1", "grass", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"mushroom_2", "grass", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"mushroom_1", "swamp", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"mushroom_2", "swamp", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"mushroom_1", "hills", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"mushroom_2", "hills", 15, 1.6, 2.0, 60, 100, 3, 30, -100, 1500, 0},
    {"pipeweed", "farmland", 20, 1.0, 2.0, 30, 100, 10, 25, 100, 5000, 0},
    {"cabbage", "farmland", 10, 1.0, 2.0, 30, 100, 10, 25, -100, 9999, 0},
    {"onion", "farmland", 10, 1.0, 2.0, 30, 100, 10, 25, 100, 9999, 0},
    {"carrot", "farmland", 10, 1.0, 2.0, 30, 100, 10, 25, 100, 9999, 0},
    {"thorns", "brush", 15, 0.5, 1.3, 30, 100, 10, 25, -100, 9999, 0},
    {"mountain_foilage", "mountain", 6, 1.0, 2.0, 25, 100, 5, 30, 0, 15999, 2},
    {NULL, NULL, 1, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0}
};

/**
 * The table below uses the same format as the one above.  However this
 * table is used to change the layout of the worldmap itself.  The tile
 * parameter is a base tile to lay down underneath the herb tile.
 */
static const weather_grow_t weather_tile[] = {
    /* herb, tile, random, rfmin, rfmax, humin, humax, tempmin, tempmax, elevmin, elevmax */
    {"dunes", NULL, 2, 0.0, 0.03, 0, 20, 10, 99, 0, 4000, 0},
    {"desert", NULL, 1, 0.0, 0.05, 0, 20, 10, 99, 0, 4000, 0},
    {"pstone_2", NULL, 1, 0.0, 0.05, 0, 20, -30, 10, 0, 4000, 0},
    {"pstone_3", NULL, 1, 0.0, 0.05, 0, 20, -30, 10, 0, 4000, 0},
    {"grassbrown", NULL, 1, 0.05, 1.0, 20, 80, -20, -3, 0, 5000, 0},
    {"grass_br_gr", NULL, 1, 0.05, 1.0, 20, 80, -3, 5, 0, 5000, 0},
    {"grass", NULL, 1, 0.05, 1.0, 20, 80, 5, 15, 0, 5000, 0},
    {"grassmedium", NULL, 1, 0.05, 1.0, 20, 80, 15, 25, 0, 5000, 0},
    {"grassdark", NULL, 1, 0.05, 1.0, 20, 80, 25, 35, 0, 5000, 0},
    {"brush", NULL, 1, 0.2, 1.0, 25, 70, 0, 30, 500, 6000, 0},
    /* small */
    {"evergreens2", "brush", 1, 0.5, 1.8, 30, 90, -30, 24, 3000, 8000, 0},
    {"fernsdense", "brush", 1, 0.9, 2.5, 50, 100, 10, 35, 1000, 6000, 0},
    {"fernssparse", "brush", 1, 0.7, 2.0, 30, 90, -15, 35, 0, 4000, 0},
    {"woods4", "brush", 1, 0.1, 0.8, 30, 60, -5, 25, 1000, 4500, 0},
    {"woods5", "brush", 1, 0.6, 1.5, 20, 70, -15, 20, 2000, 5500, 0},
    {"forestsparse", "brush", 1, 0.3, 1.5, 15, 60, -20, 25, 0, 4500, 0},
    /* big */
    /*
    {"ytree_2", "brush", 2, 0.1, 0.6, 30, 60, 10, 25, 1000, 3500, 0},
    {"tree3", "grass", 2, 0.9, 2.5, 50, 100, 10, 35, 1000, 4000, 0},
    {"tree5", "grass", 2, 0.5, 1.5, 40, 90, -10, 24, 3000, 8000, 0},
    {"tree3", "grassmeduim", 2, 0.9, 2.5, 50, 100, 10, 35, 1000, 4000, 0},
    {"tree5", "grassmedium", 2, 0.5, 1.5, 40, 90, -10, 24, 3000, 8000, 0},
    {"tree3", "grassdark", 2, 0.9, 2.5, 50, 100, 10, 35, 1000, 4000, 0},
    {"tree5", "grassdark", 2, 0.5, 1.5, 40, 90, -10, 24, 3000, 8000, 0},*/
    /* mountians */
    {"steppe", NULL, 1, 0.5, 1.3, 0, 30, -20, 35, 1000, 6000, 0},
    {"steppelight", NULL, 1, 0.0, 0.6, 0, 20, -50, 35, 0, 5000, 0},
    {"hills", NULL, 1, 0.1, 0.9, 20, 80, -10, 30, 5000, 8500, 0},
    {"hills_rocky", NULL, 1, 0.0, 0.9, 0, 100, -50, 50, 5000, 8500, 0},
    {"swamp", NULL, 1, 1.0, 9.9, 55, 80, 10, 50, 0, 1000, 0},
    {"deep_swamp", NULL, 1, 1.0, 9.9, 80, 100, 10, 50, 0, 1000, 0},
    {"mountain", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 8000, 10000, 0},
    {"mountain2", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 9500, 11000, 0},
    {"mountain4", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 10500, 12000, 0},
    {"mountain5", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 11500, 13500, 0},
    {"wasteland", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 13000, 99999, 0},
    /* catchalls */
    {"palms", "pstone_1", 1, 0.01, 0.1, 0, 30, 5, 99, 0, 4000, 0},
    {"large_stones", NULL, 1, 0.0, 9.9, 0, 100, -50, 50, 6000, 8000, 0},
    {"earth", NULL, 1, 0.0, 1.0, 0, 70, -30, 15, 0, 6000, 0},
    {"medium_stones", NULL, 1, 1.0, 3.0, 70, 100, -30, 10,  0, 4000, 0}, /*unsure*/
    {"earth", NULL, 1, 0.1, 0.9, 20, 80, -30, 30, 0, 4999, 0}, /* tundra */
    {"swamp", NULL, 1, 1.0, 9.9, 50, 100, -30, 10, 0, 4000, 0},/* cold marsh */
    {"earth", NULL, 1, 0.0, 99.9, 0, 100, -99, 99, 0, 99999, 0}, /* debug */
    {NULL, NULL, 1, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0}
};

/**
 * Set the darkness level for a map, based on the time of the day.
 *
 * @param m
 * map to alter.
 */
void set_darkness_map(mapstruct *m) {
    int i;
    timeofday_t tod;

    if (!m->outdoor) {
        return;
    }

    get_tod(&tod);
    m->darkness = 0;
    for (i = HOURS_PER_DAY/2; i < HOURS_PER_DAY; i++) {
        change_map_light(m, season_timechange[tod.season][i]);
    }
    for (i = 0; i <= tod.hour; i++) {
        change_map_light(m, season_timechange[tod.season][i]);
    }
}

/**
 * Get the darkness of the world map at this point.
 * Since the darkness of the world map is uniform, we can get away with
 * this calculation.
 *
 * @return
 * The darkenss value of the world at the current time.
 */
int get_world_darkness() {
    timeofday_t tod;
    get_tod(&tod); // Get time of day.
    // Determine the darkness on the world
    // Since the world map has uniform darkness, just calculate it outright.
    int darkness = 0;
    for (int i = 0; i < tod.hour; ++i) {
        darkness += season_timechange[tod.season][i];
    }
    return darkness;
}

/**
 * Compute the darkness level for all loaded maps in the game.
 *
 * @param tod
 * time of day to compute darkness for.
 */
static void dawn_to_dusk(const timeofday_t *tod) {
    mapstruct *m;

    /* If the light level isn't changing, no reason to do all
     * the work below.
     */
    if (season_timechange[tod->season][tod->hour] == 0) {
        return;
    }

    for (m = first_map; m != NULL; m = m->next) {
        if (!m->outdoor) {
            continue;
        }

        change_map_light(m, season_timechange[tod->season][tod->hour]);
    }
}

/**
 * This performs the basic function of advancing the clock one tick
 * forward.  Every 20 ticks, the clock is saved to disk.  It is also
 * saved on shutdown.  Any time dependant functions should be called
 * from this function, and probably be passed tod as an argument.
 * Please don't modify tod in the dependant function.
 */
void tick_the_clock(void) {
    timeofday_t tod;

    todtick++;
    if (todtick%20 == 0) {
        write_todclock();
    }
    get_tod(&tod);
    dawn_to_dusk(&tod);
}

/*
 * This batch of routines reads and writes the various
 * weathermap structures.  Each type of data is stored
 * in a separate file to allow the size of these structures to be
 * changed more or less on the fly.  If weather goes haywire, the admin
 * can simply delete and boot the server, and it will regen.
 *
 * The write functions should be called occasionally to keep the data
 * in the maps current.  Whereas the read functions should only be called
 * at boot.  If the read function cannot find the appropriate map, it
 * calls the init function, to initialize that map.
 */

/**
 * Write the sky map. We never read this map, only write it for debugging purposes
 */
void write_skymap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/skymap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing sky conditions map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].sky);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Save pressure information.
 */
void write_pressuremap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/pressuremap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing pressure map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].pressure);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read the pressure information from disk. If it doesn't exist, initialize pressure.
 */
static void read_pressuremap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/pressuremap", settings.localdir);
    LOG(llevDebug, "Reading pressure data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing pressure maps...\n");
        init_pressure();
        write_pressuremap();
        LOG(llevDebug, "Done\n");
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%hd ", &weathermap[x][y].pressure);
            if (weathermap[x][y].pressure < PRESSURE_MIN ||
                weathermap[x][y].pressure > PRESSURE_MAX) {
                weathermap[x][y].pressure = rndm(PRESSURE_MIN, PRESSURE_MAX);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Reset pressure map.
 */
static void init_pressure(void) {
    int x, y;
    int l, n, k, r;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].pressure = 1000;
        }
    }

    for (l = 0; l < PRESSURE_ITERATIONS; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        n = rndm(PRESSURE_MIN, PRESSURE_MAX);
        for (k = 1; k < PRESSURE_AREA; k++) {
            r = rndm(0, 3);
            switch (r) {
            case 0: if (x < WEATHERMAPTILESX-1) x++; break;
            case 1: if (y < WEATHERMAPTILESY-1) y++; break;
            case 2: if (x) x--; break;
            case 3: if (y) y--; break;
            }
            weathermap[x][y].pressure = (weathermap[x][y].pressure+n)/2;
        }
    }
    /* create random spikes in the pressure */
    for (l = 0; l < PRESSURE_SPIKES; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        n = rndm(500, 2000);
        weathermap[x][y].pressure = n;
    }
    smooth_pressure();
}

/* END of read/write/init */

/**
 * Link fields to their archetypes.
 *
 * @param wa
 * structure to link archetypes of.
 */
static void init_weatheravoid(weather_avoids_t wa[]) {
    for (int i = 0; wa[i].name != NULL; i++) {
        wa[i].what = find_archetype(wa[i].name);
    }
}

/** Current weather tile position. */
static int wmperformstartx;
/** Current weather tile position. */
static int wmperformstarty;

/**
 * This function initializes the weather system.  It should be called once,
 * at game startup only.
 */
void init_weather(void) {
    int x;
    char filename[MAX_BUF];
    FILE *fp;

    /* all this stuff needs to be set, otherwise this function will cause
     * chaos and destruction.
     */
    if (settings.dynamiclevel < 1) {
        return;
    }

    if (settings.worldmapstartx < 1 || settings.worldmapstarty < 1 ||
        settings.worldmaptilesx < 1 || settings.worldmaptilesy < 1 ||
        settings.worldmaptilesizex < 1 || settings.worldmaptilesizex < 1) {
        return;
    }

    /*prepare structures used for avoidance*/
    init_weatheravoid (weather_avoids);
    init_weatheravoid (growth_avoids);

    LOG(llevDebug, "Initializing the weathermap...\n");

    weathermap = (weathermap_t **)malloc(sizeof(weathermap_t *)*WEATHERMAPTILESX);
    if (weathermap == NULL) {
        fatal(OUT_OF_MEMORY);
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        weathermap[x] = (weathermap_t *)calloc(WEATHERMAPTILESY, sizeof(weathermap_t));
        if (weathermap[x] == NULL) {
            fatal(OUT_OF_MEMORY);
        }
    }

    /* now we load the values in the big worldmap weather array */
    /* do not re-order these */
    read_pressuremap();
    /* The rest have been migrated over to the weather module. */
    // Get current map position
    snprintf(filename, sizeof(filename), "%s/wmapcurpos", settings.localdir);
    LOG(llevDebug, "Reading current weather position from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Can't open %s.\n", filename);
        wmperformstartx = -1;
        return;
    }

    (void)fscanf(fp, "%d %d", &wmperformstartx, &wmperformstarty);
    LOG(llevDebug, "curposx=%d curposy=%d\n", wmperformstartx, wmperformstarty);
    fclose(fp);
    if (wmperformstartx > settings.worldmaptilesx) {
        wmperformstartx = -1;
    }
    if (wmperformstarty > settings.worldmaptilesy) {
        wmperformstarty = 0;
    }
}

/**
 * Frees all memory allocated by the weather system.
 */
void free_weather(void) {
    for (int x = 0; x < WEATHERMAPTILESX; x++) {
        FREE_AND_CLEAR(weathermap[x]);
    }
    FREE_AND_CLEAR(weathermap);
}

/**
 * This routine slowly loads the world, patches it up due to the weather,
 * and saves it back to disk.  In this way, the world constantly feels the
 * effects of weather uniformly, without relying on players wandering.
 *
 * The main point of this is stuff like growing herbs, soil, decaying crap,
 * etc etc etc.  Not actual *weather*, but weather *effects*.
 */
void perform_weather(void) {
    mapstruct *m;
    char filename[MAX_BUF];
    FILE *fp;

    if (!settings.dynamiclevel) {
        return;
    }

    /* move right to left, top to bottom */
    if (++wmperformstartx == settings.worldmaptilesx) {
        wmperformstartx = 0;
        if (++wmperformstarty == settings.worldmaptilesy) {
            wmperformstarty = 0;
        }
    }

    // Whenever we load a map for effects, recalculate the weather.
    // Do this before the actual map load so that precipitation is done with the new sky computation rather than the old
    compute_sky();

    snprintf(filename, sizeof(filename), "world/world_%d_%d", wmperformstartx+settings.worldmapstartx, wmperformstarty+settings.worldmapstarty);

    m = ready_map_name(filename, 0);
    if (m == NULL) {
        return; /* hrmm */
    }

    // Run weather effects here.
    // We do this here rather than on any map load so that the weather effects remain consistent.
    weather_effect(m);

    /* done */
    save_map(m, SAVE_MODE_OVERLAY); /* write the overlay */
    m->in_memory = MAP_IN_MEMORY; /*reset this*/
    snprintf(filename, sizeof(filename), "%s/wmapcurpos", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }

    if (players_on_map(m, TRUE) == 0) {
        delete_map(m);
    }

    fprintf(fp, "%d %d", wmperformstartx, wmperformstarty);
    fclose(fp);
}

/**
 * Perform actual effect of weather.  Should be called from perform_weather(),
 * or when a map is loaded. (player enter map).
 *
 * This is where things like snow, herbs, earthly rototilling, etc should
 * occur.
 *
 * Nothing will happen if the map isn't a world map.
 *
 * @param m
 * map to alter.
 */
static void weather_effect(mapstruct *m) {
    int wx, wy, x, y;

    /* if the dm shut off weather, go home */
    if (settings.dynamiclevel < 1) {
        return;
    }

    if (!m->outdoor) {
        return;
    }

    x = 0;
    y = 0;
    /* for now, just bail if it's not the worldmap */
    if (worldmap_to_weathermap(x, y, &wx, &wy, m) != 0) {
        return;
    }

    /*First, calculate temperature*/
    calculate_temperature(m);
    /* we change the world first, if needed */
    if (settings.dynamiclevel >= 5) {
        change_the_world(m);
    }
    if (settings.dynamiclevel >= 2) {
        let_it_snow(m);
        singing_in_the_rain(m);
    }
    /*if (settings.dynamiclevel >= 4) {
        feather_map(m, wx, wy, filename);
    }*/
    if (settings.dynamiclevel >= 3) {
        plant_a_garden(m);
    }
}

/**
 * Check the current square to see if we should avoid this one for
 * weather processing.
 *
 * @param[out] av
 * will contain how many tiles should be avoided. Mustn't be NULL.
 * @param m
 * map to process.
 * @param x
 * @param y
 * coordinates to process.
 * @param[out] gs
 * will contain how many snow/rain tiles are here. Mustn't be NULL.
 * @param grow
 * if 1, use the growth table, rather than the avoidance table.
 * @return
 * object pointer for any snow item it found, so you can destroy/melt it.
 */
static object *avoid_weather(int *av, mapstruct *m, int x, int y, int *gs, int grow) {
    int avoid, gotsnow, i;
    object *tmp, *snow;

    avoid = 0;
    gotsnow = 0;
    if (grow) {
        for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
            /* look for things like walls, holes, etc */
            if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR) && !(tmp->material&M_ICE || tmp->material&M_LIQUID)) {
                gotsnow++;
                snow = tmp;
            }
            for (i = 0; growth_avoids[i].name != NULL; i++) {
                if (tmp->arch == growth_avoids[i].what) {
                    avoid++;
                    break;
                }
            }
            if (!strncmp(tmp->arch->name, "biglake_", 8)) {
                avoid++;
                break;
            }
            if (avoid && gotsnow) {
                break;
            }
        }
    } else {
        for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
            for (i = 0; weather_avoids[i].name != NULL; i++) {
                /*if (!strcmp(tmp->arch->name, weather_avoids[i].name)) {*/
                if (tmp->arch == weather_avoids[i].what) {
                    // We clear FLAG_IS_FLOOR for our snow. The map's default snow does not.
                    // Avoid weirdness on the pathway to Brest and at the south pole by checking for non-floor snow
                    if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR) && weather_avoids[i].snow == 1) {
                        gotsnow++;
                        snow = tmp;
                    } else {
                        avoid++;
                    }
                    break;
                }
            }
            if (avoid && gotsnow) {
                break;
            }
        }
    }
    *gs = gotsnow;
    *av = avoid;

    return snow;
}

/**
 * Temperature is used in a lot of weather function.
 * This need to be precalculated before used.
 *
 * @param m
 * map for which to calculate the temperature. Must be a world map.
 */
static void calculate_temperature(mapstruct *m) {
    int x,y, wx, wy;
    assert(worldmap_to_weathermap(0, 0, &wx, &wy, m) == 0);
    for (x = 0; x < settings.worldmaptilesizex; x++) {
        for (y = 0; y < settings.worldmaptilesizey; y++) {
            worldmap_to_weathermap(x, y, &wx, &wy, m);
            weathermap[wx][wy].realtemp = real_world_temperature(x, y, m);
        }
    }
}

/**
 * Refactor the code to look for arch or object name as it's own code
 *
 * @param ob
 * The external object we are checking
 * Should not be NULL
 *
 * @param rep_struct
 * The weather_replace struct we are looking at.
 * Should not be NULL
 *
 * @return
 * 1 if a match occurred, 0 otherwise.
 */
static int check_replace_match(object *ob, weather_replace_t *rep_struct) {
    if (rep_struct->arch_or_name == 1) {
        if (!strcmp(ob->arch->name, rep_struct->tile)) {
            return 1;
        }
    } else {
        if (!strcmp(ob->name, rep_struct->tile)) {
            return 1;
        }
    }
    return 0;
}

/**
 * Weather insert flags.
 */
#define WEATHER_OVERLAY  1 /* If set, we set FLAG_OVERLAY_FLOOR */
#define WEATHER_NO_FLOOR 2 /* If set, we clear FLAG_IS_FLOOR */
#define WEATHER_NO_SAVE  4 /* If set, we set FLAG_NO_SAVE */

/**
 * Do an object insert for weather effects.
 *
 * @param m
 * The map to insert the weather effect on.
 * Must not be NULL.
 *
 * @param x
 * @param y
 * The location to insert the object at.
 *
 * @param at
 * The archetype to create an object from.
 *
 * @param object_flags
 * If first bit is 1, sets FLAG_OVERLAY_FLOOR on the created object.
 * If second bit is 1, clears FLAG_IS_FLOOR.
 * If third bit is 1, sets FLAG_NO_SAVE. Used for precipitation.
 *
 * @param material
 * The material of the object.
 * If 0, will skip setting the material
 *
 * @param insert_flags
 * The flags to pass to object_insert_in_map
 */
static void do_weather_insert(mapstruct *m, int x, int y, archetype *at, int8_t object_flags, uint16_t material, int insert_flags) {
    if (at != NULL) {
        object *ob = object_new();
        object_copy(&at->clone, ob);
        ob->x = x;
        ob->y = y;
        if (object_flags & WEATHER_OVERLAY)
            SET_FLAG(ob, FLAG_OVERLAY_FLOOR);
        if (object_flags & WEATHER_NO_FLOOR)
            CLEAR_FLAG(ob, FLAG_IS_FLOOR);
        if (object_flags & WEATHER_NO_SAVE)
            SET_FLAG(ob, FLAG_NO_SAVE);
        if (material)
            ob->material = material;
        object_insert_in_map(ob, m, ob, insert_flags);
    }
}

/**
 * Put or remove snow. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing. Must be a world map.
 */
static void let_it_snow(mapstruct *m) {
    int x, y, i, wx, wy;
    int nx, ny, j, d;
    int avoid, two, temp, sky, gotsnow, found, nodstk;
    const char *doublestack, *doublestack2;
    object *ob, *tmp, *oldsnow, *topfloor;
    archetype *at;

    for (nx = 0; nx < settings.worldmaptilesizex; nx++) {
        for (ny = 0; ny < settings.worldmaptilesizey; ny++) {
            /* jitter factor */
            if (rndm(0, 2) > 0) {
                x = y = d = -1;
                while (OUT_OF_REAL_MAP(m, x, y)) {
                    d++;
                    j = rndm(1, 8);
                    x = nx+freearr_x[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                    y = ny+freearr_y[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                    if (d > 15) {
                        x = nx;
                        y = ny;
                    }
                }
            } else {
                x = nx;
                y = ny;
            }
            /* we use the unjittered coordinates */
            (void)worldmap_to_weathermap(nx, ny, &wx, &wy, m);
            ob = NULL;
            at = NULL;
            /* this will definately need tuning */
            avoid = 0;
            two = 0;
            gotsnow = 0;
            nodstk = 0;
            /*temp = real_world_temperature(x, y, m);*/
            temp = weathermap[wx][wy].realtemp;
            sky = weathermap[wx][wy].sky;
            if (temp <= 0 && sky > SKY_OVERCAST && sky < SKY_FOG) {
                sky += 10; /*let it snow*/
            }
            oldsnow = avoid_weather(&avoid, m, x, y, &gotsnow, 0);
            if (!avoid) {
                if (sky >= SKY_LIGHT_SNOW && sky < SKY_HEAVY_SNOW) {
                    at = find_archetype(weather_replace[0].special_snow);
                }
                if (sky >= SKY_HEAVY_SNOW) {
                    at = find_archetype(weather_replace[1].special_snow);
                }
                if (sky >= SKY_LIGHT_SNOW) {
                    /* the bottom floor of scorn is not IS_FLOOR */
                    topfloor = NULL;
                    for (tmp = GET_MAP_OB(m, x, y); tmp; topfloor = tmp, tmp = tmp->above) {
                        if (strcmp(tmp->arch->name, "dungeon_magic") != 0) {
                            if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR)) {
                                break;
                            }
                        }
                    }
                    /* topfloor should now be the topmost IS_FLOOR=1 */
                    if (topfloor == NULL) {
                        continue;
                    }
                    if (tmp != NULL) {
                        nodstk++;
                    }
                    /* something is wrong with that sector. just skip it */
                    for (i = 0; weather_replace[i].tile != NULL; i++) {
                        if (check_replace_match(topfloor, &weather_replace[i])) {
                            if (weather_replace[i].special_snow != NULL) {
                                at = find_archetype(weather_replace[i].special_snow);
                            }
                            if (weather_replace[i].doublestack_arch != NULL && !nodstk) {
                                two++;
                                doublestack = weather_replace[i].doublestack_arch;
                            }
                            break;
                        }
                    }
                }
                if (gotsnow && at) {
                    if (!strcmp(oldsnow->arch->name, at->name)) {
                        at = NULL;
                    } else {
                        object_remove(oldsnow);
                        object_free(oldsnow,0);
                        tmp = GET_MAP_OB(m, x, y);
                        /* clean up the trees we put over the snow */
                        doublestack2 = NULL;
                        if (tmp) {
                            for (i = 0; weather_replace[i].tile != NULL; i++) {
                                if (weather_replace[i].doublestack_arch == NULL) {
                                    continue;
                                }
                                if (check_replace_match(tmp, &weather_replace[i])) {
                                    tmp = tmp->above;
                                    doublestack2 = weather_replace[i].doublestack_arch;
                                    break;
                                }
                            }
                        }
                        if (tmp != NULL && doublestack2 != NULL) {
                            if (strcmp(tmp->arch->name, doublestack2) == 0) {
                                object_remove(tmp);
                                object_free(tmp,0);
                            }
                        }
                    }
                }
                if (at != NULL) {
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY|WEATHER_NO_FLOOR, M_ICE, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                    if (two) {
                        at = NULL;
                        at = find_archetype(doublestack);
                        if (at != NULL) {
                            do_weather_insert(m, x, y, at, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ON_TOP);
                        }
                    }
                }
            }
            if (temp > 8 && GET_MAP_OB(m, x, y) != NULL) {
                /* melt some snow */
                for (tmp = GET_MAP_OB(m, x, y)->above; tmp; tmp = tmp->above) {
                    avoid = 0;
                    for (i = 0; weather_replace[i].tile != NULL; i++) {
                        if (weather_replace[i].special_snow == NULL) {
                            continue;
                        }

                        if (!strcmp(tmp->arch->name, weather_replace[i].special_snow)) {
                            avoid++;
                        }
                        if (avoid) {
                            break;
                        }
                    }
                    if (avoid) {
                        /* replace snow with a big puddle */
                        /* If it is a floor tile we're melting, try to place earth there to have *some* floor.
                         * Don't mark as overlay, or it will be stuck there forever, rather than until the map resets.
                         */
                        if (!tmp->below || QUERY_FLAG(tmp, FLAG_IS_FLOOR)) {
                            at = find_archetype("earth");
                            if (at)
                                do_weather_insert(m, x, y, at, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                        }
                        object_remove(tmp);
                        object_free(tmp,0);
                        tmp = GET_MAP_OB(m, x, y);
                        at = NULL; // Reset what arch we are looking at
                        if (tmp) {
                            // Put the snowmelt into a data list so it isn't hardcoded mid-code anymore
                            for (i = 0; weather_snowmelt[i].tile != NULL; ++i) {
                                if (!strcmp(tmp->arch->name, weather_snowmelt[i].tile)) {
                                    at = find_archetype(weather_snowmelt[i].special_snow);
                                }
                            }
                        }
                        // Default
                        if (!at) {
                            at = find_archetype("rain5_weather");
                        }
                        if (at != NULL) {
                            do_weather_insert(m, x, y, at, WEATHER_OVERLAY, M_LIQUID, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                        }
                    }
                }
            }
            /* woo it's cold out */
            if (temp < -8) {
                avoid = 0;
                for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
                    if (!strcasecmp(tmp->name, "ice")) {
                        avoid--;
                    }
                }
                tmp = GET_MAP_OB(m, x, y);
                if (tmp && (!strcasecmp(tmp->name, "sea"))) {
                    avoid++;
                } else if (tmp && (!strcasecmp(tmp->name, "sea1"))) {
                    avoid++;
                } else if (tmp && (!strcasecmp(tmp->name, "deep sea"))) {
                    avoid++;
                } else if (tmp && (!strcasecmp(tmp->name, "shallow sea"))) {
                    avoid++;
                }
                if (avoid > 0) {
                    at = find_archetype("ice");
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                }
            }
        }
    }
}

/**
 * Handle adding precipitation to the map
 *
 * @param m
 * The map we are acting on.
 *
 * @param x, y
 * The coordinates on the map we are handling.
 *
 * @param temp
 * The temperature at the tile we are handling.
 *
 * @param sky
 * The current sky conditions.
 */
static void do_precipitation(mapstruct *m, int x, int y, int temp, int sky) {
    // Do falling rain/snow here
    archetype *at = NULL;
    object *tmp = NULL;
    int avoid = 0;
    int pct_precip = 0; // 0-100: percent tiles with precipitation
    switch (sky) {
        case SKY_LIGHT_RAIN:
        case SKY_LIGHT_SNOW:
            pct_precip = 10;
            break;
        case SKY_RAIN:
        case SKY_SNOW:
            pct_precip = 30;
            break;
        case SKY_HEAVY_RAIN:
        case SKY_HEAVY_SNOW:
            pct_precip = 60;
            break;
        case SKY_HURRICANE:
        case SKY_BLIZZARD:
            pct_precip = 99;
            break;
    }
    if (rndm(0, 99) + pct_precip >= 100) {
        // Do our weather inserts here.
        // t < -2 == always snow
        // 2 >= t > -2 == rain/snow mix
        // t > 2 == always rain
        if (temp < -2 || (temp <= 2 && rndm(0, 2+temp) - 2 <= 0)) {
            at = find_archetype("snow_c");
        }
        else {
            at = find_archetype("rain");
        }
        if (at) {
            // Make sure we don't stack rains/snow ad nauseam on tiles. Also allow to switch the precip on the tile.
            tmp = GET_MAP_OB(m, x, y);
            avoid = 0;
            while (tmp) {
                if (!strcmp(tmp->arch->name, at->name)) {
                    avoid++;
                    break;
                }
                else if ((!strcmp(tmp->arch->name, "snow_c") && !strcmp(at->name, "rain")) ||
                         (!strcmp(tmp->arch->name, "rain") && !strcmp(at->name, "snow_c"))) {
                          // Remove the wrong precipitation
                          object_remove(tmp);
                          object_free(tmp, 0);
                }
                tmp = tmp->above;
            }
            if (!avoid)
                do_weather_insert(m, x, y, at, WEATHER_NO_SAVE, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ON_TOP);
        }
    }
    else {
        // Look for a rain/snow on this tile and remove it.
        tmp = GET_MAP_OB(m, x, y);
        while (tmp) {
            if (!strcmp(tmp->arch->name, "rain") || !strcmp(tmp->arch->name, "snow_c")) {
                object_remove(tmp);
                object_free(tmp, 0);
            }
            tmp = tmp->above;
        }
    }
}

/**
 * Do the precipitation for a given map
 *
 * @param m
 * The map we wish to process.
 */
void do_map_precipitation(mapstruct *m) {
    if (!m)
        return;
    // Make sure it has a weather map.
    int x, y, temp, sky, wx, wy;
    if (worldmap_to_weathermap(0, 0, &x, &y, m) != 0)
        return;
    // Now we re-do the precipitation on the map.
    for (x = 0; x < m->width; ++x)
        for (y = 0; y < m->height; ++y) {
            worldmap_to_weathermap(x, y, &wx, &wy, m);
            temp = weathermap[wx][wy].realtemp = real_world_temperature(x, y, m);
            sky = weathermap[wx][wy].sky;
            do_precipitation(m, x, y, temp, sky);
        }
}

/**
 * Process rain. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing.
 */
static void singing_in_the_rain(mapstruct *m) {
    int x, y, i, wx, wy;
    int nx, ny, d, j;
    int avoid, two, temp, sky, gotsnow, /*found,*/ nodstk;
    object *ob, *tmp, *oldsnow, *topfloor;
    const char *doublestack, *doublestack2;
    archetype *at;

    for (nx = 0; nx < settings.worldmaptilesizex; nx++) {
        for (ny = 0; ny < settings.worldmaptilesizey; ny++) {
            /* jitter factor */
            if (rndm(0, 2) > 0) {
                x = y = d = -1;
                while (OUT_OF_REAL_MAP(m, x, y)) {
                    // Save some processing when d > 15
                    if (++d > 15) {
                        x = nx;
                        y = ny;
                    }
                    else {
                        j = rndm(1, 8);
                        x = nx+freearr_x[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                        y = ny+freearr_y[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                    }
                }
            } else {
                x = nx;
                y = ny;
            }
            /* we use the unjittered coordinates */
            (void)worldmap_to_weathermap(nx, ny, &wx, &wy, m);
            ob = NULL;
            at = NULL;
            avoid = 0;
            two = 0;
            gotsnow = 0;
            nodstk = 0;
            /*temp = real_world_temperature(x, y, m);*/
            temp = weathermap[wx][wy].realtemp;
            sky = weathermap[wx][wy].sky;
            /* Handle adding precipitation here. */
            do_precipitation(m, x, y, temp, sky);

            /* it's probably allready snowing */
            if (temp < 0) {
                continue;
            }

            oldsnow = avoid_weather(&avoid, m, x, y, &gotsnow, 0);
            if (!avoid) {
                tmp = GET_MAP_OB(m, x, y);
                if (tmp) {
                    // Put the snowmelt into a data list so it isn't hardcoded mid-code anymore
                    for (i = 0; weather_snowmelt[i].tile != NULL; ++i) {
                        if (!strcmp(tmp->arch->name, weather_snowmelt[i].tile)) {
                            at = find_archetype(weather_snowmelt[i].special_snow);
                        }
                    }
                    if (at)
                        break;
                }
                if (sky == SKY_LIGHT_RAIN || sky == SKY_RAIN) {
                    switch (rndm(0, SKY_HAIL-sky)) {
                    case 0: at = find_archetype("rain1_weather"); break;
                    case 1: at = find_archetype("rain2_weather"); break;
                    default: at = NULL; break;
                    }
                }
                if (sky >= SKY_HEAVY_RAIN && sky <= SKY_HURRICANE) {
                    switch (rndm(0, SKY_HAIL-sky)) {
                    case 0: at = find_archetype("rain3_weather"); break;
                    case 1: at = find_archetype("rain4_weather"); break;
                    case 2: at = find_archetype("rain5_weather"); break;
                    default: at = NULL; break;
                    }
                }
                /* the bottom floor of scorn is not IS_FLOOR */
                topfloor = NULL;
                for (tmp = GET_MAP_OB(m, x, y); tmp; topfloor = tmp,tmp = tmp->above) {
                    if (strcmp(tmp->arch->name, "dungeon_magic") != 0) {
                        if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR)) {
                            break;
                        }
                    }
                }
                /* topfloor should now be the topmost IS_FLOOR=1 */
                if (topfloor == NULL) {
                    continue;
                }
                if (tmp != NULL) {
                    nodstk++;
                }
                /* something is wrong with that sector. just skip it */
                for (i = 0; weather_replace[i].tile != NULL; i++) {
                    if (check_replace_match(topfloor, &weather_replace[i])) {
                        if (weather_replace[i].doublestack_arch != NULL && !nodstk) {
                            two++;
                            doublestack = weather_replace[i].doublestack_arch;
                        }
                        break;
                    }
                }
                if (gotsnow && at) {
                    if (!strcmp(oldsnow->arch->name, at->name)) {
                        at = NULL;
                    } else {
                        tmp = GET_MAP_OB(m, x, y);
                        object_remove(oldsnow);
                        /* clean up the trees we put over the snow */
                        doublestack2 = NULL;
                        for (i = 0; weather_replace[i].tile != NULL; i++) {
                            if (weather_replace[i].doublestack_arch == NULL) {
                                continue;
                            }
                            if (check_replace_match(tmp, &weather_replace[i])) {
                                tmp = tmp->above;
                                doublestack2 = weather_replace[i].doublestack_arch;
                                break;
                            }
                        }
                        object_free(oldsnow,0);
                        if (tmp != NULL && doublestack2 != NULL) {
                            if (strcmp(tmp->arch->name, doublestack2) == 0) {
                                object_remove(tmp);
                                object_free(tmp,0);
                            }
                        }
                    }
                }
                if (at != NULL) {
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY, M_LIQUID, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                    if (two) {
                        at = find_archetype(doublestack);
                        if (at != NULL) {
                            do_weather_insert(m, x, y, at, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ON_TOP);
                        }
                    }
                }
            }
            /* Things evaporate fast in the heat */
            if (GET_MAP_OB(m, x, y) && temp > 8 && sky < SKY_OVERCAST && rndm(temp, 60) > 50) {
                /* evaporate */
                for (tmp = GET_MAP_OB(m, x, y)->above; tmp; tmp = tmp->above) {
                    avoid = 0;
                    if (!strcmp(tmp->arch->name, "rain1_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "rain2_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "rain3_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "rain4_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "rain5_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "mountain1_rivlets_weather")) {
                        avoid++;
                    } else if (!strcmp(tmp->arch->name, "mountain2_rivlets_weather")) {
                        avoid++;
                    }
                    if (avoid) {
                        object_remove(tmp);
                        object_free(tmp,0);
                        if (weathermap[wx][wy].humid < 100 && rndm(0, 50) == 0) {
                            weathermap[wx][wy].humid++;
                        }
                        tmp = GET_MAP_OB(m, x, y);
                        /* clean up the trees we put over the rain */
                        doublestack2 = NULL;
                        for (i = 0; weather_replace[i].tile != NULL; i++) {
                            if (weather_replace[i].doublestack_arch == NULL) {
                                continue;
                            }
                            if (check_replace_match(tmp, &weather_replace[i])) {
                                tmp = tmp->above;
                                doublestack2 = weather_replace[i].doublestack_arch;
                                break;
                            }
                        }
                        if (tmp != NULL && doublestack2 != NULL) {
                            if (strcmp(tmp->arch->name, doublestack2) == 0) {
                                object_remove(tmp);
                                object_free(tmp,0);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
}

/**
 * Process growth of various plants. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing.
 */
static void plant_a_garden(mapstruct *m) {
    int x, y, i, wx, wy;
    int avoid, two, temp, sky, gotsnow, found, days;
    object *ob, *tmp;
    archetype *at;

    days = todtick/HOURS_PER_DAY;
    for (x = 0; x < settings.worldmaptilesizex; x++) {
        for (y = 0; y < settings.worldmaptilesizey; y++) {
            (void)worldmap_to_weathermap(x, y, &wx, &wy, m);
            ob = NULL;
            at = NULL;
            avoid = 0;
            two = 0;
            gotsnow = 0;
            /*temp = real_world_temperature(x, y, m);*/
            temp = weathermap[wx][wy].realtemp;
            sky = weathermap[wx][wy].sky;
            (void)avoid_weather(&avoid, m, x, y, &gotsnow, 1);
            if (!avoid) {
                found = 0;
                for (i = 0; weather_grow[i].herb != NULL; i++) {
                    for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
                        if (strcmp(tmp->arch->name, weather_grow[i].herb) != 0) {
                            continue;
                        }

                        /* we found there is a herb here allready */
                        found++;
                        if ((float)weathermap[wx][wy].rainfall/days < weather_grow[i].rfmin ||
                            (float)weathermap[wx][wy].rainfall/days > weather_grow[i].rfmax ||
                            weathermap[wx][wy].humid < weather_grow[i].humin ||
                            weathermap[wx][wy].humid > weather_grow[i].humax ||
                            temp < weather_grow[i].tempmin ||
                            temp > weather_grow[i].tempmax ||
                            rndm(0, MIN(weather_grow[i].random/2, 1)) == 0) {
                            /* the herb does not belong, randomly delete
                              herbs to prevent overgrowth. */
                            object_remove(tmp);
                            object_free(tmp,0);
                            break;
                        }
                    }
                    /* don't doublestack herbs */
                    if (found) {
                        continue;
                    }
                    /* add a random factor */
                    if (rndm(1, weather_grow[i].random) != 1) {
                        continue;
                    }
                    /* we look up through two tiles for a matching tile */
                    if (weather_grow[i].tile != NULL && GET_MAP_OB(m, x, y) != NULL) {
                        if (strcmp(GET_MAP_OB(m, x, y)->arch->name, weather_grow[i].tile) != 0) {
                            if (GET_MAP_OB(m, x, y)->above != NULL) {
                                if (strcmp(GET_MAP_OB(m, x, y)->above->arch->name, weather_grow[i].tile) != 0) {
                                    continue;
                                }
                            } else {
                                continue;
                            }
                        }
                    }
                    if ((float)weathermap[wx][wy].rainfall/days < weather_grow[i].rfmin ||
                        (float)weathermap[wx][wy].rainfall/days > weather_grow[i].rfmax) {
                        continue;
                    }
                    if (weathermap[wx][wy].humid < weather_grow[i].humin ||
                        weathermap[wx][wy].humid > weather_grow[i].humax) {
                        continue;
                    }
                    if (temp < weather_grow[i].tempmin ||
                        temp > weather_grow[i].tempmax) {
                        continue;
                    }
                    if ((!GET_MAP_OB(m, x, y)) ||
                        GET_MAP_OB(m, x, y)->elevation < weather_grow[i].elevmin ||
                        GET_MAP_OB(m, x, y)->elevation > weather_grow[i].elevmax) {
                        continue;
                    }
                    /* we got this far.. must be a match */
                    at = find_archetype(weather_grow[i].herb);
                    break;
                }
                if (at != NULL) {
                    /* XXX is overlay_floor right?  maybe.. */
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                }
            }
        }
    }
}

/**
 * Process worldmap regrowth. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing.
 */
static void change_the_world(mapstruct *m) {
    int x, y, i, wx, wy;
    int nx, ny, j, d;
    int avoid, two, temp, sky, gotsnow, found, days;
    object *ob, *tmp, *doublestack;
    archetype *at, *dat;

    days = todtick/HOURS_PER_DAY;
    for (nx = 0; nx < settings.worldmaptilesizex; nx++) {
        for (ny = 0; ny < settings.worldmaptilesizey; ny++) {
            /* jitter factor */
            if (rndm(0, 2) > 0) {
                x = y = d = -1;
                while (OUT_OF_REAL_MAP(m, x, y)) {
                    d++;
                    j = rndm(1, 8);
                    x = nx+freearr_x[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                    y = ny+freearr_y[j]*(rndm(0, 1)+rndm(0, 1)+rndm(0, 1)+1);
                    if (d > 15) {
                        x = nx;
                        y = ny;
                    }
                }
            } else {
                x = nx;
                y = ny;
            }
            /* we use the unjittered coordinates */
            (void)worldmap_to_weathermap(nx, ny, &wx, &wy, m);
            ob = NULL;
            at = NULL;
            dat = NULL;
            avoid = 0;
            two = 0;
            gotsnow = 0;
            /*temp = real_world_temperature(x, y, m);*/
            temp = weathermap[wx][wy].realtemp;
            sky = weathermap[wx][wy].sky;
            (void)avoid_weather(&avoid, m, x, y, &gotsnow, 1);
            if (!avoid) {
                for (i = 0; weather_tile[i].herb != NULL; i++) {
                    found = 0;
                    doublestack = NULL;
                    if (GET_MAP_OB(m, x, y)) {
                        for (tmp = GET_MAP_OB(m, x, y)->above; tmp; tmp = tmp->above) {
                            if (weather_tile[i].tile != NULL) {
                                if (strcmp(tmp->arch->name, weather_tile[i].tile) == 0) {
                                    doublestack = tmp;
                                    continue;
                                }
                            }
                            if (strcmp(tmp->arch->name, weather_tile[i].herb) != 0) {
                                continue;
                            }

                            if ((float)weathermap[wx][wy].rainfall/days < weather_tile[i].rfmin ||
                                (float)weathermap[wx][wy].rainfall/days > weather_tile[i].rfmax ||
                                weathermap[wx][wy].humid < weather_tile[i].humin ||
                                weathermap[wx][wy].humid > weather_tile[i].humax ||
                                temp < weather_tile[i].tempmin ||
                                temp > weather_tile[i].tempmax) {
                                object_remove(tmp);
                                object_free(tmp,0);
                                if (doublestack) {
                                    object_remove(doublestack);
                                    object_free(doublestack,0);
                                }
                                break;
                            } else {
                                found++; /* there is one here allready. leave it */
                                break;
                            }
                        }
                    }
                    if (found) {
                        break;
                    }

                    /* add a random factor */
                    if (rndm(1, weather_tile[i].random) != 1) {
                        continue;
                    }
                    if ((float)weathermap[wx][wy].rainfall/days < weather_tile[i].rfmin ||
                        (float)weathermap[wx][wy].rainfall/days > weather_tile[i].rfmax) {
                        continue;
                    }
                    if (weathermap[wx][wy].humid < weather_tile[i].humin ||
                        weathermap[wx][wy].humid > weather_tile[i].humax) {
                        continue;
                    }
                    if (temp < weather_tile[i].tempmin ||
                        temp > weather_tile[i].tempmax) {
                        continue;
                    }
                    if ( (!GET_MAP_OB(m, x, y)) ||
                        GET_MAP_OB(m, x, y)->elevation < weather_tile[i].elevmin ||
                        GET_MAP_OB(m, x, y)->elevation > weather_tile[i].elevmax) {
                        continue;
                    }
                    /* we got this far.. must be a match */
                    if (GET_MAP_OB(m, x, y) && strcmp(GET_MAP_OB(m, x, y)->arch->name, weather_tile[i].herb) == 0) {
                        break; /* no sense in doubling up */
                    }
                    at = find_archetype(weather_tile[i].herb);
                    break;
                }
                if (at != NULL) {
                    if (weather_tile[i].tile != NULL && GET_MAP_OB(m, x, y) && strcmp(weather_tile[i].tile, GET_MAP_OB(m, x, y)->arch->name) != 0) {
                        dat = find_archetype(weather_tile[i].tile);
                    }
                    if (dat != NULL) {
                        do_weather_insert(m, x, y, dat, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                    }
                    if (gotsnow == 0) {
                        do_weather_insert(m, x, y, at, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|(dat ? INS_ON_TOP : INS_ABOVE_FLOOR_ONLY));
                    }
                }
            }
        }
    }
}

/**
 * Calculate the direction to push an object from wind
 *
 * @param m
 * The map the player is on.
 *
 * @param x,y
 * The coordinates of the player on the map
 *
 * @param move_type
 * The movement type the object is attempting to use.
 *
 * @param wt
 * The total weight the object has (incl. carrying)
 *
 * @param stats
 * The stat block of the object, or NULL if it is not alive.
 *
 * @return
 * The direction to push (1-8), or 0 if push does not occur
 */
uint8_t wind_blow_object(mapstruct *m, int x, int y, MoveType move_type, int32_t wt, living *stats) {
    // If we're inside, the weather can't get us :P
    if (!m || !m->outdoor)
        return 0;
    // First, we get the weathermap for this location
    int nx, ny;
    if (worldmap_to_weathermap(x, y, &nx, &ny, m))
        return 0;
    int windspeed = weathermap[nx][ny].windspeed;
    int is_fly = move_type & MOVE_FLYING;
    // If not flying, then strong winds are needed to affect you.
    if (!is_fly)
        windspeed -= 20;
    // If no wind, then no push.
    if (windspeed <= 0)
        return 0;
    // Reduce effect from carrying more stuff.
    // Also, being on the ground makes the same wind increase affect you less as well.
    // Higher strength characters can also resist being blown by the wind when on the ground.
    if (!is_fly)
        wt /= (10000 * (stats && stats->Str) ? stats->Str : 1);
    // When flying, we care about Dex over Str.
    else
        wt /= (20000 * (stats && stats->Dex) ? stats->Dex : 1);
    // Massive things are pushed around less easily.
    if (windspeed*2 < wt)
        return 0;
    // The push will not happen every try. The greater the wind, the more often it succeeds.
    // Also, the lighter the object, the more often it succeeds
    // We do two rolls because it normalizes the effects better than a single roll.
    if (rndm(0, windspeed)+rndm(0, windspeed) < wt)
        return 0;
    // winddir is the direction the wind is coming from.
    // so we need to reverse it to push where the wind is going to.
    return absdir(weathermap[nx][ny].winddir+4);
}

/**
 * Keep track of how much rain has fallen in a given weathermap square.
 */
void process_rain(void) {
    int x, y, rain;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            rain = weathermap[x][y].sky;
            if (rain >= SKY_LIGHT_SNOW) {
                rain -= 10;
            }
            if (rain > SKY_OVERCAST && rain < SKY_FOG) {
                rain -= SKY_OVERCAST;
                weathermap[x][y].rainfall += rain;
            }
        }
    }
}
