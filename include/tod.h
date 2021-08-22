/**
 * @file
 * Defines for the ingame clock, ticks management and weather system.
 */

#ifndef TOD_H
#define TOD_H

#include "config.h"

/** Number of ticks per in-game hour. With defaults, 3 real-life minutes. */
#define PTICKS_PER_CLOCK        1500

/* game time */
#define HOURS_PER_DAY           28
#define DAYS_PER_WEEK           7
#define WEEKS_PER_MONTH         5
#define MONTHS_PER_YEAR         17
#define SEASONS_PER_YEAR        5
#define PERIODS_PER_DAY         6

/* convenience */
#define WEEKS_PER_YEAR          (WEEKS_PER_MONTH*MONTHS_PER_YEAR)
#define DAYS_PER_MONTH          (DAYS_PER_WEEK*WEEKS_PER_MONTH)
#define DAYS_PER_YEAR           (DAYS_PER_MONTH*MONTHS_PER_YEAR)
#define HOURS_PER_WEEK          (HOURS_PER_DAY*DAYS_PER_WEEK)
#define HOURS_PER_MONTH         (HOURS_PER_WEEK*WEEKS_PER_MONTH)
#define HOURS_PER_YEAR          (HOURS_PER_MONTH*MONTHS_PER_YEAR)

#define LUNAR_DAYS              DAYS_PER_MONTH

/** Game world time, in in-game hours. See PTICKS_PER_CLOCK. */
extern unsigned long todtick;

/**
 * Represents the ingame time.
 */
typedef struct _timeofday {
    int year;
    int month;
    int day;
    int dayofweek;
    int hour;
    int minute;
    int weekofmonth;
    int season;
    int periodofday;
} timeofday_t;

/* sky conditions */
#define SKY_CLEAR         0
#define SKY_LIGHTCLOUD    1
#define SKY_OVERCAST      2
#define SKY_LIGHT_RAIN    3
#define SKY_RAIN          4 /* rain -> storm has lightning */
#define SKY_HEAVY_RAIN    5
#define SKY_HURRICANE     6
/* wierd weather 7-12 */
#define SKY_FOG           7
#define SKY_HAIL          8
/* snow */
#define SKY_LIGHT_SNOW    13 /* add 10 to rain to get snow */
#define SKY_SNOW          14
#define SKY_HEAVY_SNOW    15
#define SKY_BLIZZARD      16

/* from common/time.c */
extern void get_tod(timeofday_t *tod);

/** Speed of an object that gives it one move per second, real time. */
static const float MOVE_PER_SECOND = MAX_TIME / 1000000.;

#endif /* TOD_H */
