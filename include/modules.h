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
 * @file
 * List all module initialisation functions.
 */

#ifndef MODULES_H
#define MODULES_H

enum StartupStage {
    STARTUP_STAGE_FIRST = 0,            /**< Dummy stage, don't change, used for logging. */
    STARTUP_STAGE_COLLECT_HOOKS = 0,    /**< Called when adding collect hooks, before assets loading. */
    STARTUP_STAGE_BEFORE_SERVER,        /**< Called when everything is loaded but before the sockets are opened. */
};

void cfcitybell_init(Settings *settings, ServerSettings *serverSettings, StartupStage stage);
void cfcitybell_close();

void citylife_init(Settings *settings, ServerSettings *serverSettings, StartupStage stage);
void citylife_close();

void random_house_generator_init(Settings *settings, ServerSettings *serverSettings, StartupStage stage);
void random_house_generator_close();

void cfweather_init(Settings *settings, ServerSettings *servserSettings, StartupStage stage);
void cfweather_close();
#endif /* MODULES_H */
