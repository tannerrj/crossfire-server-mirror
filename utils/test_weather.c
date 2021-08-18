#include <stdio.h>

#include "global.h"

extern unsigned long todtick;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("usage: test_weather NUM_TICKS");
        return 1;
    }

    int num = atoi(argv[1]);

    init_library();
    load_settings(); // for worldmap coordinates
    settings.dynamiclevel = 2; // overwrite settings
    init_beforeplay();
    init_weather();
    init_modules(); // Abunch of weather code lives in the modules now.
    LOG(llevInfo, "Simulating weather for %d ticks: ", num);
    for (int i = 0; i < num; i++) {
        timeofday_t tod;
        todtick++;
        get_tod(&tod);
        tick_weather();
        compute_sky();
        if (tod.minute == 0) {
            process_rain();
        }
        write_weather_images();
        char filename[MAX_BUF];
        char filename2[MAX_BUF];
        snprintf(filename, sizeof(filename), "%s/weather.ppm", settings.localdir);
        snprintf(filename2, sizeof(filename2), "%s/weather.%.3d.ppm", settings.localdir, i);
        rename(filename, filename2);
        putchar('.');
    }
    putchar('\n');
}
