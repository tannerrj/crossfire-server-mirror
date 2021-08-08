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
static void read_winddirmap(void);
static void read_windspeedmap(void);
static void init_wind(void);
static void read_gulfstreammap(void);
static void init_gulfstreammap(void);
static void read_humidmap(void);
static void write_elevmap(void);
static void read_elevmap(void);
static void write_watermap(void);
static void read_watermap(void);
static void init_humid_elev(void);
static void read_temperaturemap(void);
static void init_temperature(void);
static void read_rainfallmap(void);
static void init_rainfall(void);
static void init_weatheravoid (weather_avoids_t wa[]);
static void perform_weather(void);
static object *avoid_weather(int *av, mapstruct *m, int x, int y, int *gs, int grow);
static void calculate_temperature(mapstruct *m);
static void let_it_snow(mapstruct *m);
static void singing_in_the_rain(mapstruct *m);
static void plant_a_garden(mapstruct *m);
static void change_the_world(mapstruct *m);
static char *weathermap_to_worldmap_corner(int wx, int wy, int *x, int *y, int dir, char* buffer, int bufsize);
static int polar_distance(int x, int y, int equator);
static void update_humid(void);
static int humid_tile(int x, int y);
static void temperature_calc(int x, int y, const timeofday_t *tod);
static int real_temperature(int x, int y);
static void smooth_pressure(void);
static void perform_pressure(void);
static void smooth_wind(void);
static void plot_gulfstream(void);
static void compute_sky(void);
static void process_rain(void);
static void spin_globe(void);
static void weather_effect(mapstruct *m);

/** Speed of the gulf stream. */
static int gulf_stream_speed[GULF_STREAM_WIDTH][WEATHERMAPTILESY];
/** Direction of the gulf stream. */
static int gulf_stream_dir[GULF_STREAM_WIDTH][WEATHERMAPTILESY];
static int gulf_stream_start;
static int gulf_stream_direction;

/** How to alter darkness, based on time of day and season. */
static const int season_timechange[5][HOURS_PER_DAY] = {
/*    0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  1  2  3  4  5  6  7  8  9 10 11 12 13 */
    { 0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0 }
};

/** How to alter the temperature, based on the hour of the day. */
static const int season_tempchange[HOURS_PER_DAY] = {
/*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  1  2  3  4  5  6  7  8  9 10 11 12 13 */
    0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

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

/* This stuff is for creating the images. */

/* Colour offsets into pixel array. */
#define RED   0
#define GREEN 1
#define BLUE  2

/**
 * Colours used for wind directions.
 * winddir is the direction wind is coming from.
 *   812  456
 *   7 3  3 7
 *   654  218
 */
static const uint32_t directions[] = {
    0x0000FFFF, /* south */
    0x000000FF, /* south west */
    0x00FF00FF, /* west */
    0x00FFFFFF, /* north west */
    0x00000000, /* north */
    0x00FF0000, /* north east */
    0x00FFFF00, /* east */
    0x0000FF00  /* south east */
};

/**
 * Colours used for weather types.
 */
static const uint32_t skies[] = {
    0x000000FF, /* SKY_CLEAR         0 */
    0x000000BD, /* SKY_LIGHTCLOUD    1 */
    0x0000007E, /* SKY_OVERCAST      2 */
    0x0000FF00, /* SKY_LIGHT_RAIN    3 */
    0x0000BD00, /* SKY_RAIN          4 */
    0x00007E00, /* SKY_HEAVY_RAIN    5 */
    0x00FFFF00, /* SKY_HURRICANE     6 */
/* wierd weather 7-12 */
    0x00FF0000, /* SKY_FOG           7 */
    0x00FF00FF, /* SKY_HAIL          8 */
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
/* snow */
    0x003F3F3F, /* SKY_LIGHT_SNOW    13 */
    0x007E7E7E, /* SKY_SNOW          14 */
    0x00BDBDBD, /* SKY_HEAVY_SNOW    15 */
    0x00FFFFFF  /* SKY_BLIZZARD      16 */
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
    /* call the weather calculators, here, in order */
    if (settings.dynamiclevel > 0) {
        perform_pressure();     /* pressure is the random factor */
        smooth_wind();          /* calculate the wind. depends on pressure */
        plot_gulfstream();
        update_humid();
        init_temperature();
        //compute_sky(); This is done in perform_weather
        if (tod.hour == 0) {
            process_rain();
        }
    }
    /* perform_weather must follow calculators */
    perform_weather();
    if (settings.dynamiclevel > 0) {
        spin_globe();
    }
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

/**
 * Save wind direction.
 */
void write_winddirmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/winddirmap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing wind direction map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].winddir);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read the wind direction. Will initialize the direction if file doesn't exist.
 */
static void read_winddirmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y, d;

    snprintf(filename, sizeof(filename), "%s/winddirmap", settings.localdir);
    LOG(llevDebug, "Reading wind direction data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing wind direction maps...\n");
        init_wind();
        write_winddirmap();
        LOG(llevDebug, "Done\n");
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &d);
            weathermap[x][y].winddir = d;
            if (weathermap[x][y].winddir < 1 ||
                weathermap[x][y].winddir > 8) {
                weathermap[x][y].winddir = rndm(1, 8);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Save the wind speed.
 */
void write_windspeedmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/windspeedmap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing wind speed map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%hd ", weathermap[x][y].windspeed);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read the wind speed, or init it if save file doesnt exist.
 */
static void read_windspeedmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;
    int8_t d;

    snprintf(filename, sizeof(filename), "%s/windspeedmap", settings.localdir);
    LOG(llevDebug, "Reading wind speed data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing wind speed maps...\n");
        init_wind();
        write_windspeedmap();
        LOG(llevDebug, "Done\n");
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%hhd ", &d);
            weathermap[x][y].windspeed = d;
            if (weathermap[x][y].windspeed < 0 ||
                weathermap[x][y].windspeed > 120) {
                weathermap[x][y].windspeed = rndm(1, 30);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Initialize the wind randomly. Does both direction and speed in one pass
 */
static void init_wind(void) {
    int x, y;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].winddir = rndm(1, 8);
            weathermap[x][y].windspeed = rndm(1, 10);
        }
    }
}

/**
 * Save the gulf stream
 */
void write_gulfstreammap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/gulfstreammap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing gulf stream map to file.\n");
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", gulf_stream_speed[x][y]);
        }
        fprintf(fp, "\n");
    }
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", gulf_stream_dir[x][y]);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read the gulf stream, or initialize it if no saved information.
 */
static void read_gulfstreammap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/gulfstreammap", settings.localdir);
    LOG(llevDebug, "Reading gulf stream data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing gulf stream maps...\n");
        init_gulfstreammap();
        write_gulfstreammap();
        LOG(llevDebug, "Done\n");
        return;
    }
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &gulf_stream_speed[x][y]);
            if (gulf_stream_speed[x][y] < 0 ||
                gulf_stream_speed[x][y] > 120) {
                gulf_stream_speed[x][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
            }
        }
        (void)fscanf(fp, "\n");
    }
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &gulf_stream_dir[x][y]);
            if (gulf_stream_dir[x][y] < 0 ||
                gulf_stream_dir[x][y] > 120) {
                gulf_stream_dir[x][y] = rndm(1, 8);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Initialize the gulf stream.
 */
static void init_gulfstreammap(void) {
    int x, y, tx;

    /* build a gulf stream */
    x = rndm(GULF_STREAM_WIDTH, WEATHERMAPTILESX-GULF_STREAM_WIDTH);
    /* doth the great bob inhale or exhale? */
    gulf_stream_direction = rndm(0, 1);
    gulf_stream_start = x;

    if (gulf_stream_direction) {
        for (y = WEATHERMAPTILESY-1; y >= 0; y--) {
            switch (rndm(0, 6)) {
            case 0:
            case 1:
            case 2:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == 0) {
                        gulf_stream_dir[tx][y] = 7;
                    } else {
                        gulf_stream_dir[tx][y] = 8;
                        if (tx == 0) {
                            x--;
                        }
                    }
                }
                break;

            case 3:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    gulf_stream_dir[tx][y] = 7;
                }
                break;

            case 4:
            case 5:
            case 6:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == WEATHERMAPTILESX-1) {
                        gulf_stream_dir[tx][y] = 7;
                    } else {
                        gulf_stream_dir[tx][y] = 6;
                        if (tx == 0) {
                            x++;
                        }
                    }
                }
                break;
            }
        }
    } else { /* go right to left */
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            switch(rndm(0, 6)) {
            case 0:
            case 1:
            case 2:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == 0) {
                        gulf_stream_dir[tx][y] = 3;
                    } else {
                        gulf_stream_dir[tx][y] = 2;
                        if (tx == 0) {
                            x--;
                        }
                    }
                }
                break;

            case 3:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    gulf_stream_dir[tx][y] = 3;
                }
                break;

            case 4:
            case 5:
            case 6:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == WEATHERMAPTILESX-1) {
                        gulf_stream_dir[tx][y] = 3;
                    } else {
                        gulf_stream_dir[tx][y] = 4;
                        if (tx == 0) {
                            x++;
                        }
                    }
                }
                break;
            }
        }
    } /* done */
}

/**
 * Save humidity information.
 */
void write_humidmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/humidmap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing humidity map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].humid);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read humidity information, or initialize it randomly if no saved information available.
 */
static void read_humidmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y, d;

    snprintf(filename, sizeof(filename), "%s/humidmap", settings.localdir);
    LOG(llevDebug, "Reading humidity data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing humidity and elevation maps...\n");
        init_humid_elev();
        write_elevmap();
        write_humidmap();
        write_watermap();
        LOG(llevDebug, "Done\n");
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &d);
            weathermap[x][y].humid = d;
            if (weathermap[x][y].humid < 0 ||
                weathermap[x][y].humid > 100) {
                weathermap[x][y].humid = rndm(0, 100);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Save the average elevation information.
 */
static void write_elevmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/elevmap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing elevation map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].avgelev);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Load or initialize the elevation information.
 */
static void read_elevmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/elevmap", settings.localdir);
    LOG(llevDebug, "Reading elevation data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        /* initializing these is expensive, and should have been done
           by the humidity.  It's not worth the wait to do it twice. */
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &weathermap[x][y].avgelev);
            // Randomization when out of range does weird stuff.
            // Cap them instead.
            if (weathermap[x][y].avgelev < -10000) {
                weathermap[x][y].avgelev = -10000;
            } else if (weathermap[x][y].avgelev > 15000) {
                weathermap[x][y].avgelev = 15000;
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Save water percent information.
 */
static void write_watermap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/watermap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing water map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].water);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Load or initialize water information.
 */
static void read_watermap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y, d;

    snprintf(filename, sizeof(filename), "%s/watermap", settings.localdir);
    LOG(llevDebug, "Reading water data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        /* initializing these is expensive, and should have been done
           by the humidity.  It's not worth the wait to do it twice. */
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%d ", &d);
            weathermap[x][y].water = d;
            if (weathermap[x][y].water > 100) {
                weathermap[x][y].water = rndm(0, 100);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Method to abstract some of the mess of the humidity map.
 *
 * @param dir
 * The map corner to handle. Should be one of 2, 4, 6, or 8.
 *
 * @param x
 * @param y
 * The x,y coordinates of the weather map we are handling.
 *
 * @param tx
 * @param ty
 * Pointers to the coordinates on the map we are loading.
 * The looping structure needs these.
 *
 * @return
 * 0 if successful, -1 on failure.
 */
static int load_humidity_map_part(mapstruct **m, int dir, int x, int y, int *tx, int *ty) {
    char mapname[MAX_BUF];
    if (!m || !tx || !ty)
        return -1;
    // Now we do what was wanted.
    weathermap_to_worldmap_corner(x, y, tx, ty, dir, mapname, sizeof(mapname));
    *m = mapfile_load(mapname, 0);
    if (*m == NULL) {
        return -1;
    }

    int res = load_overlay_map(mapname, *m);
    if (res != 0) {
        return -1;
    }
    return 0;
}

/**
 * Do the water and elevation updates on the given map tile.
 *
 * @param m
 * The map we are working on
 *
 * @param x
 * @param y
 * The x,y coordinates we are handling for the current space.
 *
 * @param water
 * Pointer to a water count variable.
 * Will be updated by this function.
 *
 * @param elev
 * Pointer to an elevation sum variable.
 * Will be updated by this function.
 *
 * @return
 * 0 if successful, -1 if failure
 */
static int do_water_elev_calc(mapstruct *m, int x, int y, int *water, int64_t *elev) {
    if (!m || !water || !elev)
        return -1;
    object *ob = GET_MAP_OB(m, x, y);
    if (ob) {
        if (QUERY_FLAG(ob, FLAG_IS_WATER)) {
            (*water)++;
        }
        // Deserts will reduce the precipitation in the spaces they exist in.
        if (strcmp(ob->name, "desert") == 0) {
            (*water)--;
        }
        (*elev) += ob->elevation;
    }
    return 0;
}

/**
 * Initialize both humidity and elevation.
 */
static void init_humid_elev(void) {
    int x, y, tx, ty, nx, ny, ax, ay, j;
    int spwtx, spwty;
    int64_t elev;
    int water, space;
    mapstruct *m;

    /* handling of this is kinda nasty.  For that reason,
     * we do the elevation here too.  Not because it makes the
     * code cleaner, or makes handling easier, but because I do *not*
     * want to maintain two of these nightmares.
     */

    spwtx = (settings.worldmaptilesx*settings.worldmaptilesizex)/WEATHERMAPTILESX;
    spwty = (settings.worldmaptilesy*settings.worldmaptilesizey)/WEATHERMAPTILESY;
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            water = 0;
            elev = 0;
            nx = 0;
            ny = 0;
            space = 0;

            /* top left */
            if (load_humidity_map_part(&m, 8, x, y, &tx, &ty) == -1)
                continue;

            for (nx = 0, ax = tx; nx < spwtx && ax < settings.worldmaptilesizex && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = ty; ny < spwty && ay < settings.worldmaptilesizey && space < spwtx*spwty; ay++, ny++, space++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev);
                }
            }
            delete_map(m);

            /* bottom left */
            if (load_humidity_map_part(&m, 6, x, y, &tx, &ty) == -1)
                continue;

            j = ny;
            for (nx = 0, ax = tx; nx < spwtx && ax < settings.worldmaptilesizex && space < spwtx*spwty; ax++, nx++) {
                for (ny = j, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev);
                }
            }
            delete_map(m);

            /* top right */
            if (load_humidity_map_part(&m, 2, x, y, &tx, &ty) == -1)
                continue;

            for (ax = MAX(0, tx-(spwtx-1)); nx < spwtx && ax < tx && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = ty; ny < spwty && ay < settings.worldmaptilesizey && space < spwtx*spwty; ay++, ny++, space++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev);
                }
            }
            delete_map(m);

            /* bottom left */
            if (load_humidity_map_part(&m, 4, x, y, &tx, &ty) == -1)
                continue;

            for (nx = 0, ax = MAX(0, tx - (spwtx-1)); nx < spwtx && ax < tx && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev);
                }
            }
            delete_map(m);

            /* jesus thats confusing as all hell */
            // Per meteorology, full ocean usually only gets to 80% humidity at the standard height it is measured.
            // This should help prevent a forever-hurricane over the ocean.
            weathermap[x][y].humid = water*80/(spwtx*spwty);
            weathermap[x][y].avgelev = elev/(spwtx*spwty);
            weathermap[x][y].water = water*100/(spwtx*spwty);
        }
    }

    /* and this does all the real work */
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].humid = humid_tile(x, y);
        }
    }
}

/**
 * Save temperature information.
 */
void write_temperaturemap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/temperaturemap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing temperature map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].temp);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Load or initialize temperature information.
 */
static void read_temperaturemap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/temperaturemap", settings.localdir);
    LOG(llevDebug, "Reading temperature data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing temperature map.\n");
        init_temperature();
        write_temperaturemap();
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%hd ", &weathermap[x][y].temp);
            if (weathermap[x][y].temp < -30 ||
                weathermap[x][y].temp > 60) {
                weathermap[x][y].temp = rndm(-10, 40);
            }
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Initialize the temperature based on the time.
 */
static void init_temperature(void) {
    int x, y;
    timeofday_t tod;

    get_tod(&tod);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            temperature_calc(x, y, &tod);
        }
    }
}

/**
 * Save rainfall information.
 */
void write_rainfallmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/rainfallmap", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    LOG(llevDebug, "Writing rainfall map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%u ", weathermap[x][y].rainfall);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/**
 * Read or initialize rainfall information.
 */
static void read_rainfallmap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/rainfallmap", settings.localdir);
    LOG(llevDebug, "Reading rainfall data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing rainfall map...\n");
        init_rainfall();
        write_rainfallmap();
        return;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%u ", &weathermap[x][y].rainfall);
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
}

/**
 * Initialize rainfall.
 */
static void init_rainfall(void)
{
    int x, y;
    int days = todtick/HOURS_PER_DAY;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            if (weathermap[x][y].humid < 10) {
                weathermap[x][y].rainfall = days/20;
            } else if (weathermap[x][y].humid < 20) {
                weathermap[x][y].rainfall = days/15;
            } else if (weathermap[x][y].humid < 30) {
                weathermap[x][y].rainfall = days/10;
            } else if (weathermap[x][y].humid < 40) {
                weathermap[x][y].rainfall = days/5;
            } else if (weathermap[x][y].humid < 50) {
                weathermap[x][y].rainfall = days/2;
            } else if (weathermap[x][y].humid < 60) {
                weathermap[x][y].rainfall = days;
            } else if (weathermap[x][y].humid < 80) {
                weathermap[x][y].rainfall = days*2;
            } else {
                weathermap[x][y].rainfall = days*3;
            }
        }
    }
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
    int x, tx, ty;
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
    read_winddirmap();
    read_windspeedmap();
    read_gulfstreammap();
    read_humidmap();
    read_watermap(); /* On first run, we want to do this after humidity. Otherwise, it doesn't seem to matter */
    read_elevmap(); /* elevation must allways follow humidity */
    read_temperaturemap();
    gulf_stream_direction = rndm(0, 1);
    for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
        for (ty = 0; ty < WEATHERMAPTILESY-1; ty++) {
            if (gulf_stream_direction) {
                switch (gulf_stream_dir[tx][ty]) {
                case 2: gulf_stream_dir[tx][ty] = 6; break;
                case 3: gulf_stream_dir[tx][ty] = 7; break;
                case 4: gulf_stream_dir[tx][ty] = 8; break;
                }
            } else {
                switch (gulf_stream_dir[tx][ty]) {
                case 6: gulf_stream_dir[tx][ty] = 2; break;
                case 7: gulf_stream_dir[tx][ty] = 3; break;
                case 8: gulf_stream_dir[tx][ty] = 4; break;
                }
            }
        }
    }

    gulf_stream_start = rndm(GULF_STREAM_WIDTH, WEATHERMAPTILESY-GULF_STREAM_WIDTH);
    read_rainfallmap();

    LOG(llevDebug, "Done reading weathermaps\n");
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
    // Initialize the sky so we can get accurate precipitation at initial load.
    compute_sky();
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
static void perform_weather(void) {
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
 * Convert coordinates from world map to weather tiles.
 *
 * @param x
 * @param y
 * coordinates to convert.
 * @param[out] wx
 * @param[out] wy
 * weather map coordinates.
 * @param m
 * map. Should be a world map.
 * @return
 * -1 if you give it something it can't figure out. 0 normally.
 */
int worldmap_to_weathermap(int x, int y, int *wx, int *wy, mapstruct* m) {
    int spwtx, spwty;
    int fx, fy;
    int nx, ny;
    char* filename = m->path;

    spwtx = (settings.worldmaptilesx*settings.worldmaptilesizex)/WEATHERMAPTILESX;
    spwty = (settings.worldmaptilesy*settings.worldmaptilesizey)/WEATHERMAPTILESY;

    while (*filename == '/') {
        filename++;
    }

    fx = MAP_WORLDPARTX(m);
    fy = MAP_WORLDPARTY(m);

    // -2 is our sentinel value to say that we tried to load and could not.
    // If either is -2, then this is not a world map.
    if (fx == -2 || fy == -2) {
        return -1;
    }
    if (fx > settings.worldmapstartx + settings.worldmaptilesx ||
        fx < settings.worldmapstartx ||
        fy > settings.worldmapstarty + settings.worldmaptilesy ||
        fy < settings.worldmapstarty) {
        LOG(llevDebug, "worldmap_to_weathermap(%s)\n", filename);
        // If we don't populate the variables, mark as -2.
        // This tells us to not check again, as it is not a world map.
        int amt = sscanf(filename, "world/world_%d_%d", &fx, &fy);
        if (amt == 2) {
            MAP_WORLDPARTX(m) = fx;
            MAP_WORLDPARTY(m) = fy;
        }
        else {
            MAP_WORLDPARTX(m) = -2;
            MAP_WORLDPARTY(m) = -2;
        }

    }
    if (fx > settings.worldmapstartx + settings.worldmaptilesx ||
        fx < settings.worldmapstartx) {
        return -1;
    }
    if (fy > settings.worldmapstarty + settings.worldmaptilesy ||
        fy < settings.worldmapstarty) {
        return -1;
    }
    fx -= settings.worldmapstartx;
    fy -= settings.worldmapstarty;

    nx = fx*settings.worldmaptilesizex+x;
    ny = fy*settings.worldmaptilesizey+y;

    *wx = nx/spwtx;
    *wy = ny/spwty;

    return 0;
}

/**
 * Return the path of the map in specified direction.
 *
 * @param wx
 * @param wy
 * weather map coordinates.
 * @param[out] x
 * @param[out] y
 * will contain coordinates in the new map. Mustn't be NULL.
 * @param dir
 * direction to find map for. Valid values are 2 4 6 8 for the corners.
 * @param buffer
 * buffer that will contain the path of map in specified direction.
 * @param bufsize
 * length of buffer
 * @return
 * buffer.
 */
static char *weathermap_to_worldmap_corner(int wx, int wy, int *x, int *y, int dir, char* buffer, int bufsize) {
    int spwtx, spwty;
    int tx, ty, nx, ny;

    spwtx = (settings.worldmaptilesx*settings.worldmaptilesizex)/WEATHERMAPTILESX;
    spwty = (settings.worldmaptilesy*settings.worldmaptilesizey)/WEATHERMAPTILESY;
    switch (dir) {
    case 2: wx++; break;
    case 4: wx++; wy++; break;
    case 6: wy++; break;
    case 8: break;
    }
    if (wx > 0) {
        tx = (wx*spwtx)-1;
    } else {
        tx = wx;
    }
    if (wy > 0) {
        ty = (wy*spwty)-1;
    } else {
        ty = wy;
    }

    nx = (tx/settings.worldmaptilesizex)+settings.worldmapstartx;
    ny = (ty/settings.worldmaptilesizey)+settings.worldmapstarty;
    snprintf(buffer, bufsize, "world/world_%d_%d", nx, ny);

    *x = tx%settings.worldmaptilesizex;
    *y = ty%settings.worldmaptilesizey;
    return buffer;
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
 * Calculates the distance to the nearest pole.
 * @param x
 * @param y
 * weathermap coordinates.
 * @param equator
 * current location of the equator.
 * @return
 * distance as an int.
 */
static int polar_distance(int x, int y, int equator) {
    if ((x+y) > equator) { /* south pole */
        x = WEATHERMAPTILESX - x;
        y = WEATHERMAPTILESY - y;
        return ((x+y)/2);
    } else if ((x+y) < equator) { /* north pole */
        return ((x+y)/2);
    } else {
        return equator/2;
    }
}

/**
 * Update the humidity for all weathermap tiles.
 */
static void update_humid(void) {
    int x, y;

    for (y = 0; y < WEATHERMAPTILESY; y++) {
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            weathermap[x][y].humid = humid_tile(x, y);
        }
    }
}

/**
 * Calculate the humidity of this tile.
 *
 * @param x
 * @param y
 * weathermap coordinates we wish to calculate humidity for.
 * @return
 * the humidity of the weathermap square.
 */
static int humid_tile(int x, int y) {
    int ox, oy, humid;

    ox = x;
    oy = y;

    /* find the square the wind is coming from, without going out of bounds */

    if (weathermap[x][y].winddir == 8 || weathermap[x][y].winddir <= 2) {
        if (y != 0) {
            oy = y-1;
        }
    }
    if (weathermap[x][y].winddir >= 6) {
        if (x != 0) {
            ox = x-1;
        }
    }
    if (weathermap[x][y].winddir >= 4 && weathermap[x][y].winddir <= 6) {
        if (y < WEATHERMAPTILESY-1) {
            oy = y+1;
        }
    }
    if (weathermap[x][y].winddir >= 2 && weathermap[x][y].winddir <= 4) {
        if (x < WEATHERMAPTILESX-1) {
            ox = x+1;
        }
    }
    humid = (weathermap[x][y].humid*2+
        weathermap[ox][oy].humid*weathermap[ox][oy].windspeed +
        weathermap[x][y].water+rndm(0, 10))/
        (weathermap[ox][oy].windspeed+3)+rndm(0, 5);
    if (humid < 0) {
        humid = 1;
    }
    if (humid > 100) {
        humid = 100;
    }
    return humid;
}

/**
 * Calculate temperature of a spot.
 *
 * @param x
 * @param y
 * weathermap coordinates.
 * @param tod
 * time of day.
 */
static void temperature_calc(int x, int y, const timeofday_t *tod) {
    int dist, equator, elev, n;
    float diff, tdiff;

    equator = (WEATHERMAPTILESX+WEATHERMAPTILESY)/4;
    diff = (float)(EQUATOR_BASE_TEMP-POLAR_BASE_TEMP)/(float)equator;
    tdiff = (float)SEASONAL_ADJUST/(float)(MONTHS_PER_YEAR/2.0);
    equator *= 2;
    n = 0;
    /* we essentially move the equator during the season */
    if (tod->month > (MONTHS_PER_YEAR/2)) { /* EOY */
        n -= (tod->month*tdiff);
    } else {
        n = (MONTHS_PER_YEAR - tod->month)*tdiff;
    }
    dist = polar_distance(x-n/2, y-n/2, equator);

    /* now we have the base temp, unadjusted for time.  Time adjustment
       is not recorded on the map, rather, it's done JIT. */
    weathermap[x][y].temp = (int)(dist * diff);
    /* just scrap these for now, its mostly ocean */
    if (weathermap[x][y].avgelev < 0) {
        elev = 0;
    } else {
        elev = MAX(10000, weathermap[x][y].avgelev)/1000;
    }
    weathermap[x][y].temp -= elev;
}

/**
 * Compute the real (adjusted) temperature of a given weathermap tile.
 * This takes into account the wind, base temp, sunlight, and other fun
 * things.  Seasons are automatically handled by moving the equator.
 * Elevation is partially considered in the base temp. x and y are the
 * weathermap coordinates.
 *
 * @param x
 * @param y
 * weathermap coordinates.
 */
static int real_temperature(int x, int y) {
    int i, temp;
    timeofday_t tod;

    /* adjust for time of day */
    temp = weathermap[x][y].temp;
    get_tod(&tod);
    for (i = HOURS_PER_DAY/2; i < HOURS_PER_DAY; i++) {
        temp += season_tempchange[i];
        /* high amounts of water has a buffering effect on the temp */
        if (weathermap[x][y].water > 33) {
            i++;
        }
    }
    for (i = 0; i <= tod.hour; i++) {
        temp += season_tempchange[i];
        if (weathermap[x][y].water > 33) {
            i++;
        }
    }

    /* windchill */
    for (i = 1; i < weathermap[x][y].windspeed; i += i) {
        temp--;
    }

    return temp;
}

/**
 * Compute the temperature for a specific square.  Used to normalize elevation.
 *
 * @param x
 * @param y
 * map coordinates.
 * @param m
 * map we're on.
 * @return
 * temperature.
 */
int real_world_temperature(int x, int y, mapstruct *m) {
    int wx, wy, temp, eleva, elevb;
    object *op;

    /*LOG(llevDebug, "real_world_temperature: worldmaptoweathermap : %s\n",m->path);*/
    worldmap_to_weathermap(x, y, &wx, &wy, /*m->path*/m);
    temp = real_temperature(wx, wy);
    if (weathermap[wx][wy].avgelev < 0) {
        eleva = 0;
    } else {
        eleva = weathermap[x][y].avgelev;
    }

    op = GET_MAP_OB(m, x, y);
    if (!op) {
        return eleva;
    }

    elevb = op->elevation;
    if (elevb < 0) {
        elevb = 0;
    }
    if (elevb > eleva) {
        elevb -= eleva;
        temp -= elevb/1000;
    } else {
        elevb = eleva - elevb;
        temp += elevb/1000;
    }
    return temp;
}

/**
 * This code simply smooths the pressure map
 */
static void smooth_pressure(void) {
    int x, y;
    int k;

    for (k = 0; k < PRESSURE_ROUNDING_ITER; k++) {
        for (x = 2; x < WEATHERMAPTILESX-2; x++) {
            for (y = 2; y < WEATHERMAPTILESY-2; y++) {
                weathermap[x][y].pressure = (weathermap[x][y].pressure*
                    PRESSURE_ROUNDING_FACTOR+weathermap[x-1][y].pressure+
                    weathermap[x][y-1].pressure+weathermap[x-1][y-1].pressure+
                    weathermap[x+1][y].pressure+weathermap[x][y+1].pressure+
                    weathermap[x+1][y+1].pressure+weathermap[x+1][y-1].pressure+
                    weathermap[x-1][y+1].pressure)/(PRESSURE_ROUNDING_FACTOR+8);
            }
        }
        for (x = WEATHERMAPTILESX-2; x > 2; x--) {
            for (y = WEATHERMAPTILESY-2; y > 2; y--) {
                weathermap[x][y].pressure = (weathermap[x][y].pressure*
                    PRESSURE_ROUNDING_FACTOR+weathermap[x-1][y].pressure+
                    weathermap[x][y-1].pressure+weathermap[x-1][y-1].pressure+
                    weathermap[x+1][y].pressure+weathermap[x][y+1].pressure+
                    weathermap[x+1][y+1].pressure+weathermap[x+1][y-1].pressure+
                    weathermap[x-1][y+1].pressure)/(PRESSURE_ROUNDING_FACTOR+8);
            }
        }
    }

    for (x = 0; x < WEATHERMAPTILESX; x++)
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].pressure = MIN(weathermap[x][y].pressure, PRESSURE_MAX);
            weathermap[x][y].pressure = MAX(weathermap[x][y].pressure, PRESSURE_MIN);
        }

}

/**
 * Perform small randomizations in the pressure map.  Then, apply the
 * smoothing algorithim.. This causes the pressure to change very slowly
 */
static void perform_pressure(void) {
    int x, y, l, n, j, k;

    /* create random spikes in the pressure */
    for (l = 0; l < PRESSURE_SPIKES; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        n = rndm(600, 1300);
        weathermap[x][y].pressure = n;
        if (x > 5 && y > 5 && x < WEATHERMAPTILESX-5 && y < WEATHERMAPTILESY-5) {
            for (j = x-2; j < x+2; j++) {
                for (k = y-2; k < y+2; k++) {
                    weathermap[j][k].pressure = n;
                    /* occasionally add a storm */
                    if (rndm(1, 20) == 1) {
                        weathermap[j][k].humid = rndm(50, 80);
                    }
                }
            }
        }
    }

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].pressure += rndm(-1, 4);
        }
    }

    smooth_pressure();
}

/**
 * It doesn't really smooth it as such.  The main function of this is to
 * apply the pressuremap to the wind direction and speed.  Then, we run
 * a quick pass to update the windspeed.
 */
static void smooth_wind(void) {
    int x, y;
    int tx, ty, dx, dy;
    int minp;

    /* skip the outer squares.. it makes handling alot easier */
    dx = 0;
    dy = 0;
    for (x = 1; x < WEATHERMAPTILESX-1; x++)
        for (y = 1; y < WEATHERMAPTILESY-1; y++) {
            minp = PRESSURE_MAX + 1;
            for (tx = -1; tx < 2; tx++) {
                for (ty = -1; ty < 2; ty++) {
                    if (!(tx == 0 && ty == 0)) {
                        if (weathermap[x+tx][y+ty].pressure < minp) {
                            minp = weathermap[x+tx][y+ty].pressure;
                            dx = tx;
                            dy = ty;
                        }
                    }
                }
            }

            /* if the wind is strong, the pressure won't decay it completely */
            if (weathermap[x][y].windspeed > 5 && !similar_direction(weathermap[x][y].winddir, find_dir_2(dx, dy))) {
                weathermap[x][y].windspeed -= 2*2;
            } else {
                weathermap[x][y].winddir = find_dir_2(dx, dy);
                weathermap[x][y].windspeed = (weathermap[x][y].pressure-weathermap[x+dx][y+dy].pressure)*WIND_FACTOR;
            }
                /* Add in sea breezes. */
            weathermap[x][y].windspeed += weathermap[x][y].water/4;
            if (weathermap[x][y].windspeed < 0) {
                weathermap[x][y].windspeed = 0;
            }
        }

    /*  now, lets crank on the speed.  When surrounding tiles all have
        the same speed, inc ours.  If it's chaos. drop it.
     */
    for (x = 1; x < WEATHERMAPTILESX-1; x++) {
        for (y = 1; y < WEATHERMAPTILESY-1; y++) {
            minp = 0;
            for (tx = -1; tx < 2; tx++) {
                for (ty = -1; ty < 2; ty++) {
                    if (tx != 0 && ty != 0) {
                        if (similar_direction(weathermap[x][y].winddir, weathermap[x+tx][y+ty].winddir)) {
                            minp++;
                        }
                    }
                }
            }
            if (minp > 4) {
                weathermap[x][y].windspeed++;
            }
            if (minp > 6) {
                weathermap[x][y].windspeed += 2;
            }
            if (minp < 2) {
                weathermap[x][y].windspeed--;
            }
            if (weathermap[x][y].windspeed < 0) {
                weathermap[x][y].windspeed = 0;
            }
        }
    }
}

/**
 * Plot the gulfstream map over the wind map.  This is done after the wind,
 * to avoid the windsmoothing scrambling the jet stream.
 */
static void plot_gulfstream(void) {
    int x, y, tx;

    x = gulf_stream_start;

    if (gulf_stream_direction) {
        for (y = WEATHERMAPTILESY-1; y > 0; y--) {
            for (tx = 0; tx < GULF_STREAM_WIDTH && x+tx < WEATHERMAPTILESX; tx++) {
                if (similar_direction(weathermap[x+tx][y].winddir, gulf_stream_dir[tx][y]) && weathermap[x+tx][y].windspeed < GULF_STREAM_BASE_SPEED-5) {
                    weathermap[x+tx][y].windspeed += gulf_stream_speed[tx][y];
                } else{
                    weathermap[x+tx][y].windspeed = gulf_stream_speed[tx][y];
                }
                weathermap[x+tx][y].winddir = gulf_stream_dir[tx][y];
                if (tx == GULF_STREAM_WIDTH-1) {
                    switch (gulf_stream_dir[tx][y]) {
                    case 6: x--; break;
                    case 7: break;
                    case 8: x++; ; break;
                    }
                }
                if (x < 0) {
                    x++;
                }
                if (x >= WEATHERMAPTILESX-GULF_STREAM_WIDTH) {
                    x--;
                }
            }
        }
    } else {
        for (y = 0; y < WEATHERMAPTILESY-1; y++) {
            for (tx = 0; tx < GULF_STREAM_WIDTH && x+tx < WEATHERMAPTILESX; tx++) {
                if (similar_direction(weathermap[x+tx][y].winddir, gulf_stream_dir[tx][y]) && weathermap[x+tx][y].windspeed < GULF_STREAM_BASE_SPEED-5) {
                    weathermap[x+tx][y].windspeed += gulf_stream_speed[tx][y];
                } else {
                    weathermap[x+tx][y].windspeed = gulf_stream_speed[tx][y];
                }
                weathermap[x+tx][y].winddir = gulf_stream_dir[tx][y];
                if (tx == GULF_STREAM_WIDTH-1) {
                    switch (gulf_stream_dir[tx][y]) {
                    case 2: x++; break;
                    case 3: break;
                    case 4: x--; break;
                    }
                }
                if (x < 0) {
                    x++;
                }
                if (x >= WEATHERMAPTILESX-GULF_STREAM_WIDTH) {
                    x--;
                }
            }
        }
    }
    /* occasionally move the stream */
    if (rndm(1, 500) == 1) {
        gulf_stream_direction = rndm(0, 1);
        for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
            for (y = 0; y < WEATHERMAPTILESY-1; y++) {
                if (gulf_stream_direction) {
                    switch (gulf_stream_dir[tx][y]) {
                    case 2: gulf_stream_dir[tx][y] = 6; break;
                    case 3: gulf_stream_dir[tx][y] = 7; break;
                    case 4: gulf_stream_dir[tx][y] = 8; break;
                    }
                } else {
                    switch (gulf_stream_dir[tx][y]) {
                    case 6: gulf_stream_dir[tx][y] = 2; break;
                    case 7: gulf_stream_dir[tx][y] = 3; break;
                    case 8: gulf_stream_dir[tx][y] = 4; break;
                    }
                }
            }
        }
    }
    if (rndm(1, 25) == 1) {
        gulf_stream_start += rndm(-1, 1);
    }
    if (gulf_stream_start >= WEATHERMAPTILESX-GULF_STREAM_WIDTH) {
        gulf_stream_start--;
    }
    if (gulf_stream_start < 1) {
        gulf_stream_start++;
    }
}

/**
 * Let the madness, begin.
 *
 * This function is the one that ties everything together.  Here we loop
 * over all the weathermaps, and compare the various conditions we have
 * calculated up to now, to figure out what the sky conditions are for this
 * square.
 */
static void compute_sky(void) {
    int x, y;
    int temp;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            temp = real_temperature(x, y);
            if (weathermap[x][y].pressure < 980) {
                if (weathermap[x][y].humid < 20) {
                    weathermap[x][y].sky = SKY_LIGHTCLOUD;
                } else if (weathermap[x][y].humid < 30) {
                    weathermap[x][y].sky = SKY_OVERCAST;
                } else if (weathermap[x][y].humid < 40) {
                    weathermap[x][y].sky = SKY_LIGHT_RAIN;
                } else if (weathermap[x][y].humid < 55) {
                    weathermap[x][y].sky = SKY_RAIN;
                } else if (weathermap[x][y].humid < 70) {
                    weathermap[x][y].sky = SKY_HEAVY_RAIN;
                } else {
                    weathermap[x][y].sky = SKY_HURRICANE;
                }
                if (weathermap[x][y].sky < SKY_HURRICANE &&
                    weathermap[x][y].windspeed > 30) {
                    weathermap[x][y].sky++;
                }
                if (temp <= 0 && weathermap[x][y].sky > SKY_OVERCAST) {
                    weathermap[x][y].sky += 10; /*let it snow*/
                }
            } else if (weathermap[x][y].pressure < 1000) {
                if (weathermap[x][y].humid < 10) {
                    weathermap[x][y].sky = SKY_CLEAR;
                } else if (weathermap[x][y].humid < 25) {
                    weathermap[x][y].sky = SKY_LIGHTCLOUD;
                } else if (weathermap[x][y].humid < 45) {
                    weathermap[x][y].sky = SKY_OVERCAST;
                } else if (weathermap[x][y].humid < 60) {
                    weathermap[x][y].sky = SKY_LIGHT_RAIN;
                } else if (weathermap[x][y].humid < 75) {
                    weathermap[x][y].sky = SKY_RAIN;
                } else {
                    weathermap[x][y].sky = SKY_HEAVY_RAIN;
                }
                if (weathermap[x][y].sky < SKY_HURRICANE &&
                    weathermap[x][y].windspeed > 30) {
                    weathermap[x][y].sky++;
                }
                if (temp <= 0 && weathermap[x][y].sky > SKY_OVERCAST) {
                    weathermap[x][y].sky += 10; /*let it snow*/
                }
                if (temp > 0 && temp < 5 && weathermap[x][y].humid > 95 &&
                    weathermap[x][y].windspeed < 3) {
                    weathermap[x][y].sky = SKY_FOG; /* rare */
                }
                if (temp > 0 && temp < 5 && weathermap[x][y].humid > 70 &&
                    weathermap[x][y].windspeed > 35) {
                    weathermap[x][y].sky = SKY_HAIL; /* rare */
                }
            } else if (weathermap[x][y].pressure < 1020) {
                if (weathermap[x][y].humid < 20) {
                    weathermap[x][y].sky = SKY_CLEAR;
                } else if (weathermap[x][y].humid < 30) {
                    weathermap[x][y].sky = SKY_LIGHTCLOUD;
                } else if (weathermap[x][y].humid < 40) {
                    weathermap[x][y].sky = SKY_OVERCAST;
                } else if (weathermap[x][y].humid < 55) {
                    weathermap[x][y].sky = SKY_LIGHT_RAIN;
                } else if (weathermap[x][y].humid < 70) {
                    weathermap[x][y].sky = SKY_RAIN;
                } else {
                    weathermap[x][y].sky = SKY_HEAVY_RAIN;
                }
                if (weathermap[x][y].sky < SKY_HURRICANE &&
                    weathermap[x][y].windspeed > 30) {
                    weathermap[x][y].sky++;
                }
                if (temp <= 0 && weathermap[x][y].sky > SKY_OVERCAST) {
                    weathermap[x][y].sky += 10; /*let it snow*/
                }
            } else {
                if (weathermap[x][y].humid < 35) {
                    weathermap[x][y].sky = SKY_CLEAR;
                } else if (weathermap[x][y].humid < 55) {
                    weathermap[x][y].sky = SKY_LIGHTCLOUD;
                } else if (weathermap[x][y].humid < 70) {
                    weathermap[x][y].sky = SKY_OVERCAST;
                } else if (weathermap[x][y].humid < 85) {
                    weathermap[x][y].sky = SKY_LIGHT_RAIN;
                } else if (weathermap[x][y].humid < 95) {
                    weathermap[x][y].sky = SKY_RAIN;
                } else {
                    weathermap[x][y].sky = SKY_HEAVY_RAIN;
                }
                if (weathermap[x][y].sky < SKY_HURRICANE &&
                    weathermap[x][y].windspeed > 30) {
                    weathermap[x][y].sky++;
                }
                if (temp <= 0 && weathermap[x][y].sky > SKY_OVERCAST) {
                    weathermap[x][y].sky += 10; /*let it snow*/
                }
            }
        }
    }
}

/**
 * Keep track of how much rain has fallen in a given weathermap square.
 */
static void process_rain(void) {
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

/**
 * The world spinning drags the weather with it.
 * The equator is diagonal, and the poles are 45 degrees from north /south.
 * What the hell, lets spin the planet backwards.
 */
static void spin_globe(void) {
    int x, y;
    int buffer_humid;
    int buffer_sky;

    for (y = 0; y < WEATHERMAPTILESY; y++) {
        buffer_humid = weathermap[0][y].humid;
        buffer_sky = weathermap[0][y].sky;
        for (x = 0; x < (WEATHERMAPTILESX-1); x++) {
            weathermap[x][y].humid = weathermap[x+1][y].humid;
            weathermap[x][y].sky = weathermap[x+1][y].sky;
        }
        weathermap[WEATHERMAPTILESX-1][y].humid = buffer_humid;
        weathermap[WEATHERMAPTILESX-1][y].sky = buffer_sky;
    }
}

/**
 * Dump all the weather data as an image.
 * Writes two other files that are useful for creating animations and web pages.
 */
void write_weather_images(void) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;
    int32_t min[10], max[10], avgrain, avgwind, realmaxwind;
    double scale[10], realscalewind;
    uint8_t pixels[3*3*WEATHERMAPTILESX];
    int64_t total_rainfall = 0;
    int64_t total_wind = 0;

    min[0] = -100;         max[0] = 100;
    min[1] = 0;            max[1] = 0;
    min[2] = 0;            max[2] = 0;
    min[3] = PRESSURE_MIN; max[3] = PRESSURE_MAX;
    min[4] = 0;            max[4] = 0;
    min[5] = 1;            max[5] = 8;
    min[6] = 0;            max[6] = 100;
    min[7] = -45;          max[7] = 45;
    min[8] = 0;            max[8] = 16;
    min[9] = 0;            max[9] = 0;
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
/*          min[0] = MIN(min[0], weathermap[x][y].water); */
            min[1] = MIN(min[1], weathermap[x][y].avgelev);
            min[2] = MIN(min[2], weathermap[x][y].rainfall);
/*          min[3] = MIN(min[3], weathermap[x][y].pressure); */
            min[4] = MIN(min[4], weathermap[x][y].windspeed);
/*          min[5] = MIN(min[5], weathermap[x][y].winddir); */
/*          min[6] = MIN(min[6], weathermap[x][y].humid); */
/*          min[7] = MIN(min[7], real_temp[x][y]); */
/*          min[8] = MIN(min[8], weathermap[x][y].sky); */
/*          min[9] = MIN(min[9], weathermap[x][y].darkness); */

/*          max[0] = MAX(max[0], weathermap[x][y].water); */
            max[1] = MAX(max[1], weathermap[x][y].avgelev);
            max[2] = MAX(max[2], weathermap[x][y].rainfall);
/*          max[3] = MAX(max[3], weathermap[x][y].pressure); */
            max[4] = MAX(max[4], weathermap[x][y].windspeed);
/*          max[5] = MAX(max[5], weathermap[x][y].winddir); */
/*          max[6] = MAX(max[6], weathermap[x][y].humid); */
/*          max[7] = MAX(max[7], real_temp[x][y]); */
/*          max[8] = MAX(max[8], weathermap[x][y].sky); */
/*          max[9] = MAX(max[9], weathermap[x][y].darkness); */
            total_rainfall += weathermap[x][y].rainfall;
            total_wind += weathermap[x][y].windspeed;
        }
    }
    avgrain = total_rainfall/(WEATHERMAPTILESX*WEATHERMAPTILESY);
    avgwind = (total_wind   /((WEATHERMAPTILESX*WEATHERMAPTILESY)*3/2));
    max[2] = avgrain-1;
    realscalewind = 255.0l/(max[4]-min[4]);
    realmaxwind = max[4];
    max[4] = avgwind-1;
    for (x = 0; x < 10; x++) {
        scale[x] = 255.0l/(max[x]-min[x]);
    }

    LOG(llevDebug, "Writing weather conditions map.\n");

    snprintf(filename, sizeof(filename), "%s/weather.ppm", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    fprintf(fp, "P6\n%d %d\n", 3*WEATHERMAPTILESX, 3*WEATHERMAPTILESY);
    fprintf(fp, "255\n");
    // First row of maps
    for (y = 0; y < WEATHERMAPTILESY; y++) {
        memset(pixels, 0, 3 * 3 * WEATHERMAPTILESX);
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            // water map -- first map of row
            // blue = high water amount, black = low water amount, red = desert-like
            if (weathermap[x][y].water < 0)
                pixels[3*x+(0*WEATHERMAPTILESX*3+RED)] = (uint8_t)(255-(weathermap[x][y].water-min[0])*scale[0]*2);
            else
                pixels[3*x+(0*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((weathermap[x][y].water)*scale[0]*2);
            // elevation map -- second map of row.
            // green -- mostly land --> brighter green is higher elevation
            // blue -- mostly water --> deeper blue is lower elevation
            if (weathermap[x][y].avgelev >= 0) {
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)((weathermap[x][y].avgelev-min[1])*scale[1]);
            } else {
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((weathermap[x][y].avgelev-min[1])*scale[1]);
            }
            // rainfall map -- third map of row
            // magenta = high rainfall, blue = average rainfall, black = low rainfall
            if (weathermap[x][y].rainfall >= avgrain) { /* rainfall is rather spikey, this gives us more detail. */
                pixels[3*x+(2*WEATHERMAPTILESX*3+BLUE)] = 255;
                pixels[3*x+(2*WEATHERMAPTILESX*3+RED)] = (uint8_t)((weathermap[x][y].rainfall-avgrain)*(255.0/(max[2]-avgrain)));
            } else {
                pixels[3*x+(2*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((weathermap[x][y].rainfall-min[2])*(avgrain-min[2]));
            }
        }
        fwrite(pixels, sizeof(uint8_t), (3*3*WEATHERMAPTILESX), fp);
    }
    // Second row of maps.
    for (y = 0; y < WEATHERMAPTILESY; y++) {
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            uint32_t dir = directions[weathermap[x][y].winddir-1];
            uint32_t speed = weathermap[x][y].windspeed;
            uint8_t pressure = (weathermap[x][y].pressure-min[3])*scale[3];
            // Pressure -- first map of row
            // light = high pressure, dark = low pressure
            pixels[3*x+(0*WEATHERMAPTILESX*3+RED)] = pressure;
            pixels[3*x+(0*WEATHERMAPTILESX*3+GREEN)] = pressure;
            pixels[3*x+(0*WEATHERMAPTILESX*3+BLUE)] = pressure;
            // Wind speed -- second map of row
            // very high wind = red, else light = high wind, dark = low wind
            if (speed < avgwind) {
                speed = (speed-min[4])*scale[4];
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = speed;
            } else {
                speed = (speed-realmaxwind)*realscalewind;
/*              if (speed < 100) {
                    speed = 100;
                }*/
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = 0;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = 0;
            }
            // Wind direction -- third map of row
            // red = northeast, yellow = east, green = southeast, cyan = south,
            // blue = southwest, magenta = west, white = northwest, black = north
            pixels[3*x+(2*WEATHERMAPTILESX*3+RED)] = (uint8_t)((dir&0x00FF0000)>>16);
            pixels[3*x+(2*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)((dir&0x0000FF00)>>8);
            pixels[3*x+(2*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((dir&0x000000FF));
        }
        fwrite(pixels, sizeof(uint8_t), (3*3*WEATHERMAPTILESX), fp);
    }
    // Third row of maps
    for (y = 0; y < WEATHERMAPTILESY; y++) {
        memset(pixels, 0, 3 * 3 * WEATHERMAPTILESX);
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            uint32_t dir = skies[weathermap[x][y].sky];
            // Humidity -- first map of row.
            // blue = high humidity, black = low humidity, red = droughty (even lower humidity)
            // Didn't adjust min for this one, so it should look a little different than the others.
            if (weathermap[x][y].humid < 0)
                pixels[3*x+(0*WEATHERMAPTILESX*3+RED)] = (uint8_t)(255-(-weathermap[x][y].humid)*scale[6]);
            else
                pixels[3*x+(0*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((weathermap[x][y].humid-min[6])*scale[6]);
            // Real temperature -- second map of row
            // temp < 0 --> scale from white to blue
            // temp > 0 --> scale from blue to green to yellow to red
            //              green is 20 C
            //              yellow is 30 C
            int temp = real_temperature(x, y);
            // white -> cyan
            if (temp < 0) {
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = (uint8_t)(-temp * scale[7]*2);
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)0xFF;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)0xFF;
            }
            // cyan->blue is the boundary for above/below freezing
            // blue -> green
            else if (temp < 20) {
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = 0;
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)(temp * scale[7]*9/2);
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((20-temp) * scale[7]*9/2);
            }
            // green -> yellow
            else if (temp < 30) {
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = (uint8_t)((temp-20) * scale[7]*9);
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = 255;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = 0;
            }
            // yellow -> red
            else {
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = 255;
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)((45-temp) * scale[7]*6);
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = 0;
            }
            // current weather -- third map of row
            // blue = clear, medium blue = light clouds, dark blue = overcast, green = light rain
            // medium green = rain, dark green = heavy rain, yellow = hurricane, red = fog, magenta = hail,
            // dark gray = light snow, medium gray = snow, gray = heavy snow, white = blizzard
            pixels[3*x+(2*WEATHERMAPTILESX*3+RED)] = (uint8_t)((dir&0x00FF0000)>>16);
            pixels[3*x+(2*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)((dir&0x0000FF00)>>8);
            pixels[3*x+(2*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((dir&0x000000FF));
        }
        fwrite(pixels, sizeof(uint8_t), (3*3*WEATHERMAPTILESX), fp);
    }
    fclose(fp);

    snprintf(filename, sizeof(filename), "%s/todtick", settings.localdir);
    if ((fp = fopen(filename, "w")) == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return;
    }
    fprintf(fp, "%lu", todtick);
    fclose(fp);
}
