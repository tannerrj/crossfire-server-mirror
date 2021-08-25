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
weathermap_t **weathermap;

/* weather constants */

#define POLAR_BASE_TEMP		0	/* C */
#define EQUATOR_BASE_TEMP	30	/* C */
#define SEASONAL_ADJUST		10	/* polar distance */
#define GULF_STREAM_WIDTH       3       /* width of gulf stream */
#define GULF_STREAM_BASE_SPEED  40      /* base speed of gulf stream */

/* don't muck with these unless you are sure you know what they do */
#define PRESSURE_ITERATIONS		30
#define PRESSURE_AREA			180
#define PRESSURE_ROUNDING_FACTOR	2
#define PRESSURE_ROUNDING_ITER		1
#define PRESSURE_SPIKES			3
#define PRESSURE_MAX			1040
#define PRESSURE_MIN			960

/**
 * This is a multiplier for the wind caused by pressure differences.
 * The type of overal climate you get depends on this.
 * Too little wind, and the rain hugs the coast.
 * Too much wind, and there are hurricanes and blizzards everywhere.
 * 1 is too little.
 */
#define WIND_FACTOR  4.0

/* editing the below might require actual changes to code */
#define WEATHERMAPTILESX		100
#define WEATHERMAPTILESY		100

/********************************************************************************************
 * Section -- weather structures
 * Structures to handle various aspects of the weather system.
 ********************************************************************************************/

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

/**
 * Defines a tile the weather system should avoid.
 */
typedef struct _weather_avoids {
    sstring name;                    /**< Tile archetype name. It is always arch name, not object name. */
    int snow;                        /**< Is this a long-term weather effect, like snow or a puddle?
                                          Used for various tests. */
    struct _weather_avoids *next;    /**< The next item in the avoid list. */
} weather_avoids_t;

/**
 * Defines a tile the weather system can change to another tile.
 */
typedef struct _weather_replace {
    sstring tile;                  /**< Tile archetype or object name. */
    archetype *special_snow;       /**< The archetype name of the tile to place over specified tile. */
    archetype *doublestack_arch;   /**< If set, this other archetype will be added atop special_snow. */
    int arch_or_name;              /**< If set, tile matches the archetype name, else the object's name. */
    struct _weather_replace *next; /**< The next item in the replace list. */
} weather_replace_t;

/**
 * Defines a tile where something can grow.
 */
typedef struct _weather_grow {
	const char *herb;   /**< Arch name of item to grow. */
	const char *tile;   /**< Arch tile to grow on, NULL if anything. */
	int random;         /**< Random apparition factor. Min 1, higher = lower chance of appearance. */
	float rfmin;        /**< Minimum rainfall for herb to grow (inches/day). */
	float rfmax;        /**< Maximum rainfall for herb to grow (inches/day). */
	int humin;          /**< Minimum humidity for herb to grow. */
	int humax;          /**< Maximum humidity for herb to grow. */
	int tempmin;        /**< Minimum temperature for herb to grow. */
	int tempmax;        /**< Maximum temperature for herb to grow. */
	int elevmin;        /**< Minimum elevation for herb to grow. */
	int elevmax;        /**< Maximum elevation for herb to grow. */
	int season;         /**< Season the herb can grow. 0=any or 1-5. */
} weather_grow_t;

/********************************************************************************************
 * Section END -- weather structures
 ********************************************************************************************/

DensityConfig *forest_list = NULL;
DensityConfig *water_list = NULL;
weather_avoids_t *weather_avoids = NULL;
weather_avoids_t *growth_avoids = NULL;
weather_replace_t *weather_replace = NULL;
weather_replace_t *weather_evaporate = NULL;
weather_replace_t *weather_snowmelt = NULL;

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

/** Current weather tile position. */
static int wmperformstartx;
/** Current weather tile position. */
static int wmperformstarty;

/*
 * @todo
 * The following static tables should probably be defined by files.
 */
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
 * buffer containing the path to the world map we want, or NULL if were weren't given a corner direction.
 */
static char *weathermap_to_worldmap_corner(int wx, int wy, int * const x, int * const y, const int dir, char * const buffer, const int bufsize) {
    const int spwtx = (settings.worldmaptilesx*settings.worldmaptilesizex)/WEATHERMAPTILESX,
              spwty= (settings.worldmaptilesy*settings.worldmaptilesizey)/WEATHERMAPTILESY;
    int tx, ty, nx, ny;

    // Load the position on the map the corner of the weathermap takes.
    // Since each map is larger than each weathermap, there can be no more than four
    // map existing in a weathermap.
    switch (dir) {
    case 2:
        tx = (wx+1)*spwtx-1;
        ty = wy*spwty;
        break;
    case 4:
        tx = (wx+1)*spwtx-1;
        ty = (wy+1)*spwty-1;
        break;
    case 6:
        tx = wx*spwtx;
        ty = (wy+1)*spwty-1;
        break;
    case 8:
        tx = wx*spwtx;
        ty = wy*spwty;
        break;
    // If an incorrect direction is given, bail.
    default:
        LOG(llevError, "weathermap_to_worldmap_corner: Invalid direction %d given, should be in set {2,4,6,8}.\n", dir);
        return NULL;
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
static int polar_distance(int x, int y, const int equator) {
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
static int get_config_tile(const int x, const int y, const mapstruct *m, const DensityConfig *list) {
    // If no list specified, shortcut the exit.
    if (list == NULL)
        return 0;
    object *ob = GET_MAP_OB(m, x, y);
    const DensityConfig *tmp;
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
int worldmap_to_weathermap(const int x, const int y, int * const wx, int * const wy, mapstruct * const m) {
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
static object *avoid_weather(int * const av, const mapstruct *m, const int x, const int y, int * const gs, const int grow) {
    int avoid, gotsnow, i;
    object *tmp, *snow;
    const weather_avoids_t *cur;

    avoid = 0;
    gotsnow = 0;
    if (grow) {
        for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
            /* look for things like walls, holes, etc */
            if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR) && !(tmp->material&M_ICE || tmp->material&M_LIQUID)) {
                gotsnow++;
                snow = tmp;
            }
            for (cur = growth_avoids; cur; cur = cur->next) {
                // Due to the use of shared strings, we can do pointer comparison here.
                if (tmp->arch->name == cur->name) {
                    avoid++;
                    break;
                }
            }
            if (avoid && gotsnow) {
                break;
            }
        }
    } else {
        for (tmp = GET_MAP_OB(m, x, y); tmp; tmp = tmp->above) {
            for (cur = weather_avoids; cur; cur = cur->next) {
                if (tmp->arch->name == cur->name) {
                    // We clear FLAG_IS_FLOOR for our snow. The map's default snow does not.
                    // Avoid weirdness on the pathway to Brest and at the south pole by checking for non-floor snow
                    if (!QUERY_FLAG(tmp, FLAG_IS_FLOOR) && cur->snow == 1) {
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
 * Refactor the code to look for arch or object name as it's own code
 *
 * Since we are using shared strings to store our arch/object names,
 * we can simply use pointer arithmetic in order to compare.
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
static int check_replace_match(const object *ob, const weather_replace_t *rep_struct) {
    if (rep_struct->arch_or_name == 1) {
        if (ob->arch->name == rep_struct->tile) {
            return 1;
        }
    } else {
        if (ob->name == rep_struct->tile) {
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
static void do_weather_insert(mapstruct * const m, int x, int y, const archetype *at, const int8_t object_flags, uint16_t material, int insert_flags) {
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
 * Finds the start of the next field in the provided string
 * Skips past interceding commas and spaces.
 *
 * Used in all the config initializations, but not
 * the weathermap initializations
 *
 * @param line
 * The line to process. We will null terminate the end of the current field
 * directly here, so we must pass as non-const.
 *
 * @return
 * NULL if no next field could be found,
 * or the address of the start of that field if found
 */
static char *get_next_field(char *line) {
    // The comma is the end of the field
    line = strchr(line, ',');
    if (line == NULL)
        return NULL;
    // Move past the known field seperator, but null terminate over it for the previous field.
    *(line++) = '\0';
    // While spaces or commas, skip the character
    while (*line == ' ' || *line == ',')
        ++line;
    // Next field begins here.
    return line;
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
            i += weathermap[x][y].water/33;
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
            i += weathermap[x][y].water/33;
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
static void temperature_calc(const int x, const int y, const timeofday_t *tod) {
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
static int humid_tile(const int x, const int y, const int dark) {
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
    int x, y, tx, diroffset, dirdiff, ystart, ydiff, ylimup, ylimlow;

    x = gulf_stream_start;

    // Use the same offset/multiplier formula we used in gulf stream initialization
    // to make the code here much cleaner to look at.
    if (gulf_stream_direction) {
        diroffset = 0;
        dirdiff = -1;
        // We go from WEATHERMAPTILESY-1 down to 1 for the loop
        ystart = WEATHERMAPTILESY-1;
        ydiff = -1;
        ylimup = WEATHERMAPTILESY;
        ylimlow = 0;
    }
    else {
        diroffset = 10;
        dirdiff = 1;
        // We go from 0 to WEATHERMAPTILESY-2 for the loop
        ystart = 0;
        ydiff = 1;
        ylimup = WEATHERMAPTILESY-1;
        ylimlow = -1;
    }
    for (y = ystart; y > ylimlow && y < ylimup; y += ydiff) {
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

/**
 * Temperature is used in a lot of weather function.
 * This need to be precalculated before used.
 *
 * @param m
 * map for which to calculate the temperature. Must be a world map.
 *
 * @todo
 * The results of this seem rather dubious, as we are really just
 * overwriting about 15x15 times per tile calculated.
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
 * Do the weather calculations in order.
 *
 * Re-ordering these will probably produce unintended side effects.
 */
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
 * Section -- Weather effect methods
 * Functions to provide weather effects.
 * This includes precipitation, puddles, ice on water, snowfall, growing plants,
 * defacing the world (since some comments seemed to imply that one was broken, and I never changed it)
 ********************************************************************************************/

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
static void do_precipitation(mapstruct * const m, const int x, const int y, const int temp, const int sky) {
    // Do falling rain/snow here
    const archetype *at = NULL;
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
                if (tmp->arch == at) {
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
static void do_map_precipitation(mapstruct * const m) {
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
 * Put or remove snow. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing. Must be a world map.
 */
static void let_it_snow(mapstruct * const m) {
    int x, y, i, wx, wy;
    int nx, ny, j, d;
    int avoid, temp, sky, gotsnow, found, nodstk;
    object *ob, *tmp, *oldsnow, *topfloor;
    archetype *at, *doublestack, *doublestack2;

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
            doublestack = NULL;
            /* this will definately need tuning */
            avoid = 0;
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
                    at = find_archetype("snow5");
                }
                if (sky >= SKY_HEAVY_SNOW) {
                    at = find_archetype("snow4");
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
                    for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                        if (check_replace_match(topfloor, repl)) {
                            if (repl->special_snow != NULL) {
                                at = repl->special_snow;
                            }
                            if (repl->doublestack_arch != NULL && !nodstk) {
                                doublestack = repl->doublestack_arch;
                            }
                            break;
                        }
                    }
                }
                if (gotsnow && at) {
                    if (oldsnow->arch == at) {
                        at = NULL;
                    } else {
                        object_remove(oldsnow);
                        object_free(oldsnow,0);
                        tmp = GET_MAP_OB(m, x, y);
                        /* clean up the trees we put over the snow */
                        doublestack2 = NULL;
                        if (tmp) {
                            for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                                if (repl->doublestack_arch == NULL) {
                                    continue;
                                }
                                if (check_replace_match(tmp, repl)) {
                                    tmp = tmp->above;
                                    doublestack2 = repl->doublestack_arch;
                                    break;
                                }
                            }
                        }
                        if (tmp != NULL && doublestack2 != NULL) {
                            if (tmp->arch == doublestack2) {
                                object_remove(tmp);
                                object_free(tmp,0);
                            }
                        }
                    }
                }
                if (at != NULL) {
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY|WEATHER_NO_FLOOR, M_ICE, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                    if (doublestack != NULL) {
                        do_weather_insert(m, x, y, doublestack, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ON_TOP);
                    }
                }
            }
            if (temp > 8 && GET_MAP_OB(m, x, y) != NULL) {
                /* melt some snow */
                for (tmp = GET_MAP_OB(m, x, y)->above; tmp; tmp = tmp->above) {
                    avoid = 0;
                    for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                        if (repl->special_snow == NULL) {
                            continue;
                        }

                        if (tmp->arch == repl->special_snow) {
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
                            for (weather_replace_t *melt = weather_snowmelt; melt; melt = melt->next) {
                                if (tmp->arch->name == melt->tile) {
                                    at = melt->special_snow;
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
 * Process rain. This should be called from weather_effect().
 *
 * @param m
 * map we are currently processing.
 */
static void singing_in_the_rain(mapstruct * const m) {
    int x, y, i, wx, wy;
    int nx, ny, d, j;
    int avoid, temp, sky, gotsnow, /*found,*/ nodstk;
    object *ob, *tmp, *oldsnow, *topfloor;
    archetype *at, *doublestack, *doublestack2;

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
            doublestack = NULL;
            avoid = 0;
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
                    for (weather_replace_t *melt = weather_snowmelt; melt; melt = melt->next) {
                        if (tmp->arch->name == melt->tile) {
                            at = melt->special_snow;
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
                for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                    if (check_replace_match(topfloor, repl)) {
                        if (repl->doublestack_arch != NULL && !nodstk) {
                            doublestack = repl->doublestack_arch;
                        }
                        break;
                    }
                }
                if (gotsnow && at) {
                    if (oldsnow->arch == at) {
                        at = NULL;
                    } else {
                        tmp = GET_MAP_OB(m, x, y);
                        object_remove(oldsnow);
                        /* clean up the trees we put over the snow */
                        doublestack2 = NULL;
                        for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                            if (repl->doublestack_arch == NULL) {
                                continue;
                            }
                            if (check_replace_match(tmp, repl)) {
                                tmp = tmp->above;
                                doublestack2 = repl->doublestack_arch;
                                break;
                            }
                        }
                        object_free(oldsnow,0);
                        if (tmp != NULL && doublestack2 != NULL) {
                            if (tmp->arch == doublestack2) {
                                object_remove(tmp);
                                object_free(tmp,0);
                            }
                        }
                    }
                }
                if (at != NULL) {
                    do_weather_insert(m, x, y, at, WEATHER_OVERLAY, M_LIQUID, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
                    if (doublestack != NULL) {
                        do_weather_insert(m, x, y, doublestack, 0, 0, INS_NO_MERGE|INS_NO_WALK_ON|INS_ON_TOP);
                    }
                }
            }
            /* Things evaporate fast in the heat */
            if (GET_MAP_OB(m, x, y) && temp > 8 && sky < SKY_OVERCAST && rndm(temp, 60) > 50) {
                /* evaporate */
                for (tmp = GET_MAP_OB(m, x, y)->above; tmp; tmp = tmp->above) {
                    // Find a tile to evaporate
                    weather_replace_t *evap;
                    for (evap = weather_evaporate; evap; evap = evap->next) {
                        if (tmp->arch->name == evap->tile)
                            break;
                    }
                    // If we found it, then evaporate it
                    if (evap) {
                        object_remove(tmp);
                        object_free(tmp,0);
                        if (weathermap[wx][wy].humid < 100 && rndm(0, 50) == 0) {
                            weathermap[wx][wy].humid++;
                        }
                        // If the evaporation is done, clean up the doublestack on this tile.
                        if (evap->special_snow == NULL) {
                            tmp = GET_MAP_OB(m, x, y);
                            /* clean up the trees we put over the rain */
                            doublestack2 = NULL;
                            for (weather_replace_t *repl = weather_replace; repl; repl = repl->next) {
                                if (repl->doublestack_arch == NULL) {
                                    continue;
                                }
                                if (check_replace_match(tmp, repl)) {
                                    tmp = tmp->above;
                                    doublestack2 = repl->doublestack_arch;
                                    break;
                                }
                            }
                            if (tmp != NULL && doublestack2 != NULL) {
                                if (tmp->arch == doublestack2) {
                                    object_remove(tmp);
                                    object_free(tmp,0);
                                }
                            }
                        }
                        else {
                            // Apply the replacement puddle
                            do_weather_insert(m, x, y, evap->special_snow, WEATHER_OVERLAY, M_LIQUID, INS_NO_MERGE|INS_NO_WALK_ON|INS_ABOVE_FLOOR_ONLY);
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
static void plant_a_garden(mapstruct *const m) {
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
static void change_the_world(mapstruct * const m) {
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
static void weather_effect(mapstruct * const m) {
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
    if (settings.dynamiclevel >= 3) {
        plant_a_garden(m);
    }
}

/**
 * This routine slowly loads the world, patches it up due to the weather,
 * and saves it back to disk.  In this way, the world constantly feels the
 * effects of weather uniformly, without relying on players wandering.
 *
 * The main point of this is stuff like growing herbs, soil, decaying crap,
 * etc etc etc.  Not actual *weather*, but weather *effects*.
 */
void perform_weather() {
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
static uint8_t wind_blow_object(mapstruct * const m, const int x, const int y, const MoveType move_type, int32_t wt, const living *stats) {
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
 * Section END -- weather effect methods
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
                    line = get_next_field(line);
                    if (line == NULL) {
                        LOG(llevError, "init_config_vals: Malformed name entry in %s, line %d.\n",
                            filename, bufferreader_current_line(bfr));
                        // Move on to the next line and hope it is fine.
                        continue;
                    }

                    found = sscanf(line, "%d, %d\n", &is_obj_name, &tree_count);
                    if (found != 2) {
                        // Print an error for the malformed line
                        LOG(llevError, "init_config_vals: Malformed forestry entry in %s, line %d.\n",
                            filename, bufferreader_current_line(bfr));
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
 * Load the weather/growth avoid defintions from file.
 *
 * @param settings
 * The settings structure we wish to use for install paths.
 * We want confdir specifically for this one.
 *
 * @param conf_filename
 * The name of the file we are loading.
 *
 * @param wa
 * pointer to a list to link elements of the weatheravoid to.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int init_weatheravoid(const Settings *settings, const char *conf_filename, weather_avoids_t **wa) {
    char filename[MAX_BUF], *line, *name;
    BufferReader *bfr;
    FILE *fp;
    int found, is_effect;

    snprintf(filename, sizeof(filename), "%s/%s", settings->confdir, conf_filename);
    // Open the file, then pass it off to the buffer reader.
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG(llevError, "init_weatheravoid: Could not open file %s. No weatheravoid data is defined.\n", filename);
        return 1;
    }
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
                // name, (1 if weather effect, 0 if regular tile)
                // [spaces are expected after commas]

                // sscanf on strings is wonky (it always reads to whitespace),
                // so I'm gonna do it by just nabbing part of the buffer.
                name = line; // Each line starts with name
                line = get_next_field(line);
                if (line == NULL) {
                    LOG(llevError, "init_weatheravoid: Malformed name entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr));
                    // Move on to the next line and hope it is fine.
                    continue;
                }

                found = sscanf(line, "%d\n", &is_effect);
                if (found != 1) {
                    // Print an error for the malformed line
                    LOG(llevError, "init_weatheravoid: Malformed effect flag entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr));
                }
                else {
                    // Add a struct to the list.
                    weather_avoids_t *frst = (weather_avoids_t *)malloc(sizeof(weather_avoids_t));
                    if (!frst) {
                        fatal(OUT_OF_MEMORY);
                    }
                    // Shared strings are friend, not food
                    frst->name = add_string(name);
                    frst->snow = is_effect;
                    // Attach to front of list, since order doesn't matter much, if at all.
                    frst->next = *wa;
                    *wa = frst;
                }
        }
    }
    bufferreader_destroy(bfr);
    return 0;
}

/**
 * Load the weather replacement definitions from file
 *
 * @param settings
 * The settings structure we wish to use for install paths.
 * We want confdir specifically for this one.
 *
 * @param conf_filename
 * The name of the file we are loading.
 *
 * @param list
 * pointer to a list to link elements of the weatheravoid to.
 *
 * @return
 * 0 if successful, 1 if failure.
 */
static int init_weather_replace(const Settings *settings, const char *conf_filename, weather_replace_t **list) {
    char filename[MAX_BUF], *line, *name, *repl, *doublestack;
    BufferReader *bfr;
    FILE *fp;
    int found, is_arch;

    snprintf(filename, sizeof(filename), "%s/%s", settings->confdir, conf_filename);
    // Open the file, then pass it off to the buffer reader.
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG(llevError, "init_weather_replace: Could not open file %s. No weather replace data is defined.\n", filename);
        return 1;
    }
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
                // name, replacement tile arch name, additional tile arch name (or NONE if not), (1 if arch name, 0 if object name)
                // [spaces are expected after commas]

                // sscanf on strings is wonky (it always reads to whitespace),
                // so I'm gonna do it by just nabbing part of the buffer.
                name = line; // Each line starts with name
                line = get_next_field(line);
                if (line == NULL) {
                    // Since we end up tokenizing the line strings, we can't reliably print it in the output.
                    LOG(llevError, "init_weather_replace: Malformed name entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr));
                    // Move on to the next line and hope it is fine.
                    continue;
                }

                repl = line; // Each line starts with name
                line = get_next_field(line);
                if (line == NULL) {
                    // Since we end up tokenizing the line strings, we can't reliably print it in the output.
                    LOG(llevError, "init_weather_replace: Malformed replacement entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr));
                    // Move on to the next line and hope it is fine.
                    continue;
                }

                doublestack = line; // Each line starts with name
                line = get_next_field(line);
                if (line == NULL) {
                    // Since we end up tokenizing the line strings, we can't reliably print it in the output.
                    LOG(llevError, "init_weather_replace: Malformed doublestack entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr));
                    // Move on to the next line and hope it is fine.
                    continue;
                }

                found = sscanf(line, "%d\n", &is_arch);
                if (found != 1) {
                    // Print an error for the malformed line
                    LOG(llevError, "init_weatheravoid: Malformed archetype/object flag entry in %s, line %d.\n",
                        filename, bufferreader_current_line(bfr), line);
                }
                else {
                    // Add a struct to the list.
                    weather_replace_t *frst = (weather_replace_t *)malloc(sizeof(weather_replace_t));
                    if (!frst) {
                        fatal(OUT_OF_MEMORY);
                    }
                    // Shared strings are friend, not food
                    frst->tile = add_string(name);
                    // Some replcement definitions can have NONE here to denote removal
                    if (strcmp(repl, "NONE") == 0)
                        frst->special_snow = NULL;
                    else
                        frst->special_snow = find_archetype(repl);
                    // if doublestack is NONE, then set the arch to NULL
                    if (strcmp(doublestack, "NONE") == 0)
                        frst->doublestack_arch = NULL;
                    else
                        frst->doublestack_arch = find_archetype(doublestack);
                    frst->arch_or_name = is_arch;
                    // Attach to front of list, since order doesn't matter much, if at all.
                    frst->next = *list;
                    *list = frst;
                }
        }
    }
    bufferreader_destroy(bfr);
    return 0;
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
static int load_humidity_map_part(mapstruct **m, const int dir, const int x, const int y, int * const tx, int * const ty) {
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
static int do_water_elev_calc(mapstruct * const m, const int x, const int y, int * const water, int64_t * const elev, int * const trees) {
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
    // Variable uses:
    // x, y: weathermap tile being affected
    // tx, ty: the in-map coordinates of the corner of the weathermap we are calculating.
    // nx, ny: coordinates within the weathermap
    // ax, ay: the location on the map we are examining
    // j: temporary variable for when a specific ny needs to be initialized from within a loop.
    int x, y, tx, ty, nx, ny, ax, ay, j;
    // spwtx, spwty: The number of tiles in a single weathermap in the associated (x or y) direction
    const int spwtx = (settings->worldmaptilesx*settings->worldmaptilesizex)/WEATHERMAPTILESX,
              spwty = (settings->worldmaptilesy*settings->worldmaptilesizey)/WEATHERMAPTILESY;
    int64_t elev;
    int water, space, trees;
    mapstruct *m;

    /* handling of this is kinda nasty.  For that reason,
     * we do the elevation here too.  Not because it makes the
     * code cleaner, or makes handling easier, but because I do *not*
     * want to maintain two of these nightmares.
     */

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
                    //LOG(llevInfo, "%s %d %d (8)->(%d.%d, %d.%d)\n", m->path, ax, ay, x, nx, y, ny);
                }
            }
            delete_map(m);


            // Sanely skip some processing if the entire weathermap fit on one world map.
            // Since we are the same size for x/y direction on both weathermaps and on world maps,
            // we will either need to load one map, two maps, or four maps. When two maps are loaded, it
            // will be one of bottom left or top right, since bottom right only is relevant when we intersect maps
            // in both x and y directions.
            if (space < spwtx*spwty) {
                // If we got all the way to the bottom on one map, don't even bother to load the map again.
                if (ny < spwty) {
                   /* bottom left */
                   if (load_humidity_map_part(&m, 6, x, y, &tx, &ty) == -1)
                        continue;

                    // If we get here, then we didn't have the whole weathermap reside on one map.
                    // Since we are continuing from top left, maintaining our position in the y direction
                    // allows us to correctly check when we reach the end of the weathermap bounds.
                    j = ny;
                    for (nx = 0, ax = tx; nx < spwtx && ax < settings->worldmaptilesizex && space < spwtx*spwty; ax++, nx++) {
                        for (ny = j, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                            do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                            //LOG(llevInfo, "%s %d %d (6)->(%d.%d, %d.%d)\n", m->path, ax, ay, x, nx, y, ny);
                        }
                    }
                    delete_map(m);
                }

                // If we gotall the way to the right on the left calculations, skip both right-side calculations.
                if (nx < spwtx) {
                    /* top right */
                    if (load_humidity_map_part(&m, 2, x, y, &tx, &ty) == -1)
                        continue;

                    for (ax = MAX(0, tx-(spwtx-1)); nx < spwtx && ax <= tx && space < spwtx*spwty; ax++, nx++) {
                        for (ny = 0, ay = ty; ny < spwty && ay < settings->worldmaptilesizey && space < spwtx*spwty; ay++, ny++, space++) {
                            do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                            //LOG(llevInfo, "%s %d %d (2)->(%d.%d, %d.%d)\n", m->path, ax, ay, x, nx, y, ny);
                        }
                    }
                    delete_map(m);

                    // If we got all the way to the bottom on one map, don't even bother to load the map again.
                    if (ny < spwty) {
                       /* bottom right */
                        if (load_humidity_map_part(&m, 4, x, y, &tx, &ty) == -1)
                            continue;

                        // Moving from top to bottom should behave the same on both right and left.
                        j = ny;
                        for (nx = 0, ax = MAX(0, tx - (spwtx-1)); nx < spwtx && ax <= tx && space < spwtx*spwty; ax++, nx++) {
                            for (ny = j, ay = MAX(0, ty-(spwty-1)); ny < spwty && ay <= ty && space < spwtx*spwty; space++, ay++, ny++) {
                                do_water_elev_calc(m, ax, ay, &water, &elev, &trees);
                                //LOG(llevInfo, "%s %d %d (4)->(%d.%d, %d.%d)\n", m->path, ax, ay, x, nx, y, ny);
                            }
                        }
                        delete_map(m);
                    }
                }
            }

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
static void init_wind() {
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
static void init_pressure() {
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
    // And the last line is the starting position, so we don't always have to initialize it.
    fprintf(fp, "%d\n", gulf_stream_start);
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

/**
 * Write the sky map. We never read this map, only write it for debugging purposes
 *
 * @return
 * 0 if successful, 1 otherwise
 */
int write_skymap(void) {
    char filename[MAX_BUF];
    FILE *fp;
    OutputFile of;
    int x, y;

    snprintf(filename, sizeof(filename), "%s/skymap", settings.localdir);
    fp = of_open(&of, filename);
    if (fp == NULL) {
        LOG(llevError, "Cannot open %s for writing\n", filename);
        return 1;
    }
    LOG(llevDebug, "Writing sky conditions map to file.\n");
    for (x = 0; x < WEATHERMAPTILESX; x++) {
        for (y = 0; y < WEATHERMAPTILESY; y++) {
            fprintf(fp, "%d ", weathermap[x][y].sky);
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
    // Minimum wind speed is always 0. Don't track it. We just define it so scale[4] is valid
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
/*          min[4] = MIN(min[4], weathermap[x][y].windspeed); */
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
    avgwind = (total_wind   /(WEATHERMAPTILESX*WEATHERMAPTILESY));
    max[2] = avgrain-1;
    realscalewind = 255.0l/(max[4]);
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
            // very high wind = red, else grey = average wind, dark = low wind
            if (speed < avgwind) {
                speed = (speed)*scale[4]/2;
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = speed;
            } else {
                speed = (speed-avgwind)*realscalewind/2;
                pixels[3*x+(1*WEATHERMAPTILESX*3+RED)] = (uint8_t)(MIN(255,(avgwind)*scale[4]/2+speed));
                pixels[3*x+(1*WEATHERMAPTILESX*3+GREEN)] = (avgwind)*scale[4]/2 - speed;
                pixels[3*x+(1*WEATHERMAPTILESX*3+BLUE)] = (avgwind)*scale[4]/2 - speed;
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
 * 0 if successful, -1 if failed.
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
                    return -1;
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
                    return -1;
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
    return -1;
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
 * 0 if successful, -1 if failure.
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
        return -1;
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
                return -1;
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
                return -1;
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
 * 0 if successful, -1 if failure.
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
        return -1;
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
                return -1;
            }
            // Range is -100 to 100 due to deserts and such being negative.
            weathermap[x][y].water = MAX(-100, MIN(100, wtr));
            // Adjust the data pointer.
            tmp = strpbrk(data, " \n");
            if (tmp == NULL) {
                LOG(llevError, "Unexpected end of file in water map.\n"
                    "Please delete %s/humidmap and restart your server to regenerate the water map.\n", settings->localdir);
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
    // Then we read in the start point, if it exists
    // For backward compatability, we randomly initialize this if we can't read it.
    res = sscanf(data, "%d\n", &in);
    if (res != 1) {
        LOG(llevInfo, "Gulf stream file lacks start position, and is assumed to be old; initializing it randomly.\n");
        in = rndm(GULF_STREAM_WIDTH, WEATHERMAPTILESY-GULF_STREAM_WIDTH);
    }
    gulf_stream_start = MAX(0, MIN(WEATHERMAPTILESY-GULF_STREAM_WIDTH, in));
    // If we add any more parsing here, we'll need to affect the data pointer.
    // But, since we don't, leave it stale until it falls out of scope.
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

/**
 * Read the weather map position from file.
 *
 * @param settings
 * A pointer to the settings structure.
 * We actually use a couple of these instead of just localdir.
 *
 * @return
 * 0 if successful, -1 if failed.
 * When failed, the map position fields that are set here will be set to
 * default values, so subsequent use will just result in a lost position.
 */
static int read_weatherposition(const Settings *settings) {
    char filename[MAX_BUF], *data;
    FILE *fp;
    BufferReader *bfr;
    int sx, sy, res;

    snprintf(filename, sizeof(filename), "%s/wmapcurpos", settings->localdir);
    LOG(llevDebug, "Reading current weather position from %s...\n", filename);
    if ((fp = fopen(filename, "r")) == NULL) {
        LOG(llevError, "Can't open %s.\n", filename);
        wmperformstartx = -1;
        wmperformstarty = 0;
        return 1;
    }
    bfr = bufferreader_create();
    bufferreader_init_from_file(bfr, fp);
    fclose(fp);
    data = bufferreader_data(bfr);
    // Read from the buffer
    res = sscanf(data, "%d %d", &sx, &sy);
    // Whether success or failure, we're done with the buffer.
    bufferreader_destroy(bfr);
    // Now we check our result.
    if (res != 2) {
        LOG(llevError, "Weather position file was malformed. Using default position.\n");
        wmperformstartx = -1;
        wmperformstarty = 0;
        return 1;
    }
    LOG(llevDebug, "curposx=%d curposy=%d\n", sx, sy);

    if (sx > settings->worldmaptilesx) {
        sx = -1;
    }
    if (sy > settings->worldmaptilesy) {
        sy = 0;
    }
    // Now we apply these to the static variables.
    wmperformstartx = sx;
    wmperformstarty = sy;
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
                /* call the weather calculators, here, in order */
                tick_weather();
                // At every hour, measure the rainfall.
                // pticks%PTICKS_PER_CLOCK is already triggering every hour.
                process_rain();
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

/**
 * Global object-process handling for weather.
 * @param type
 * The event type.
 * @param op
 * The object being processed.
 * @return
 * 0.
 */
static int weather_object_listener(int *type, ...) {
    va_list args;
    int code;
    object *op;

    va_start(args, type);
    code = va_arg(args, int);
    // At this point, we don't need the entering object for this.
    // but it is passed as an arg, so just skip it.
    op = va_arg(args, object *);

    va_end(args);

    switch (code) {
        case EVENT_TIME:
            // Have weather affect the position of the object. Do not blow the floors, though -- that is bad.
            if (op->map && !QUERY_FLAG(op, FLAG_IS_FLOOR)) {
                uint8_t dir = wind_blow_object(op->map, op->x, op->y, op->move_type, op->weight+op->carrying, &op->stats);
                mapstruct *m;
                int16_t x, y;
                // By checking only the head space, we can actually caue sailing galleons to irrecoverably crash ashore.
                // This is deliberate behavior.
                if (dir &&
                       // We will avoid pushing empty transports. Assume they are anchored/parked.
                       !(op->type == TRANSPORT && op->inv == NULL) &&
                       // If our player is in a transport, do not push the player
                       !(op->type == PLAYER && op->contr && op->contr->transport) &&
                       !(get_map_flags(op->map, &m, op->x+freearr_x[dir], op->y+freearr_y[dir], &x, &y)&P_OUT_OF_MAP) &&
                       !blocked_link(op, m, x, y)) {
                    object_remove(op);
                    object_insert_in_map_at(op, m, op, 0, x, y);

                    // Make sure to update the player view
                    if (op->type == PLAYER) {
                        esrv_map_scroll(&op->contr->socket, freearr_x[dir], freearr_y[dir]);
                        op->contr->socket.update_look = 1;
                        op->contr->socket.look_position = 0;
                    } else if (op->type == TRANSPORT) {
                        FOR_INV_PREPARE(op, pl)
                            if (pl->type == PLAYER) {
                                pl->contr->do_los = 1;
                                pl->map = op->map;
                                pl->x = op->x;
                                pl->y = op->y;
                                esrv_map_scroll(&pl->contr->socket, freearr_x[dir], freearr_y[dir]);
                                pl->contr->socket.update_look = 1;
                                pl->contr->socket.look_position = 0;
                            }
                        FOR_INV_FINISH();
                    }
                }
            }
    }
    return 0;
}
/********************************************************************************************
 * Section END -- weather event listeners
 ********************************************************************************************/

// Event handler ids start at 1, so 0 is an unset flag.
static event_registration global_map_handler = 0, global_clock_handler = 0, global_object_handler = 0;

/**
 * Weather module initialisation.
 * @param settings server settings.
 */
void cfweather_init(Settings *settings) {
    int x, tx, ty;
    /* all this stuff needs to be set, otherwise this function will cause
     * chaos and destruction.
     */
    if (settings->dynamiclevel < 1) {
        LOG(llevInfo, "cfweather_init: dynamic level set to %d. Not loading weather.\n", settings->dynamiclevel);
        return;
    }

    if (settings->worldmapstartx < 1 || settings->worldmapstarty < 1 ||
        settings->worldmaptilesx < 1 || settings->worldmaptilesy < 1 ||
        settings->worldmaptilesizex < 1 || settings->worldmaptilesizex < 1) {
        return;
    }
    // Initialize the forestry information from file.
    init_config_vals(settings, "treedefs", &forest_list);
    init_config_vals(settings, "waterdefs", &water_list);

    /*prepare structures used for avoidance*/
    init_weatheravoid(settings, "wavoiddefs", &weather_avoids);
    init_weatheravoid(settings, "gavoiddefs", &growth_avoids);

    init_weather_replace(settings, "wreplacedefs", &weather_replace);
    init_weather_replace(settings, "wevapdefs", &weather_evaporate);
    init_weather_replace(settings, "wmeltdefs", &weather_snowmelt);

    // Set up weathermap grid. This is needed for just about everything
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

    // Get current map position
    read_weatherposition(settings);

    // Connect the events after initialization, since we don't need to do
    // precipitation when we're initializing.
    global_map_handler = events_register_global_handler(EVENT_MAPENTER, weather_listener);
    global_clock_handler = events_register_global_handler(EVENT_CLOCK, weather_clock_listener);
    global_object_handler = events_register_global_handler(EVENT_TIME, weather_object_listener);
    /* Disable the plugin in case it's still there */
    linked_char *disable = (linked_char *)calloc(1, sizeof(linked_char));
    disable->next = settings->disabled_plugins;
    disable->name = strdup("cfweather");
    settings->disabled_plugins = disable;
}

void cfweather_close() {
    // Define temp pointers for clearing up the linked lists
    DensityConfig *cur;
    weather_avoids_t *avcur;
    weather_replace_t *rpcur;
    // Unregister handlers.
    if (global_map_handler != 0)
        events_unregister_global_handler(EVENT_MAPENTER, global_map_handler);
    if (global_clock_handler != 0)
        events_unregister_global_handler(EVENT_CLOCK, global_clock_handler);
    if (global_object_handler != 0)
        events_unregister_global_handler(EVENT_TIME, global_object_handler);
    // Free the weathermap
    for (int x = 0; x < WEATHERMAPTILESX; x++) {
        FREE_AND_CLEAR(weathermap[x]);
    }
    FREE_AND_CLEAR(weathermap);
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
    // Free our avoid lists
    while (weather_avoids != NULL) {
        avcur = weather_avoids;
        weather_avoids = weather_avoids->next;
        free_string(avcur->name);
        free(avcur);
    }
    while (growth_avoids != NULL) {
        avcur = growth_avoids;
        growth_avoids = growth_avoids->next;
        free_string(avcur->name);
        free(avcur);
    }
    // Free our replacement lists
    while (weather_replace != NULL) {
        rpcur = weather_replace;
        weather_replace = weather_replace->next;
        free_string(rpcur->tile);
        free(rpcur);
    }
    while (weather_evaporate != NULL) {
        rpcur = weather_evaporate;
        weather_evaporate = weather_evaporate->next;
        free_string(rpcur->tile);
        free(rpcur);
    }
    while (weather_snowmelt != NULL) {
        rpcur = weather_snowmelt;
        weather_snowmelt = weather_snowmelt->next;
        free_string(rpcur->tile);
        free(rpcur);
    }
}


/*@}*/
