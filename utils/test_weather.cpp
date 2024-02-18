#include <stdio.h>

#include "global.h"
#include "sproto.h"

extern unsigned long todtick;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("usage: test_weather NUM_TICKS");
        return 1;
    }

    int num = atoi(argv[1]);

    init(argc, argv);
    // With weather settings separate from the main settings,
    // there is not a good way to override this.
    // It is expected that we have dynamiclevel 2 or higher here.
    //settings.dynamiclevel = 2; // overwrite settings
    LOG(llevInfo, "Simulating weather for %d ticks: ", num);
    for (int i = 0; i < num; i++) {
        todtick++;
        tick_weather();
        process_rain();
// Swap this toggle to have it perform weather effects as well.
// When on, it will run *much* slower, but will be the most accurate,
// Since a few things like evaporation of puddles does affect the weather.
#if 0
        perform_weather();
#else
        // If we call compute_weather(), it calls compute_sky() already, so we only need to call
        // it if we aren't calling compute_weather.
        compute_sky();
#endif
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
