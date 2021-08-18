/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 1999-2021 The Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

/**
 * @defgroup module_weather Weather module
 * Adds weather effects to the world map.
 *
 * @{
 */

#include "global.h"
#include "map.h"
#include "object.h"
#include "output_file.h"

#include <string.h>
#include <assert.h>

extern unsigned long todtick;
extern weathermap_t **weathermap;

/**
 * Structure to hold forestry data entries.
 */
struct forestry {
    // Use shared strings so we can do pointer comparisons.
    sstring name;
    // 0 if name is the arch name, 1 if it is the object name.
    int is_obj;
    // The tree density the tile type counts for.
    int num_trees;
    // Pointer to the next item in the list
    // We're scanning all of these when we check anyway,
    // so might as well use a structure that works fine that way.
    struct forestry *next;
};

struct forestry *forest_list = NULL;

/**
 * Global event handling for weather.
 * @param type
 * The event type.
 * @param m
 * The map being loaded.
 * @return
 * 0.
 */
static int weather_listener(int *type, ...) {
    va_list args;
    int code;
    mapstruct *m;

    va_start(args, type);
    code = va_arg(args, int);
    // At this point, we don't need the entering object for this.
    // but it is passed as an arg, so just skip it.
    va_arg(args, object *);
    m = va_arg(args, mapstruct *);

    switch (code) {
        case EVENT_MAPENTER:
            if (m->outdoor)
                do_map_precipitation(m);
            break;
    }

    va_end(args);

    return 0;
}


/********************************************************************************************
 * Section -- weather data helpers
 * These functions do important things like convert weathermap location to worldmap location.
 * They are used by multiple sections, and as a result are general helpers.
 ********************************************************************************************/

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
 *
 * @return
 * buffer containing the path to the world map we want.
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
 * Calculates the distance to the nearest pole.
 * @param x
 * @param y
 * weathermap coordinates.
 * @param equator
 * current location of the equator.
 *
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

/********************************************************************************************
 * Section END -- weather data helpers
 ********************************************************************************************/

/********************************************************************************************
 * Section -- weather data calculators
 * These functions determine the progression of weather data over time.
 * This is the bread-and-butter of the weather system.
 ********************************************************************************************/

// We need to declare init_temperature, since it is defined below this area.
static void init_temperature();

/**
 * It doesn't really smooth it as such.  The main function of this is to
 * apply the pressuremap to the wind direction and speed.  Then, we run
 * a quick pass to update the windspeed.
 */
static void smooth_wind() {
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
 * Perform small randomizations in the pressure map.  Then, apply the
 * smoothing algorithim.. This causes the pressure to change very slowly
 */
static void perform_pressure() {
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
        // FIXME: elev will always be at least 10000 here.
        // That seems rather dubious to me.
        elev = MAX(10000, weathermap[x][y].avgelev)/1000;
    }
    weathermap[x][y].temp -= elev;
}

/**
 * The world spinning drags the weather with it.
 * The equator is diagonal, and the poles are 45 degrees from north /south.
 * What the hell, lets spin the planet backwards.
 *
 * @todo
 * Make the wraparound make more sense for the polar layout.
 * Current implementation does naive tiling, which will roll the equator onto the poles and vice versa.
 */
static void spin_globe() {
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
 * Calculate the humidity of the given weather tile.
 *
 * @param x
 * @param y
 * weathermap coordinates we wish to calculate humidity for.
 *
 * @return
 * the humidity of the weathermap square, trimmed to the range [0, 100]
 */
static int humid_tile(int x, int y) {
    // ox and oy denote the neighbor that is influencing us (due to winds from there)
    int ox = x, oy = y, humid;

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
    // This is where the magic happens
    // If humidity is unstable over time, this is what will need to be tweaked
    // (or one of the values it depends on, if not this)
    humid = (weathermap[x][y].humid*2 +
        weathermap[ox][oy].humid*weathermap[ox][oy].windspeed +
        weathermap[x][y].water +
        weathermap[x][y].forestry/10 + rndm(0, 10))/
        (weathermap[ox][oy].windspeed+3)+rndm(-3, 3);
    if (humid < 0) {
        humid = 0;
    }
    if (humid > 100) {
        humid = 100;
    }
    return humid;
}

/**
 * Update the humidity for all weathermap tiles.
 * Calls humid_tile for every weathermap tile.
 * All the fun stuff happens in humid_tile
 */
static void update_humid() {
    int x, y;

    for (y = 0; y < WEATHERMAPTILESY; y++) {
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            weathermap[x][y].humid = humid_tile(x, y);
        }
    }
}

void tick_weather() {
    assert(settings.dynamiclevel > 0);
    perform_pressure();     /* pressure is the random factor */
    smooth_wind();          /* calculate the wind. depends on pressure */
    plot_gulfstream();
    update_humid();
    init_temperature();
    spin_globe();
    //compute_sky(); This is done in perform_weather
}

/********************************************************************************************
 * Section END -- weather data calculators
 ********************************************************************************************/

/********************************************************************************************
 * Section -- Initializations
 * Functions to load in config for determining certain weathermap attributes,
 * functions to initialize missing weathermap attributes,
 * and their helper functions.
 ********************************************************************************************/

/**
 * Read the config file that tells how many trees a given
 * tree arch is worth during calculations.
 * By defining in a file, we get our structure to be non-static,
 * so we can do pointer comparisons on the
 * object name or arch rather than string comparisons on the name.
 *
 * @param settings
 * Pointer to the settings structure, so we can get the directory where the config
 * is stored.
 *
 * @return
 * 0 if successful (even if lines are malformed in the file), 1 otherwise
 */
static int init_forestry_vals(const Settings *settings) {
    char filename[MAX_BUF], *line, *name;
    BufferReader *bfr;
    FILE *fp;
    int found, is_obj_name, tree_count;

    snprintf(filename, sizeof(filename), "%s/treedefs", settings->confdir);
    // Open the file, then pass it off to the buffer reader.
    fp = fopen(filename, "r");
    if (fp != NULL) {
        bfr = bufferreader_create();
        bufferreader_init_from_file(bfr, fp);
        fclose(fp);
        // Now we read in from the buffer.
        while ((line = bufferreader_next_line(bfr)) != NULL) {
            // Now we parse the line
            // Start by examining the first character.
            switch (*line) {
                // Ignore empty lines and comment lines (denoted by # at front)
                case '\0':
                case '#':
                // Handling \r means Windows should work right, too.
                case '\r':
                case '\n':
                    break;
                default:
                    // Actually parse the line
                    // Format is like this:
                    // name, (0 if arch, 1 if object name), # trees
                    // [spaces are expected after commas]
                    found = sscanf(line, "%s, %d, %d", name, &is_obj_name, &tree_count);
                    if (found != 3) {
                        // Print an error for the malformed line
                        LOG(llevError, "init_forestry_vals: Malformed forestry entry in %s, line %d:\n%s\n",
                            filename, bufferreader_current_line(bfr), line);
                    }
                    else {
                        // Add a struct to the list.
                        struct forestry *frst = (struct forestry *)malloc(sizeof(struct forestry));
                        if (!frst) {
                            fatal(OUT_OF_MEMORY);
                        }
                        // Shared strings are friend, not food
                        frst->name = add_string(name);
                        frst->is_obj = is_obj_name;
                        frst->num_trees = tree_count;
                        // Attach to front of list, since order doesn't matter much, if at all.
                        frst->next = forest_list;
                        forest_list = frst;
                    }
            }
        }
        bufferreader_destroy(bfr);
        return 0;
    }
    LOG(llevError, "init_forestry_vals: Could not open file %s. No forestry data is defined.\n", filename);
    return 1;
}

/**
 * Method to abstract some of the mess of the humidity map.
 * Loads a specific corner of the map based on dir.
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
 * @param trees
 * Pointer to a forestry sum variable.
 * Will be updated by this function.
 *
 * @return
 * 0 if successful, -1 if failure
 */
static int do_water_elev_calc(mapstruct *m, int x, int y, int *water, int64_t *elev, int *trees) {
    if (!m || !water || !elev || !trees)
        return -1;
    object *ob = GET_MAP_OB(m, x, y), *obtmp;
    if (ob) {
        struct forestry *tmp;
        if (QUERY_FLAG(ob, FLAG_IS_WATER)) {
            (*water)++;
        }
        // Deserts will reduce the humidity/precipitation in the spaces they exist in.
        if (strcmp(ob->name, "desert") == 0) {
            (*water)--;
        }

        // Handle forestry
        obtmp = ob;
        // Our trees are not always the floor. Look higher if need be.
        while (obtmp) {
            // Look at our forestry config data for tree amounts.
            tmp = forest_list;
            while (tmp) {
                // Does object name match?
                if ((tmp->is_obj && tmp->name == obtmp->name) ||
                    // Does arch name match?
                    (!tmp->is_obj && tmp->name == obtmp->arch->name)) {
                        *trees += tmp->num_trees;
                        break;
                }

                tmp = tmp->next;
            }
            // If we found a tree match and broke out, break out here, too.
            if (tmp)
                break;
            obtmp = obtmp->above;
        }

        (*elev) += ob->elevation;
    }
    return 0;
}

/**
 * Initialize humidity, water, forestry, and elevation.
 *
 * This is a fairly expensive operation, so we only do this if
 * the elevation map is not already existant.
 *
 * @param settings
 * Pointer to the settings structure.
 * We use multiple values from it.
 */
static void init_humid_elev(const Settings *settings) {
    int x, y, tx, ty, nx, ny, ax, ay, j;
    int spwtx, spwty;
    int64_t elev;
    int water, space, trees;
    mapstruct *m;

    /* handling of this is kinda nasty.  For that reason,
     * we do the elevation here too.  Not because it makes the
     * code cleaner, or makes handling easier, but because I do *not*
     * want to maintain two of these nightmares.
     */

    spwtx = (settings->worldmaptilesx*settings->worldmaptilesizex)/WEATHERMAPTILESX;
    spwty = (settings->worldmaptilesy*settings->worldmaptilesizey)/WEATHERMAPTILESY;
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            water = space = trees = 0;
            elev = 0;
            nx = ny = 0;

            /* top left */
            if (load_humidity_map_part(&m, 8, x, y, &tx, &ty) == -1)
                continue;

            for (nx = 0, ax = tx; nx < spwtx && ax < settings->worldmaptilesizex && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = ty; ny < spwty && ay < settings->worldmaptilesizey && space < spwtx*spwty; ay++, ny++, space++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                }
            }
            delete_map(m);

            /* bottom left */
            if (load_humidity_map_part(&m, 6, x, y, &tx, &ty) == -1)
                continue;

            j = ny;
            for (nx = 0, ax = tx; nx < spwtx && ax < settings->worldmaptilesizex && space < spwtx*spwty; ax++, nx++) {
                for (ny = j, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                }
            }
            delete_map(m);

            /* top right */
            if (load_humidity_map_part(&m, 2, x, y, &tx, &ty) == -1)
                continue;

            for (ax = MAX(0, tx-(spwtx-1)); nx < spwtx && ax < tx && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = ty; ny < spwty && ay < settings->worldmaptilesizey && space < spwtx*spwty; ay++, ny++, space++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                }
            }
            delete_map(m);

            /* bottom left */
            if (load_humidity_map_part(&m, 4, x, y, &tx, &ty) == -1)
                continue;

            for (nx = 0, ax = MAX(0, tx - (spwtx-1)); nx < spwtx && ax < tx && space < spwtx*spwty; ax++, nx++) {
                for (ny = 0, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                    do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                }
            }
            delete_map(m);

            /* jesus thats confusing as all hell */
            // Per meteorology, full ocean usually only gets to 80% humidity at the standard height it is measured.
            // This should help prevent a forever-hurricane over the ocean.
            weathermap[x][y].humid = water*80/(spwtx*spwty);
            weathermap[x][y].avgelev = elev/(spwtx*spwty);
            weathermap[x][y].water = water*100/(spwtx*spwty);
            // Cap at 100 for tree values. Denser trees stop having any effect.
            weathermap[x][y].forestry = MIN(100, trees*100/(spwtx*spwty));
        }
    }

    /* and this does all the real work */
    update_humid();
}

/**
 * Initialize the temperature based on the time.
 * This is actually used at every recalculation step, too.
 * Calls temperature_calc to do the heavy lifting.
 */
static void init_temperature() {
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
 * Initialize rainfall.
 * Appears to just give a very rough estimate of rainfall amounts.
 * Could probably be better, but should be fine despite this.
 */
static void init_rainfall()
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

/********************************************************************************************
 * Section END -- initializations
 ********************************************************************************************/

/********************************************************************************************
 * Section -- weather data writers
 * These functions write the current state of the weather to file,
 * allowing persistence across server runs.
 ********************************************************************************************/

/**
 * Write the forestry map to the localdir.
 * Since this doesn't change over time, we can make this static and only call it from
 * humidity initialization.
 *
 * @param settings
 * Pointer to the settings structure.
 * We want the localdir from this.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int write_forestrymap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    // First, allocate our file.
    snprintf(filename, sizeof(filename), "%s/treemap", settings->localdir);
    // We use the output_file handling for atomic file operations.
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Failed to open %s for writing.\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing forestry map to file.\n");
    // Actually write the forestry amounts to the file
    for (x = 0; x < WEATHERMAPTILESX; ++x) {
        for (y = 0; y < WEATHERMAPTILESY; ++y) {
            fprintf(fp, "%d ", weathermap[x][y].forestry);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save humidity information to localdir.
 *
 * @param settings
 * The settings structure to use for finding localdir
 *
 * @return
 * 0 if successful, 1 if failure
 */
int write_humidmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/humidmap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing humidity map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].humid);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save the average elevation information to localdir.
 * Since this does not change over time, we call this only
 * when we are initalizing humidity.
 *
 * @param settings
 * Pointer to the settings structure.
 * In particular, we want localdir.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int write_elevmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/elevmap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing elevation map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].avgelev);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}


/**
 * Save water percent information to localdir.
 * Since this does not actually change regularly, we can
 * just call this when we initialize humidity.
 *
 * @param settings
 * Pointer to the settings structure.
 * In particular, we want localdir from it.
 *
 * @return 0 if successful, 1 if failure.
 */
static int write_watermap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/watermap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing water map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].water);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save temperature information to localdir.
 *
 * @param settings
 * The settings structure we wish to pull localdir from.
 * It's probably the only one, but oh well. I'm passing it
 * as a function parameter anyway.
 *
 * @return
 * 0 on success, 1 on failure.
 */
int write_temperaturemap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/temperaturemap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing temperature map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].temp);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save rainfall information to localdir.
 *
 * @param settings
 * The current server settings
 * In particular, we want localdir from this
 *
 * @return
 * 0 if success, 1 if failure.
 */
int write_rainfallmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/rainfallmap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing rainfall map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%u ", weathermap[x][y].rainfall);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/********************************************************************************************
 * Section END -- weather data writers
 ********************************************************************************************/

/********************************************************************************************
 * Section -- weather data readers
 * These read weather data that gets periodically stored to file to restore
 * the prior state on a server restart. They also handle first-load initialization.
 ********************************************************************************************/

/**
 * Read the forestry map from the localdir.
 * The forestry map contains the measure of how forested a
 * given weather map tile is. The values of each tree tile
 * are defined in the forestry data (and initialized in
 * init_forestry_vals(), and the values read here are calculated
 * at the same time as humidity, water, and elevation.
 * Here we merely read the resultant values in.
 *
 * @param settings
 * Pointer to the Settings structure that designates
 * where different install directories are located.
 * In this case, we care about localdir.
 *
 * @return
 * 0 if successful, 1 if failed.
 */
static int read_forestrymap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    BufferReader *bfr;
    FILE *fp;
    int trees, x, y, res;

    snprintf(filename, sizeof(filename), "%s/treemap", settings->localdir);
    LOG(llevDebug, "Reading forestry data from %s...\n", filename);
    fp = fopen(filename, "r");
    if (fp != NULL) {
        // Set up the bufferreader and read in the file.
        // We do it through the bufferreader so that we only dip into I/O once,
        // and the rest is just parsing it in memory.
        bfr = bufferreader_create();
        bufferreader_init_from_file(bfr, fp);
        fclose(fp);
        // Parse the file. Since this is auto-generated by the weather system,
        // just bail if the file is malformed.
        data = bufferreader_data(bfr);
        for (x = 0; x < WEATHERMAPTILESX; ++x) {
            for (y = 0; y < WEATHERMAPTILESY; ++y) {
                res = sscanf(data, "%d ", &trees);
                if (res != 1) {
                    LOG(llevError, "Forestry data is corrupted and should be regenerated.\n"
                        "Please delete %s/humidmap and restart the server at your earliest convenience to regenerate the forestry map.\n", settings->localdir);
                    bufferreader_destroy(bfr);
                    return 1;
                }
                // Limit the range from 0 to 100
                weathermap[x][y].forestry = MIN(100, MAX(0, trees));
                // Now we move where we're looking, since we want to read more than just the first entry.
                // Use strpbrk so that we can handle newlines more cleanly.
                tmp = strpbrk(data, " \n");
                if (tmp != NULL)
                    data = tmp + 1;
                else {
                    LOG(llevError, "Unexpected end of forestry file. Forestry file may need to be regenerated.\n"
                        "Please delete %s/humidmap and restart the server at your earliest convenience to regenerate the forestry map.\n", settings->localdir);
                    bufferreader_destroy(bfr);
                    return 1;
                }
            }
            // Due to the way the file is written, the end of the line should have a space and a newline.
            // Handle the newline if it is the front of the string now.
            if (*data == '\n')
                ++data;
        }
        bufferreader_destroy(bfr);
        return 0;
    }
    // Otherwise, we could not load.
    // Since this should be calculated and stored when humidity is calculated,
    // and stored by the time we reach here, just fail if it does not exist.
    LOG(llevError, "Cannot open %s for reading.\n", filename);
    return 1;
}

/**
 * Attempt to read humidity information, or barring that, read the maps
 * and initialize elevation, humidity, water, and forestry maps.
 *
 * Must be called before attempting to read elevation, humidity, water, or forestry,
 * as those all do not attempt initialization if they cannot find their file to read from.
 *
 * @param settings
 * The settings structure. We use this for localdir and to pass to initialization if needed.
 *
 * @return
 * 0 if successful and no initialization done, 1 if successful and initialization done.
 * Returns -1 on failure.
 */
static int read_humidmap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, hmd, res;

    snprintf(filename, sizeof(filename), "%s/humidmap", settings->localdir);
    LOG(llevDebug, "Reading humidity data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing humidity and elevation maps...\n");
        init_humid_elev(settings);
        write_elevmap(settings);
        write_humidmap(settings);
        write_watermap(settings);
        write_forestrymap(settings);
        LOG(llevDebug, "Done\n");
        return 1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &hmd);
            if (res != 1) {
                LOG(llevError, "Humidity data is corrupted and cannot be loaded.\n"
                    "Please delete %s and restart the server to regenerate humidity data.\n", filename);
                bufferreader_destroy(bfr);
                return -1;
            }
            // Limit range from 0 to 100. Clip entries that surpass these bounds.
            weathermap[x][y].humid = MAX(0, MIN(100, hmd));
            // Move to the next spot in the buffer.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of humidity file.\n"
                    "Please delete %s and restart the server to regenerate humidity data.\n", filename);
                bufferreader_destroy(bfr);
                return -1;
            }
            // If found, move data to the next character after the space/newline found.
            data = tmp + 1;
        }
        // If the newline from the previous line hasn't been parsed yet, skip it.
        if (*data == '\n')
            ++data;
    }
    bufferreader_destroy(bfr);
    LOG(llevDebug, "Done.\n");
    return 0;
}

/**
 * Load the elevation information.
 * Does not attempt to initalize elevation, since humidity should
 * have tried to do that already.
 *
 * @param settings
 * The settings information structure we wish to use.
 * Specifically, we grab localdir from it
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int read_elevmap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, elev, res;

    snprintf(filename, sizeof(filename), "%s/elevmap", settings->localdir);
    LOG(llevDebug, "Reading elevation data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        /* initializing these is expensive, and should have been done
           by the humidity.  It's not worth the wait to do it twice. */
        return 1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &elev);
            if (res != 1) {
                LOG(llevError, "Elevation data is corrupted and cannot be loaded.\n"
                    "Please delete %s/humidmap and restart your server to regenerate elevation data.\n", settings->localdir);
                bufferreader_destroy(bfr);
                return 1;
            }
            // Individual elevation values should be in the range [-32000, 32000]
            // Averages can be capped to these as well.
            weathermap[x][y].avgelev = MAX(-32000, MIN(32000, elev));
            // Now we move to the next entry in the buffer.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in elevation data.\n"
                    "Please delete %s/humidmap and restart your server to regenerate elevation data.\n", settings->localdir);
                bufferreader_destroy(bfr);
                return 1;
            }
            // If found, we move to the character after the space/newline.
            data = tmp + 1;
        }
        // If there's still a newline after passing a space, then skip past it.
        if (*data == '\n')
            ++data;
    }
    bufferreader_destroy(bfr);
    LOG(llevDebug, "Done.\n");
    return 0;
}

/**
 * Load water information from localdir.
 * Since This requires us to look at all the map pieces, it should be
 * initialized by the humidity code before we reach here.
 *
 * @param settings
 * The settings structure. We specifically want localdir.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int read_watermap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, wtr, res;

    snprintf(filename, sizeof(filename), "%s/watermap", settings->localdir);
    LOG(llevDebug, "Reading water data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        /* initializing these is expensive, and should have been done
           by the humidity.  It's not worth the wait to do it twice. */
        return 1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &wtr);
            if (res != 1) {
                LOG(llevError, "Water map is corrupted and cannot be loaded.\n"
                    "Please delete %s/humidmap and restart your server to regenerate the water map.\n", settings->localdir);
                bufferreader_destroy(bfr);
                return 1;
            }
            // Range is -100 to 100 due to deserts and such being negative.
            weathermap[x][y].water = MAX(-100, MIN(100, wtr));
            // Adjust the data pointer.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in water map.\n"
                    "Please delete %s/humidmap and restart your server to regenerate the water map.\n", settings->localdir);
                bufferreader_destroy(bfr);
                return 1;
            }
            // Okay, so we want the first character after the space/newline.
            data = tmp + 1;
        }
        // Make sure we don't leave a newline in the event of a trailing space on a given line.
        if (*data == '\n')
            ++data;
    }
    bufferreader_destroy(bfr);
    LOG(llevDebug, "Done.\n");
    return 0;
}

/**
 * Load or initialize temperature information.
 * Depends on water info, so must be initialized after humidity.
 *
 * @param settings
 * The settings structure we're using.
 *
 * @return
 * 0 on success without initialization, 1 on success with initialization.
 * Returns -1 on failure.
 *
 * @todo
 * Refactor to use a bufferreader.
 */
static int read_temperaturemap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/temperaturemap", settings->localdir);
    LOG(llevDebug, "Reading temperature data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing temperature map.\n");
        init_temperature();
        // If writing was successful, then consider this a success.
        if (write_temperaturemap(settings) == 0)
            return 1;
        return -1;
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
    return 0;
}

/**
 * Read or initialize rainfall information.
 * Initialization depends on humidity, so it must
 * be called after humidity is initialized.
 *
 * @param settings
 * The settings structure we use to locate config folders.
 * Here we want localdir specifically.
 *
 * @return
 * 0 if success without initialization, 1 if success with initialization.
 * Returns -1 on failure.
 *
 * @todo
 * Refactor to use a bufferreader
 */
static int read_rainfallmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/rainfallmap", settings->localdir);
    LOG(llevDebug, "Reading rainfall data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing rainfall map...\n");
        init_rainfall();
        write_rainfallmap(settings);
        return 1;
    }
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            (void)fscanf(fp, "%u ", &weathermap[x][y].rainfall);
        }
        (void)fscanf(fp, "\n");
    }
    LOG(llevDebug, "Done.\n");
    fclose(fp);
    return 0;
}

/********************************************************************************************
 * Section END -- weather data readers
 ********************************************************************************************/

static event_registration global_map_handler, global_clock_handler;

/**
 * Weather module initialisation.
 * @param settings server settings.
 */
void cfweather_init(Settings *settings) {
    // Initialize the forestry information from file.
    init_forestry_vals(settings);
    // Trees help stabilize local temperature and evaporate water from deeper underground.
    // This is calculated at the same time as elevation and humidity.
    int result = read_humidmap(settings);
    // If result is 1, then we initialized all these.
    // When that is the case, we don't need to read in from the file.
    // If result is -1, everything's jacked up anyway, so still don't load.
    if (result == 0) {
       read_watermap(settings); /* On first run, we want to do this after humidity. Otherwise, it doesn't seem to matter */
       read_elevmap(settings); /* elevation must allways follow humidity */
       read_forestrymap(settings);
    }
    read_temperaturemap(settings);
    read_rainfallmap(settings);

    LOG(llevDebug, "Done reading weathermaps\n");
    // Initialize the sky so we can get accurate precipitation at initial load.
    compute_sky();

    // Connect the events after initialization, since we don't need to do
    // precipitation when we're initializing.
    global_map_handler = events_register_global_handler(EVENT_MAPENTER, weather_listener);
    /* Disable the plugin in case it's still there */
    linked_char *disable = (linked_char *)calloc(1, sizeof(linked_char));
    disable->next = settings->disabled_plugins;
    disable->name = strdup("cfweather");
    settings->disabled_plugins = disable;
}

void cfweather_close() {
    struct forestry *cur;
    events_unregister_global_handler(EVENT_MAPENTER, global_map_handler);
    // Deallocate our linked list of forest entries.
    while (forest_list != NULL) {
        cur = forest_list;
        forest_list = forest_list->next;
        free_string(cur->name);
        free(cur);
    }
}


/*@}*/
