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
#include "object.h"

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

    snprintf(filename, sizeof(filename), "%s/treedefs", settings.confdir);
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
                    found = sscanf(line, "%s, %d, %d", &name, &is_obj_name, &tree_count);
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
    snprintf(filename, sizeof(filename), "%s/treemap", settings.localdir);
    // We use the output_file handling for atomic file operations.
    fp = of_open(&of, filename);
    if (fp != NULL) {
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
    LOG(llevError, "Failed to open forestry file for writing.\n");
    return 1;
}

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

    snprintf(filename, sizeof(filename), "%s/treemap", settings.localdir);
    LOG(llevDebug, "Reading forestry data from %s...\n", filename);
    fp = fopen(filename, "r");
    if (fp != NULL) {
        // Set up the bufferreader and read in the file.
        // We do it through the bufferreader so that we only dip into I/O once,
        // and the rest is just parsing it in memory.
        bfr = buffereader_create();
        buffereader_init_from_file(bfr, fp);
        fclose(fp);
        // Parse the file. Since this is auto-generated by the weather system,
        // just bail if the file is malformed.
        data = bufferreader_data(bfr);
        for (x = 0; x < WEATHERMAPTILESX; ++x) {
            for (y = 0; y < WEATHERMAPTILESY; ++y) {
                res = sscanf(data, "%d ", &trees);
                if (res != 1) {
                    LOG(llevError, "Forestry data is corrupted and should be regenerated.\n"
                        "Please delete %s/humidmap and restart the server at your earliest convenience to regenerate the forestry map.\n", settings.localdir);
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
                        "Please delete %s/humidmap and restart the server at your earliest convenience to regenerate the forestry map.\n", settings.localdir);
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

static event_registration global_handler;

/**
 * Weather module initialisation.
 * @param settings server settings.
 */
void cfweather_init(Settings *settings) {
    // Initialize the forestry information from file.
    init_forestry_vals(settings);
    // Trees help stabilize local temperature and evaporate water from deeper underground.
    // This is calculated at the same time as elevation and humidity.
    read_forestrymap(settings);

    // Connect the event after initialization, since we don't need to do
    // precipitation when we're initializing.
    global_handler = events_register_global_handler(EVENT_MAPENTER, weather_listener);
    /* Disable the plugin in case it's still there */
    linked_char *disable = (linked_char *)calloc(1, sizeof(linked_char));
    disable->next = settings->disabled_plugins;
    disable->name = strdup("cfweather");
    settings->disabled_plugins = disable;
}

void cfweather_close() {
    struct forestry *cur;
    events_unregister_global_handler(EVENT_MAPENTER, global_handler);
    // Deallocate our linked list of forest entries.
    while (forest_list != NULL) {
        cur = forest_list;
        forest_list = forest_list->next;
        free_string(cur->name);
        free(cur);
    }
}


/*@}*/
