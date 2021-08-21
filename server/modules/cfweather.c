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
#include "sproto.h"

#include <string.h>
#include <assert.h>
#include <math.h>

extern unsigned long todtick;
extern weathermap_t **weathermap;

/**
 * Structure to hold density data entries.
 * It is used for both forestry data and for
 * desert data, and is named generically so
 * it makes sense for both.
 */
typedef struct density {
    // Use shared strings so we can do pointer comparisons.
    sstring name;
    // 0 if name is the arch name, 1 if it is the object name.
    int is_obj;
    // The density the tile type counts for.
    int value_density;
    // Pointer to the next item in the list
    // We're scanning all of these when we check anyway,
    // so might as well use a structure that works fine that way.
    struct density *next;
} DensityConfig;

DensityConfig *forest_list = NULL;
DensityConfig *water_list = NULL;

/**
 * Gulf stream variables.
 *
 * @todo
 * These should probably be in a structure instead of loose variables.
 */
/** Speed of the gulf stream. */
static int gulf_stream_speed[GULF_STREAM_WIDTH][WEATHERMAPTILESY];
/** Direction of the gulf stream. */
static int gulf_stream_dir[GULF_STREAM_WIDTH][WEATHERMAPTILESY];
static int gulf_stream_start;
static int gulf_stream_direction;


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

/**
 * Get config tile retrieves the desired tile's associated value from a given space.
 * This can be used for trees, deserts, water, anything of the sort.
 *
 * @param x
 * @param y
 * The coordinates on the map we wish to check
 *
 * @param m
 * The map the coordinates belong to.
 *
 * @param list
 * The list we are pulling from.
 * This allows us to use the same code for denoting water
 * as for denoting trees.
 *
 * @return
 * The associated value of the bottommost config-specified tile at this location,
 * or 0 if there are no matches here.
 */
static int get_config_tile(const int x, const int y, const mapstruct *m, DensityConfig *list) {
    // If no list specified, shortcut the exit.
    if (list == NULL)
        return 0;
    object *ob = GET_MAP_OB(m, x, y);
    DensityConfig *tmp;
    // Our trees are not always the floor. Look higher if need be.
    // Even for types that are floor, check anyway. This ensures
    // that no-magic tiles hiding underneath floor don't cause problems.
    while (ob) {
        // Look at our config data for the associated amounts.
        tmp = list;
        while (tmp) {
            // Does object name match?
            if ((tmp->is_obj && tmp->name == ob->name) ||
                // Does arch name match?
                (!tmp->is_obj && tmp->name == ob->arch->name)) {
                    return tmp->value_density;
                }

            tmp = tmp->next;
        }
        ob = ob->above;
    }
    // If we get here, there were no matches.
    return 0;
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
 * apply the pressuremap to the wind direction and speed, followed by some
 * tree-driven disruption.  Then, we run a quick pass to update the windspeed.
 */
static void smooth_wind() {
    int x, y;
    int tx, ty, dx, dy;
    int minp;

    // Grab the old wind speed.
    // Moving air is lower pressure than stationary air.
    int oldwind = weathermap[x][y].windspeed;

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
            // Disrupt the wind where trees are present (a reduction of up to 5 is possible).
            weathermap[x][y].windspeed -= weathermap[x][y].forestry/20;
            if (weathermap[x][y].windspeed < 0) {
                weathermap[x][y].windspeed = 0;
            }
            // The wind moves some of the higher pressure to the lower pressure.
            weathermap[x][y].pressure -= weathermap[x][y].windspeed/8;
            weathermap[x+dx][y+dy].pressure += weathermap[x][y].windspeed/8;
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

    // Apply a difference to the pressure equal to our change in wind.
    // Higher wind speed is lower pressure.
    weathermap[x][y].pressure -= weathermap[x][y].windspeed - oldwind;
}

/**
 * This code simply smooths the pressure map.
 * It also does clipping to ensure we are within acceptable bounds.
 */
static void smooth_pressure() {
    int x, y;
    int k;

    for (k = 0; k < PRESSURE_ROUNDING_ITER; k++) {
        for (x = 1; x < WEATHERMAPTILESX-1; x++) {
            for (y = 1; y < WEATHERMAPTILESY-1; y++) {
                weathermap[x][y].pressure = (weathermap[x][y].pressure*
                    PRESSURE_ROUNDING_FACTOR+weathermap[x-1][y].pressure+
                    weathermap[x][y-1].pressure+weathermap[x-1][y-1].pressure+
                    weathermap[x+1][y].pressure+weathermap[x][y+1].pressure+
                    weathermap[x+1][y+1].pressure+weathermap[x+1][y-1].pressure+
                    weathermap[x-1][y+1].pressure)/(PRESSURE_ROUNDING_FACTOR+8);
            }
        }
        for (x = WEATHERMAPTILESX-2; x > 0; x--) {
            for (y = WEATHERMAPTILESY-2; y > 0; y--) {
                weathermap[x][y].pressure = (weathermap[x][y].pressure*
                    PRESSURE_ROUNDING_FACTOR+weathermap[x-1][y].pressure+
                    weathermap[x][y-1].pressure+weathermap[x-1][y-1].pressure+
                    weathermap[x+1][y].pressure+weathermap[x][y+1].pressure+
                    weathermap[x+1][y+1].pressure+weathermap[x+1][y-1].pressure+
                    weathermap[x-1][y+1].pressure)/(PRESSURE_ROUNDING_FACTOR+8);
            }
        }
    }

    // Clip to our valid pressure range
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
static void perform_pressure() {
    int x, y, l, n, j, k, is_storm;

    /* create random spikes in the pressure */
    for (l = 0; l < PRESSURE_SPIKES; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        // This goes beyond the valid bounds so that the smoothing proces ends up
        // making different-sized pressure spikes.
        n = rndm(600, 1300);
        weathermap[x][y].pressure = n;
        // Get close to the edge. But, to make things cleaner, don't go off the edge.
        if (x > 3 && y > 3 && x < WEATHERMAPTILESX-3 && y < WEATHERMAPTILESY-3) {
            /* occasionally add a storm
             * and make sure the whole pressure spot is a storm, not just pieces of it
             *
             * Also, only try to make storms out of low pressure spikes. 1013 mbar
             * Is standard pressure at sea level.
             */
            is_storm = (n < 1013 && rndm(1, 10) == 1);
            for (j = x-2; j < x+2; j++) {
                for (k = y-2; k < y+2; k++) {
                    weathermap[j][k].pressure = n;
                    if (is_storm) {
                        weathermap[j][k].humid = rndm(50, 90);
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
 * How to alter the temperature, based on the hour of the day.
 * This is used exclusively by real_temperature.
 *
 * @todo
 * Make different seasons affect temperature differently (becuase light/darkness)
 */
static const int season_tempchange[HOURS_PER_DAY] = {
/*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  1  2  3  4  5  6  7  8  9 10 11 12 13 */
    0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

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
 *
 * @param tod
 * Pointer to an already-filled-out time-of-day structure.
 *
 * @return
 * The temperature of the provided weathermap tile.
 */
static int real_temperature(int x, int y, const timeofday_t *tod) {
    int i, temp, adj;

    // Clear and partly-cloudy skies have a stronger temperature effect
    // than overcast skies, since clouds create a barrier to heat escaping
    // and sunlight entering. Super thick clouds add additional buffer.
    // If adj is set to one, then the weather provides some amount of buffer effect.
    // This buffer will override the forestry one if it is set.
    switch (weathermap[x][y].sky) {
        case SKY_CLEAR:
        case SKY_LIGHTCLOUD:
            adj = 0;
            break;
        case SKY_HURRICANE:
        case SKY_BLIZZARD:
            adj = 2;
            break;
        default:
            adj = 1;
            break;
    }

    /* adjust for time of day */
    temp = weathermap[x][y].temp;
    for (i = HOURS_PER_DAY/2; i < HOURS_PER_DAY; i++) {
        temp += season_tempchange[i];
        /* high amounts of water has a buffering effect on the temp */
        if (weathermap[x][y].water > 33) {
            i++;
        }
        // Cloudy skies will have a buffering effect on the temperature
        if (adj >= 1)
            i += adj;
        // High amounts of trees also provide some amount of buffering under clear skies
        else if (weathermap[x][y].forestry > 60) {
            i++;
        }
    }
    for (i = 0; i <= tod->hour; i++) {
        temp += season_tempchange[i];
        if (weathermap[x][y].water > 33) {
            i++;
        }
        // Cloudy skies will have a buffering effect on the temperature
        if (adj >= 1)
            i += adj;
        // High amounts of trees also provide some amount of buffering under clear skies
        else if (weathermap[x][y].forestry > 60) {
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
 *
 * @param m
 * map we're on.
 *
 * @return
 * temperature on the provided space.
 */
int real_world_temperature(int x, int y, mapstruct *m) {
    int wx, wy, temp, eleva, elevb, trees;
    object *op;
    timeofday_t tod;

    // Get the time of day for real_temperature
    // Since real_temperature is sometimes called in a loop, it expects
    // the time of day to be provided to it instead of calculating it directly.
    get_tod(&tod);

    /*LOG(llevDebug, "real_world_temperature: worldmaptoweathermap : %s\n",m->path);*/
    worldmap_to_weathermap(x, y, &wx, &wy, m);
    temp = real_temperature(wx, wy, &tod);
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

    // Get localized effects from trees, too.
    trees = get_config_tile(x, y, m, forest_list);
    // Sparse trees reduce local temp by 1.
    // Dense trees raise it by one.
    if (trees > 0) {
        if (trees < 4)
            --temp;
        else
            ++temp;
    }
    // And done!
    return temp;
}

/**
 * Calculate temperature of a spot.
 *
 * @param x
 * @param y
 * weathermap coordinates.
 *
 * @param tod
 * time of day.
 */
static void temperature_calc(int x, int y, const timeofday_t *tod) {
    int dist, equator, elev, n, trees;
    float diff, tdiff;

    // Warmer air has higher pressure than colder air.
    // Store the old value for temperature.
    int oldtemp = weathermap[x][y].temp, tempdiff;

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
        // Make sure that higher elevations cause lower temps.
        elev = MIN(20000, weathermap[x][y].avgelev)/1000;
    }
    weathermap[x][y].temp -= elev;

    /**
     * Now we adjust for the presence of trees.
     * Thick trees create a heat-holding canopy.
     * Sparse trees don't hold so much heat in,
     * and actually work to reduce temperatures.
     */
    // Arbitrarily make the cutoff threshold for heat-hold as 60
    trees = weathermap[x][y].forestry;
    // Dense trees can raise the temperature up to ~3 degrees, per the calculations below.
    if (trees >= 60) {
        weathermap[x][y].temp += (trees-60)/15;
    }
    // If not, then we have heat reduction, most effective (~4 degrees) at 30.
    else if (trees >= 30){
        weathermap[x][y].temp -= (60-trees)/8;
    }
    else {
        weathermap[x][y].temp -= trees/8;
    }

    // Now we determine the difference in temperature and adjust the pressure accordingly.
    tempdiff = weathermap[x][y].temp - oldtemp;
    // The rate (arbitrarily chosen) for temperature-to-pressure change is 1 degrees per millibar
    // I'd have to keep track of partial millibar changes if I wanted to be coarser in this.
    if (tempdiff != 0)
        weathermap[x][y].pressure += tempdiff;
}

/**
 * Let the madness, begin.
 *
 * This function is the one that ties everything together.  Here we loop
 * over all the weathermaps, and compare the various conditions we have
 * calculated up to now, to figure out what the sky conditions are for each
 * square.
 *
 * @todo
 * Rework fog/hail generation
 * Fog could certainly be less rare.
 */
void compute_sky() {
    int x, y;
    int temp;
    int calc, inv_pressure;
    float press_root;
    timeofday_t tod;

    // Before we begin the loops, we get the time of day for real_temperature()
    get_tod(&tod);

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            temp = real_temperature(x, y, &tod);
            // Make sure we clip to the allowed pressure range.
            inv_pressure = MAX(0, MIN(80, (PRESSURE_MAX - weathermap[x][y].pressure)));
            // Take the square root. This allows us to have values weighted toward
            // producing rain. Also the resultant value is <9, since our limit is 80.
            press_root = sqrt(inv_pressure);
            calc = MAX(0, MIN(900, (int)(press_root * weathermap[x][y].humid)));
            // 900 / 7 = 129-ish. So as long as we divide by a number greater than that, we're good.
            // If we divide by smaller, we overrun the sequential weather numbers, and reach FOG and HAIL
            // when not encountering their special cases.
            calc /= 130;

            // If wind speed is high enough and we have rain, we can add one.
            if (calc >= SKY_LIGHT_RAIN && calc < SKY_HURRICANE && weathermap[x][y].windspeed > 30)
                ++calc;
            // If we are cold enough we have snow.
            if (temp <= 0 && calc >= SKY_LIGHT_RAIN)
                calc += 10;

            // Keep the old fog/hail generation for now
            if (weathermap[x][y].pressure < 980 && weathermap[x][y].pressure < 1000) {
                if (temp > 0 && temp < 5 && weathermap[x][y].humid > 95 &&
                    weathermap[x][y].windspeed < 3) {
                    calc = SKY_FOG; /* rare */
                }
                if (temp > 0 && temp < 5 && weathermap[x][y].humid > 70 &&
                    weathermap[x][y].windspeed > 35) {
                    calc = SKY_HAIL; /* rare */
                }
            }
            weathermap[x][y].sky = calc;
        }
    }
}

/**
 * The world spinning drags the weather with it.
 * The equator is diagonal, and the poles are 45 degrees from north /south.
 * What the hell, lets spin the planet backwards.
 *
 * Due to the polar layout, the weather moves from northeast to southwest.
 * This does, however, make storms come from polar-east (in-game northeast).
 * So the world is spinning backwards, even if it looks odd.
 */
static void spin_globe() {
    int x, xy, xy_eff;
    int buffer_humid;
    int buffer_sky;
    int buffer_pressure;

    // On each pass, x + y is a constant. We shift down and to the left, and wraparound to the upper right.
    // The cornermost tiles by the poles to not move as a result, so we can skip them.
    for (xy = 1; xy < WEATHERMAPTILESX + WEATHERMAPTILESY - 1; ++xy) {
        // Effective xy is essentially clipped to the end.
        // xy-xy_eff is thus the bounds on the other side of the map to care about for wraparound.
        xy_eff = MIN(xy, WEATHERMAPTILESX-1);
        buffer_humid = weathermap[xy-xy_eff][xy_eff].humid;
        buffer_sky = weathermap[xy-xy_eff][xy_eff].sky;
        buffer_pressure = weathermap[xy-xy_eff][xy_eff].pressure;
        for (x = xy-xy_eff; x < xy_eff; ++x) {
            /* Using xy directly here *looks* wrong, but is actually not,
             * since x = xy-xy_eff+c, where c is one less than the loop count;
             * thus, xy-x = xy-xy+xy_eff-c = xy_eff-c.
             * This is within the bounds of the map at all times.
             */
            weathermap[x][xy-x].humid = weathermap[x+1][xy-x-1].humid;
            weathermap[x][xy-x].sky = weathermap[x+1][xy-x-1].sky;
            weathermap[x][xy-x].pressure = weathermap[x+1][xy-x-1].pressure;
        }
        weathermap[xy_eff][xy-xy_eff].humid = buffer_humid;
        weathermap[xy_eff][xy-xy_eff].sky = buffer_sky;
        weathermap[xy_eff][xy-xy_eff].pressure = buffer_pressure;
    }
}

/**
 * Calculate the humidity of the given weather tile.
 *
 * @param x
 * @param y
 * weathermap coordinates we wish to calculate humidity for.
 *
 * @param dark
 * The darkness amount on the world. Presumably from get_world_darkness().
 *
 * @return
 * the humidity of the weathermap square, trimmed to the range [0, 100]
 */
static int humid_tile(int x, int y, int dark) {
    // ox and oy denote the neighbor that is influencing us (due to winds from there)
    int ox = x, oy = y, humid, evap, tempeffect, lightness;

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
    // Determine the effect of sunlight on evaporation.
    int light = MAX_DARKNESS - dark;
    // The sky conditions affect how strong an effect the sunlight has.
    switch (weathermap[x][y].sky) {
        case SKY_CLEAR:
            tempeffect = light*light/4;
            break;
        case SKY_LIGHTCLOUD:
            tempeffect = light*light/5;
            break;
        case SKY_OVERCAST:
            tempeffect = light;
            break;
        case SKY_LIGHT_RAIN:
        case SKY_LIGHT_SNOW:
            tempeffect = light*4/5;
            break;
        case SKY_RAIN:
        case SKY_SNOW:
            tempeffect = light/2;
        case SKY_HEAVY_RAIN:
        case SKY_HEAVY_SNOW:
            tempeffect = light/3;
            break;
        case SKY_HURRICANE:
        case SKY_BLIZZARD:
        case SKY_HAIL:
            tempeffect = light/5;
            break;
        case SKY_FOG:
        default:
            tempeffect = 0;
    }
    // Determine the evaporative component contributing to the humidity.
    // The amount of water, the temperature, the wind, the pressure, the time of day, the cloudcover, and the previous humidity all affect the evaporation.
    // The exact formula is arbitrary, but it gives values that make some sense.
    evap = (weathermap[x][y].water/2+20+tempeffect)*(weathermap[x][y].temp+tempeffect)*weathermap[x][y].windspeed*weathermap[x][y].windspeed*(100-weathermap[x][y].humid)/(weathermap[x][y].pressure*weathermap[x][y].humid+1);
    // Don't go negative if temperature gets too low.
    evap = MAX(0, evap);
    // This is where the magic happens
    // If humidity is unstable over time, this is what will need to be tweaked
    // (or one of the values it depends on, if not this)
    humid = (weathermap[x][y].humid*2 +
        (weathermap[ox][oy].humid)*weathermap[ox][oy].windspeed/100 +
        // Evaporative components.
        evap + weathermap[x][y].forestry/10 + rndm(-3, 7))/
        (weathermap[ox][oy].windspeed/100+3)+rndm(-3, 3);
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
    int x, y, dark = get_world_darkness();

    for (y = 0; y < WEATHERMAPTILESY; y++) {
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            weathermap[x][y].humid = humid_tile(x, y, dark);
        }
    }
}

/**
 * Plot the gulfstream map over the wind map.  This is done after the wind,
 * to avoid the windsmoothing scrambling the jet stream.
 */
static void plot_gulfstream() {
    int x, y, tx, diroffset, dirdiff;

    x = gulf_stream_start;

    // Use the same offset/multiplier formula we used in gulf stream initialization
    // to make the code here much cleaner to look at.
    if (gulf_stream_direction) {
        diroffset = 0;
        dirdiff = -1;
    }
    else {
        diroffset = 10;
        dirdiff = 1;
    }
    for (y = WEATHERMAPTILESY-1; y > 0; y--) {
        for (tx = 0; tx < GULF_STREAM_WIDTH && x+tx < WEATHERMAPTILESX; tx++) {
            if (similar_direction(weathermap[x+tx][y].winddir, gulf_stream_dir[tx][y]) && weathermap[x+tx][y].windspeed < GULF_STREAM_BASE_SPEED-5) {
                weathermap[x+tx][y].windspeed += gulf_stream_speed[tx][y];
            } else{
                weathermap[x+tx][y].windspeed = gulf_stream_speed[tx][y];
            }
            weathermap[x+tx][y].winddir = gulf_stream_dir[tx][y];
            if (tx == GULF_STREAM_WIDTH-1) {
                switch ((diroffset-gulf_stream_dir[tx][y])*dirdiff) {
                    case 6: x--; break;
                    case 7: break;
                    case 8: x++; break;
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
    /* occasionally move the stream
     * Arbitrary code from the original implementation says
     * 1 in 500 to switch, then 1 in 2 the switch actually is relevant.
     *
     * So, if we make the outer effect 1 in 1000, we cover both.
     */
    if (rndm(1, 1000) == 1) {
        for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
            for (y = 0; y < WEATHERMAPTILESY-1; y++) {
                // The direction changes here are dir + 4 mod 8, but 8 instead of 0 on those ones.
                gulf_stream_dir[tx][y] = (gulf_stream_dir[tx][y] + 4) & 7;
                // And we want 8 as a direction instead of 0.
                if (gulf_stream_dir[tx][y] == 0)
                    gulf_stream_dir[tx][y] = 8;
            }
        }
    }
    /* Occasionally move the gulf stream starting point.
     * Original code had 1 in 25 to try, but 1 in 3 that the move was 0.
     *
     * So, the chance of actual movement was 2 in 75.
     *
     * We will use that and redesign the inner offset generation to determine + or - movement.
     */
    if (rndm(1, 75) <= 2) {
        // Only get +1 or -1
        gulf_stream_start += 1-2*rndm(0, 1);
        // Make sure we don't go off the map.
        if (gulf_stream_start >= WEATHERMAPTILESX-GULF_STREAM_WIDTH) {
            gulf_stream_start--;
        }
        if (gulf_stream_start < 1) {
            gulf_stream_start++;
        }
    }
}

/**
 * Keep track of how much rain has fallen in a given weathermap square.
 *
 * This is called at the beginning of every in-game hour (tod.minute == 0)
 * To make some amount of sampling effect out of totals.
 */
void process_rain() {
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

void tick_weather() {
    assert(settings.dynamiclevel > 0);
    update_humid();         /* Run the humidity updates based on prior pressure, temperature, and wind */
    perform_pressure();     /* pressure is the random factor */
    smooth_wind();          /* calculate the wind. depends on pressure */
    plot_gulfstream();
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
 * Read a config file that tells how many units (for an arbitrary purpose) a given
 * arch is worth during calculations.
 * By defining in a file, we get our structure to be non-static,
 * so we can do pointer comparisons on the
 * object name or arch rather than string comparisons on the name.
 *
 * @param settings
 * Pointer to the settings structure, so we can get the directory where the config
 * is stored.
 *
 * @param conf_filename
 * The name of the config file we are loading.
 *
 * @param list
 * Pointer to the list we are appending entries to.
 *
 * @return
 * 0 if successful (even if lines are malformed in the file), 1 otherwise
 */
static int init_config_vals(const Settings *settings, const char *conf_filename, DensityConfig **list) {
    char filename[MAX_BUF], *line, *name;
    BufferReader *bfr;
    FILE *fp;
    int found, is_obj_name, tree_count;

    snprintf(filename, sizeof(filename), "%s/%s", settings->confdir, conf_filename);
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

                    // sscanf on strings is wonky (it always reads to whitespace),
                    // so I'm gonna do it by just nabbing part of the buffer.
                    name = line; // Each line starts with name
                    line = strchr(line, ',');
                    if (line == NULL) {
                        LOG(llevError, "init_config_vals: Malformed forestry entry in %s, line %d:\n%s\n",
                            filename, bufferreader_current_line(bfr), line);
                        // Move on to the next line and hope it is fine.
                        continue;
                    }
                    // Null terminate the end of the name, and move to the next space.
                    *(line++) = '\0';
                    // Move past whitespace.
                    while (*line == ',' || *line == ' ')
                        ++line;
                    found = sscanf(line, "%d, %d\n", &is_obj_name, &tree_count);
                    if (found != 2) {
                        // Print an error for the malformed line
                        LOG(llevError, "init_config_vals: Malformed forestry entry in %s, line %d:\n%s\n",
                            filename, bufferreader_current_line(bfr), line);
                    }
                    else {
                        // Add a struct to the list.
                        DensityConfig *frst = (DensityConfig *)malloc(sizeof(DensityConfig));
                        if (!frst) {
                            fatal(OUT_OF_MEMORY);
                        }
                        // Shared strings are friend, not food
                        frst->name = add_string(name);
                        frst->is_obj = is_obj_name;
                        frst->value_density = tree_count;
                        // Attach to front of list, since order doesn't matter much, if at all.
                        frst->next = *list;
                        *list = frst;
                    }
            }
        }
        bufferreader_destroy(bfr);
        return 0;
    }
    LOG(llevError, "init_config_vals: Could not open file %s. No forestry data is defined.\n", filename);
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
        DensityConfig *tmp;
        if (QUERY_FLAG(ob, FLAG_IS_WATER)) {
            (*water)++;
        }
        // Deserts will reduce the humidity/precipitation in the spaces they exist in.
        // Since the config entries are all negative, we can add the value here.
        (*water) += get_config_tile(x, y, m, water_list);

        // Handle forestry
        (*trees) += get_config_tile(x, y, m, forest_list);

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
            // And, even in the desert, relative humidity averages like 20%. So, in non-deserts, it should be like 40%.
            // This should help prevent a forever-hurricane over the ocean.
            weathermap[x][y].humid = 40+water*40/(spwtx*spwty);
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

/**
 * Initialize the gulf stream.
 */
static void init_gulfstreammap() {
    int x, y, tx, starty, ymul, diroffset, dirdiff;

    /* build a gulf stream */
    x = rndm(GULF_STREAM_WIDTH, WEATHERMAPTILESX-GULF_STREAM_WIDTH);
    /* doth the great bob inhale or exhale? */
    gulf_stream_direction = rndm(0, 1);
    gulf_stream_start = x;

    // Handle both gulf stream directions
    if (gulf_stream_direction) {
        // These variables allow us to only define the loop once.
        // That should make the code less awful to see
        starty = WEATHERMAPTILESY-1;
        ymul = -1;
        // The diroffset pieces allow us to merge the meat of the loops
        // the mapping between the different directions is as follows
        // 8 <-> 2
        // 7 <-> 3
        // 6 <-> 4
        // Thus setting diroffset to 10 when dirdiff is 1 gives us one direction
        // and setting diroffset to 0 when dirdiff is -1 gives us the other.
        diroffset = 0;
        dirdiff = -1;
    }
    else {
        starty = 0;
        ymul = 1;
        diroffset = 10;
        dirdiff = 1;
    }
    // Huzzah! a loop common to both directions!
    for (y = starty; y >= 0 && y < WEATHERMAPTILESY; y += ymul) {
        switch (rndm(0, 6)) {
            case 0:
            case 1:
            case 2:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == 0) {
                        gulf_stream_dir[tx][y] = (diroffset-7)*dirdiff;
                    } else {
                        gulf_stream_dir[tx][y] = (diroffset-8)*dirdiff;
                        if (tx == 0) {
                            x--;
                        }
                    }
                }
                break;

            case 3:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    gulf_stream_dir[tx][y] = (diroffset-7)*dirdiff;
                }
                break;

            case 4:
            case 5:
            case 6:
                for (tx = 0; tx < GULF_STREAM_WIDTH; tx++) {
                    gulf_stream_speed[tx][y] = rndm(GULF_STREAM_BASE_SPEED, GULF_STREAM_BASE_SPEED+10);
                    if (x == WEATHERMAPTILESX-1) {
                        gulf_stream_dir[tx][y] = (diroffset-7)*dirdiff;
                    } else {
                        gulf_stream_dir[tx][y] = (diroffset-6)*dirdiff;
                        if (tx == 0) {
                            x++;
                        }
                    }
                }
                break;
        }
    }
}

/**
 * Initialize the wind randomly. Does both direction and speed in one pass
 *
 * Values for speed are fairly low -- the calculations for wind
 * are built in a way where this is not a problem.
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
 * Reset pressure map.
 *
 * Sets the whole map to 1013 mbar,
 * then adds some disturbances in two different ways.
 */
static void init_pressure(void) {
    int x, y;
    int l, n, k;

    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            weathermap[x][y].pressure = 1013;
        }
    }
    // Add medium patches of low noise.
    for (l = 0; l < PRESSURE_ITERATIONS; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        n = rndm(PRESSURE_MIN, PRESSURE_MAX);
        for (k = 1; k < PRESSURE_AREA; k++) {
            switch (rndm(0, 3)) {
            case 0: if (x < WEATHERMAPTILESX-1) x++; break;
            case 1: if (y < WEATHERMAPTILESY-1) y++; break;
            case 2: if (x) x--; break;
            case 3: if (y) y--; break;
            }
            weathermap[x][y].pressure = (weathermap[x][y].pressure+n)/2;
        }
    }
    /* create random spikes in the pressure
     * These go way beyond the bounds of allowed pressure, but smooth_pressure
     * turns that into a sizable pressure blob.
     */
    for (l = 0; l < PRESSURE_SPIKES; l++) {
        x = rndm(0, WEATHERMAPTILESX-1);
        y = rndm(0, WEATHERMAPTILESY-1);
        n = rndm(500, 2000);
        weathermap[x][y].pressure = n;
    }
    smooth_pressure();
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

/**
 * Save the gulf stream to localdir
 *
 * @param settings
 * The settings structure we are using to find localdir.
 * Pretty sure it's the same one as the global settings, but oh well.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
int write_gulfstreammap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/gulfstreammap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing gulf stream map to file.\n");
    // First block is speed
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", gulf_stream_speed[x][y]);
        }
        fprintf(fp, "\n");
    }
    // second block is direction
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", gulf_stream_dir[x][y]);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save the wind speed to localdir
 *
 * @param settings
 * The settings structure we are using to find localdir.
 * Pretty sure it's the same one as the global settings, but oh well.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
int write_windspeedmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/windspeedmap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing wind speed map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%hd ", weathermap[x][y].windspeed);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save wind direction to localdir
 *
 * @param settings
 * The settings structure we are using to find localdir.
 * Pretty sure it's the same one as the global settings, but oh well.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
int write_winddirmap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/winddirmap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing wind direction map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].winddir);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/**
 * Save pressure information to localdir
 *
 * @param settings
 * The settings structure we are using to find localdir.
 * Pretty sure it's the same one as the global settings, but oh well.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
int write_pressuremap(const Settings *settings) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/pressuremap", settings->localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing pressure map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].pressure);
        }
        fprintf(fp, "\n");
    }
    of_close(&of);
    return 0;
}

/* This stuff is for creating the images,
 * and is only used by write_weather_images()
 */

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
 * Dump all the weather data as an image.
 * Does not write to the tod file, since that is already handled elsewhere.
 * (It used to, though, probably because all the saves from within the weather subsytem were
 * synchronized to the time of day. Since I changed that to lighten the load on those ticks,
 * saving the tod is not nearly as relevant.)
 *
 * The image created is a ppm image, so it is really easy to just write it without a library.
 *
 * @return
 * 0 if successful, 1 if failed.
 */
int write_weather_images() {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;
    int32_t min[8], max[8], avgrain, avgwind, realmaxwind;
    double scale[8], realscalewind;
    uint8_t pixels[3*3*WEATHERMAPTILESX];
    int64_t total_rainfall = 0;
    int64_t total_wind = 0;
    timeofday_t tod;

    // Get the time of day.
    // This is important for weather output later.
    get_tod(&tod);

    // Determine the output file's limits.
    min[0] = -100;         max[0] = 100;
    min[1] = 0;            max[1] = 0;
    min[2] = 0;            max[2] = 0;
    min[3] = PRESSURE_MIN; max[3] = PRESSURE_MAX;
    min[4] = 0;            max[4] = 0;
    // The 6th tile is raw wind direction, and thus does not need limits
    min[6] = 0;            max[6] = 100;
    min[7] = -45;          max[7] = 45;
    // The 9th tile is raw sky data and does not need limits
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
/*          min[0] = MIN(min[0], weathermap[x][y].water); */
            min[1] = MIN(min[1], weathermap[x][y].avgelev);
            min[2] = MIN(min[2], weathermap[x][y].rainfall);
/*          min[3] = MIN(min[3], weathermap[x][y].pressure); */
            min[4] = MIN(min[4], weathermap[x][y].windspeed);
/*          min[6] = MIN(min[6], weathermap[x][y].humid); */
/*          min[7] = MIN(min[7], real_temp[x][y]); */

/*          max[0] = MAX(max[0], weathermap[x][y].water); */
            max[1] = MAX(max[1], weathermap[x][y].avgelev);
            max[2] = MAX(max[2], weathermap[x][y].rainfall);
/*          max[3] = MAX(max[3], weathermap[x][y].pressure); */
            max[4] = MAX(max[4], weathermap[x][y].windspeed);
/*          max[6] = MAX(max[6], weathermap[x][y].humid); */
/*          max[7] = MAX(max[7], real_temp[x][y]); */
            total_rainfall += weathermap[x][y].rainfall;
            total_wind += weathermap[x][y].windspeed;
        }
    }
    // Twiddle the data on total rainfall, since they have a different color for above/below average
    // This allows us to have the full scale of color range on each above average and below average.
    avgrain = total_rainfall/(WEATHERMAPTILESX*WEATHERMAPTILESY);
    avgwind = (total_wind   /((WEATHERMAPTILESX*WEATHERMAPTILESY)*3/2));
    max[2] = avgrain-1;
    realscalewind = 255.0l/(max[4]-min[4]);
    realmaxwind = max[4];
    max[4] = avgwind-1;
    for (x = 0; x < 8; x++) {
        scale[x] = 255.0l/(max[x]-min[x]);
    }

    LOG(llevDebug, "Writing weather conditions map.\n");

    snprintf(filename, sizeof(filename), "%s/weather.ppm", settings.localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    fprintf(fp, "P6\n%d %d\n", 3*WEATHERMAPTILESX, 3*WEATHERMAPTILESY);
    fprintf(fp, "255\n");
    // First row of maps
    for (y = 0; y < WEATHERMAPTILESY; y++) {
        memset(pixels, 0, 3 * 3 * WEATHERMAPTILESX);
        for (x = 0; x < WEATHERMAPTILESX; x++) {
            // water/tree map -- first map of row
            // blue = high water amount, black = low water amount, red = desert-like, green = trees
            if (weathermap[x][y].water < 0)
                pixels[3*x+(0*WEATHERMAPTILESX*3+RED)] = (uint8_t)(255-(weathermap[x][y].water-min[0])*scale[0]*2);
            else
                pixels[3*x+(0*WEATHERMAPTILESX*3+BLUE)] = (uint8_t)((weathermap[x][y].water)*scale[0]*2);
            // Either way, we use green to highlight the trees, too
            // Make this real simple since it is established that forestry values range from 0 to 100.
            // As a result, our values go from 0-250.
            pixels[3*x+(0*WEATHERMAPTILESX*3+GREEN)] = (uint8_t)((weathermap[x][y].forestry)*5/2);
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
            int temp = real_temperature(x, y, &tod);
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
 * init_config_vals(), and the values read here are calculated
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
 */
static int read_temperaturemap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, res;
    int16_t temperature;

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
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%hd ", &temperature);
            if (res != 1) {
                LOG(llevError, "Temperature file is malformed, unable to load temps from file.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // Clip to reasonable bounds.
            weathermap[x][y].temp = MIN(60, MAX(-30, temperature));
            // Adjust the data pointer.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in temperature map.\n");
                bufferreader_destroy(bfr);
                return -1;
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
 */
static int read_rainfallmap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, res;

    snprintf(filename, sizeof(filename), "%s/rainfallmap", settings->localdir);
    LOG(llevDebug, "Reading rainfall data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing rainfall map...\n");
        init_rainfall();
        // If we write to file successfully, consider initialization a success.
        if (write_rainfallmap(settings) != 0);
            return -1;
        return 1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%u ", &weathermap[x][y].rainfall);
            if (res != 1) {
                LOG(llevError, "Rainfall file is corrupted, cannot load rainfall from file.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in rainfall map.\n");
                bufferreader_destroy(bfr);
                return -1;
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
 * Read the gulf stream, or initialize it if no saved information.
 *
 * @param settings
 * The settings structure we use to locate config folders.
 * Here we want localdir specifically.
 *
 * @return
 * 0 if success without initialization, 1 if success with initialization.
 * Returns -1 on failure.
 */
static int read_gulfstreammap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, in, res;

    snprintf(filename, sizeof(filename), "%s/gulfstreammap", settings->localdir);
    LOG(llevDebug, "Reading gulf stream data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing gulf stream maps...\n");
        init_gulfstreammap();
        res = write_gulfstreammap(settings);
        LOG(llevDebug, "Done\n");
        if (res == 0)
            return 1;
        return -1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    // First we read in the speeds
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &in);
            if (res != 1) {
                LOG(llevError, "Gulf stream speed definitions are malformed. Cannot load gulf stream from file.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            gulf_stream_speed[x][y] = MIN(120, MAX(0, in));
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in gulfstream speed map.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // Okay, so we want the first character after the space/newline.
            data = tmp + 1;
        }
        // Make sure we don't leave a newline in the event of a trailing space on a given line.
        if (*data == '\n')
            ++data;
    }
    // Then we read in the directions.
    for (x = 0; x < GULF_STREAM_WIDTH; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &in);
            if (res != 1) {
                LOG(llevError, "Gulf stream direction definitions are malformed. Cannot load gulf stream from file.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            gulf_stream_dir[x][y] = MAX(1, MIN(8, in));
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in gulfstream direction map.\n");
                bufferreader_destroy(bfr);
                return -1;
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
 * Read the wind speed.
 * We do not init here, since winddir should have handled that already.
 * Depends on wind direction loading having been run before it as a result.
 *
 * @param settings
 * Pointer to the settings structure.
 * We want localdir from it.
 *
 * @return
 * 0 if sucessfully loaded, -1 if failed.
 * There is no init, so it won't return 1 ever.
 */
static int read_windspeedmap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, res;
    int8_t spd;

    snprintf(filename, sizeof(filename), "%s/windspeedmap", settings->localdir);
    LOG(llevDebug, "Reading wind speed data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        // Wind direction is done before this, and should have initialized this already.
        return -1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%hhd ", &spd);
            if (res != 1) {
                LOG(llevError, "Wind speed file is malformed. Cannot load wind speed file.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // Clip to reasonable bounds
            weathermap[x][y].windspeed = MIN(120, MAX(0, spd));
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in wind speed map.\n");
                bufferreader_destroy(bfr);
                return -1;
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
 * Read the wind direction. Will initialize the entirety of wind if file doesn't exist.
 *
 * @param settings
 * The settings structure we will use to find localdir
 *
 * @return
 * 0 if success without initialization, 1 if success with initialization
 * returns -1 on failure
 */
static int read_winddirmap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, d, res;

    snprintf(filename, sizeof(filename), "%s/winddirmap", settings->localdir);
    LOG(llevDebug, "Reading wind direction data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing wind direction and speed maps...\n");
        init_wind();
        // If both of these succeed, the end result is 0.
        res = write_winddirmap(settings);
        res += write_windspeedmap(settings);
        LOG(llevDebug, "Done\n");
        if (res == 0)
            return 1;
        return -1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%d ", &d);
            if (res != 1) {
                LOG(llevError, "Wind direction map is malformed. Could not load wind direction.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // If the direction is not valid, just give it one randomly.
            if (d < 1 || d > 8) {
                d = rndm(1, 8);
            }
            weathermap[x][y].winddir = d;
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in wind direction map.\n");
                bufferreader_destroy(bfr);
                return -1;
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
 * Read the pressure information from disk. If it doesn't exist, initialize pressure.
 *
 * @param settings
 * Pointer to the settings structure so we can reach localdir
 *
 * @return
 * 0 if successful without initialization, 1 if successful with initialization
 * Returns -1 on failure.
 */
static int read_pressuremap(const Settings *settings) {
    char filename[MAX_BUF], *data, *tmp;
    FILE *fp;
    BufferReader *bfr;
    int x, y, res;
    int16_t press;

    snprintf(filename, sizeof(filename), "%s/pressuremap", settings->localdir);
    LOG(llevDebug, "Reading pressure data from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Cannot open %s for reading\n", filename);
        LOG(llevInfo, "Initializing pressure maps...\n");
        init_pressure();
        res = write_pressuremap(settings);
        LOG(llevDebug, "Done\n");
        if (res == 0)
            return 1;
        return -1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            res = sscanf(data, "%hd ", &press);
            if (res != 1) {
                LOG(llevError, "Pressure map is malformed. Could not load pressure.\n");
                bufferreader_destroy(bfr);
                return -1;
            }
            // Apply clipping to the pressure.
            weathermap[x][y].pressure = MIN(PRESSURE_MAX, MAX(PRESSURE_MIN, press));
            // Now we update the pointer to data to move to the next item.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in pressure map.\n");
                bufferreader_destroy(bfr);
                return -1;
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

/********************************************************************************************
 * Section END -- weather data readers
 ********************************************************************************************/

/********************************************************************************************
 * Section -- weather event listeners
 ********************************************************************************************/

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

/**
 * Global clock event handling for weather.
 * This is separate from the mapenter listener because I felt like it.
 * They possibly could be merged, it just makes documenting the parameters
 * for the function into a mess.
 *
 * @param type
 * The event type.
 *
 * @return
 * 0.
 */
static int weather_clock_listener(int *type, ...) {
    va_list args;
    int code;

    va_start(args, type);
    code = va_arg(args, int);
    va_end(args);

    switch (code) {
        case EVENT_CLOCK:
            if (!(pticks%PTICKS_PER_CLOCK)) {
                // We need the time of day for handling of process_rain()
                timeofday_t tod;
                get_tod(&tod);
                /* call the weather calculators, here, in order */
                tick_weather();
                // At every hour, measure the rainfall.
                if (tod.minute == 0) {
                    process_rain();
                }
                /* perform_weather must follow calculators */
                perform_weather();
            }
            // Handle weather printouts after enough server time.
            // By using primes here, rather than inside tick_the_clock(), we can reduce the load on a given tick.
            // (pticks%1500 is real busy otherwise)
            // This produces the side-effect that the server actually has to be on for that length
            // of time without interruption for the save to happen, but most servers are run long-term
            // anyway, so this should not be an issue
            if (!(pticks%1511))
                write_weather_images();
            if (!(pticks%31511))
                write_pressuremap(&settings);
            if (!(pticks%33013))
                write_winddirmap(&settings);
            if (!(pticks%34501))
                write_windspeedmap(&settings);
            if (!(pticks%36007))
                write_humidmap(&settings);
            if (!(pticks%39019))
                write_temperaturemap(&settings);
            if (!(pticks%40507))
                write_gulfstreammap(&settings);
            if (settings.fastclock > 0 && !(pticks%42013))
                write_skymap();
            if (!(pticks%43517))
                write_rainfallmap(&settings);
            break;
    }

    return 0;
}

/********************************************************************************************
 * Section END -- weather event listeners
 ********************************************************************************************/

// Event handler ids start at 1, so 0 is an unset flag.
static event_registration global_map_handler = 0, global_clock_handler = 0;

/**
 * Weather module initialisation.
 * @param settings server settings.
 */
void cfweather_init(Settings *settings) {
    int tx, ty;
    // Initialize the forestry information from file.
    init_config_vals(settings, "treedefs", &forest_list);
    init_config_vals(settings, "waterdefs", &water_list);
    /* Unless you know what you're doing, do not re-order these
     * I think I got all the dependencies noted, but it works this way
     * and I'd advise against changing the order unless you have a good reason.
     */
    read_pressuremap(settings);
    // Begin to initialize the various data pieces for the weather
    // If wind direction did initialization, we don't need to read the wind speed from file.
    if (read_winddirmap(settings) == 0)
        read_windspeedmap(settings);
    read_gulfstreammap(settings);
    // Some gulf stream fiddling that happens every startup.
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
    global_clock_handler = events_register_global_handler(EVENT_CLOCK, weather_clock_listener);
    /* Disable the plugin in case it's still there */
    linked_char *disable = (linked_char *)calloc(1, sizeof(linked_char));
    disable->next = settings->disabled_plugins;
    disable->name = strdup("cfweather");
    settings->disabled_plugins = disable;
}

void cfweather_close() {
    DensityConfig *cur;
    if (global_map_handler != 0)
        events_unregister_global_handler(EVENT_MAPENTER, global_map_handler);
    if (global_clock_handler != 0)
        events_unregister_global_handler(EVENT_CLOCK, global_clock_handler);
    // Deallocate our linked list of forest entries.
    while (forest_list != NULL) {
        cur = forest_list;
        forest_list = forest_list->next;
        free_string(cur->name);
        free(cur);
    }
    // Do the same for the water list
    while (water_list != NULL) {
        cur = water_list;
        water_list = water_list->next;
        free_string(cur->name);
        free(cur);
    }
}


/*@}*/
