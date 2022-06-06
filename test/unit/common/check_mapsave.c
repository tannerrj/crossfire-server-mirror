#include "global.h"

extern int save_objects(mapstruct *m, FILE *fp, FILE *fp2, int flag);
extern void allocate_map(mapstruct *m);
extern void load_objects(mapstruct *m, FILE *fp, int mapflags);

int mapsave_test(const char *map) {
    int flags = 0;
    if (map[0] == '~') {
        flags |= MAP_PLAYER_UNIQUE;
    }

    // Load map file to get objects
    mapstruct *m = mapfile_load(map, flags);
    if (m == NULL) {
        return 1;
    }

    // Save objects (without map header)
    FILE *fp1 = fopen("check_mapsave_1.map", "w");
    m->in_memory = MAP_SAVING;
    save_objects(m, fp1, fp1, 0);
    int width = m->width;
    int height = m->height;
    free_map(m);
    fclose(fp1);

    // Load objects from saved map
    fp1 = fopen("check_mapsave_1.map", "r");
    m = map_new();
    map_add(m);
    m->width = width;
    m->height = height;
    allocate_map(m);
    m->in_memory = MAP_LOADING;
    load_objects(m, fp1, flags & MAP_STYLE);
    fclose(fp1);

    // Save loaded objects out to different file
    FILE *fp2 = fopen("check_mapsave_2.map", "w");
    save_objects(m, fp2, fp2, 0);
    fclose(fp2);

    // Check that file1 and file2 are the same
    return system("cmp check_mapsave_1.map check_mapsave_2.map");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("usage: check_mapsave PATH");
        return 0;
    }

    settings.debug = llevDebug;
    init_library();
    return mapsave_test(argv[1]);
}
