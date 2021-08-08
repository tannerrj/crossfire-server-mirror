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
