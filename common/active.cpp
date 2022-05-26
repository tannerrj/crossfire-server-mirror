/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 1999-2022 Mark Wedel and the Crossfire Development Team
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
 * Active list
 */

extern "C" {
#include "global.h"
}

#include <cassert>
#include <cmath>
#include <map>
#include <queue>

static bool object_in_icecube(object *op) {
    return op->env != NULL && strcmp(op->env->arch->name, "icecube") == 0;
}

static int numerator(object *ob) {
    if (object_in_icecube(ob)) {
        return 10;
    }
    return 1;
}

class ActiveObject {
    private:
        float incr;

    public:
        uint32_t tick_added, next_tick;
        object *ob;

    ActiveObject(object *o) {
        ob = o;
        assert(FABS(ob->speed) > MIN_ACTIVE_SPEED); // otherwise it wouldn't be active_add()'ed
        // The earliest that the event can fire is the tick after the next (2)
        uint32_t nticks = MAX(2, ceil(.5 + -ob->speed_left * numerator(ob) / FABS(ob->speed)));
        tick_added = pticks;
        next_tick = pticks + nticks;
        incr = nticks * FABS(ob->speed) / numerator(ob);
        assert(really_ready());
    }

    bool operator < (const ActiveObject& rhs) const {
        return next_tick > rhs.next_tick;
    }

    bool ready() const {
        return pticks >= next_tick;
    }

    bool really_ready() const {
        return ob->speed_left + incr > 0;
    }

    void update() const {
        ob->speed_left += incr - numerator(ob);
    }
};

static std::map<tag_t, ActiveObject> active_objects; //< for querying active list membership
static std::priority_queue<ActiveObject> queue; //< for ordering events

extern "C" {
    void active_add(object *ob) {
        auto ao = ActiveObject(ob);
        auto result = active_objects.insert(std::pair<tag_t, ActiveObject>(ob->count, ao));
        // Only add to queue if not already there.
        if (result.second) {
            queue.push(ao);
        }
    }

    void active_remove(object *ob) {
        auto ao = active_objects.find(ob->count)->second;
        // Update speed_left so that it can be saved when maps are swapped.
        ob->speed_left += (pticks - ao.tick_added) * FABS(ob->speed);
        active_objects.erase(ob->count);
    }

    int active_count() {
        return active_objects.size();
    }

    bool object_is_active(object *ob) {
        return active_objects.find(ob->count) != active_objects.end();
    }

    bool active_has_next() {
        while (!queue.empty() && queue.top().ready()) {
            auto top = queue.top();
            if (!object_is_active(top.ob)) {
                // Check if the object has already been removed.
                queue.pop();
            } else if (!top.really_ready()) {
                // Someone may have fiddled with speed_left. In that case, pop it
                // and push it back with an updated next_tick.
                queue.pop();
                top.update();
                queue.push(ActiveObject(top.ob));
            } else {
                return true;
            }
        }

        return false;
    }

    object *active_next() {
        auto top = queue.top();
        assert(top.really_ready()); // ensured by call to active_has_next()
        active_objects.erase(top.ob->count);
        object *next = top.ob;
        queue.pop();
        if (next->type != PLAYER) {
            top.update();
        }
        return next;
    }
}
