/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 1999-2022 the Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

/**
 * @file
 * Functions related to relationship management.
 */

#include "global.h"

#include <stdlib.h>

/** List of all friendly objects, object and its count. */
static std::vector<std::pair<object *, tag_t>> friends;

/**
 * Add a new friendly object to the list of friendly objects.
 * Will log an error if the object is already on that list.
 *
 * @param op
 * object to add to the list.
 */
void add_friendly_object(object *op) {
    /* Add some error checking.  This shouldn't happen, but the friendly
     * object list usually isn't very long, and remove_friendly_object
     * won't remove it either.  Plus, it is easier to put a breakpoint in
     * the debugger here and see where the problem is happening.
     */
    if (is_friendly(op)) {
        LOG(llevError, "add_friendly_object: Trying to add object already on list (%s)\n", op->name);
        return;
    }

    friends.push_back(std::make_pair(op, op->count));
}

/**
 * Removes the specified object from the linked list of friendly objects.
 *
 * @param op
 * object to remove from list.
 */
void remove_friendly_object(object *op) {
    CLEAR_FLAG(op, FLAG_FRIENDLY);
    auto find = std::find_if(friends.begin(), friends.end(), [&] (auto item) { return item.first == op; });
    if (find != friends.end()) {
        if ((*find).second != op->count) {
            LOG(llevError, "remove_friendly_object, tags do no match, %s, %u != %u\n",
                op->name ? op->name : "none", op->count, (*find).second);
        }
        friends.erase(find);
    }
}

/**
 * Dumps all friendly objects.  Invoked in DM-mode with dumpfriendlyobjects command.
 *
 * @note
 * logs at the error level.
 */
void dump_friendly_objects(void) {
    std::for_each(friends.begin(), friends.end(), [] (const auto item) {
        LOG(llevError, "%s (%u)\n", item.first->name, item.second);
    });
}

/**
 * It traverses the friendly list removing objects that should not be here
 * (ie, do not have friendly flag set, freed, etc)
 */
void clean_friendly_list(void) {
    Profiler cfl("clean friendly list");
    int count = 0;

    auto item = friends.begin();
    while (item != friends.end()) {
        if (QUERY_FLAG((*item).first, FLAG_FREED)
        || !QUERY_FLAG((*item).first, FLAG_FRIENDLY)
        || ((*item).first->count != (*item).second)) {
            ++count;
            item = friends.erase(item);
        } else {
            ++item;
        }
    }

    friends.shrink_to_fit();
    if (count)
        LOG(llevDebug, "clean_friendly_list: Removed %d bogus links\n", count);
}

/**
 * Checks if the given object is already in the friendly list or not
 *
 * @param op
 * item to check
 * @return
 * 1 if on friendly list, 0 else
 */
int is_friendly(const object *op) {
    return std::find_if(friends.begin(), friends.end(), [&] (auto item) { return item.first == op; }) != friends.end();
}

/**
 * Get a list of friendly objects for the specified owner.
 * @param owner who to get objects of, may be NULL to get all friendly objects.
 * @return list of objects that must be freed with free_objectlink(), may be NULL.
 */
objectlink *get_friends_of(const object *owner) {
    objectlink *list = NULL;
    std::for_each(friends.begin(), friends.end(), [&] (auto item) {
        if (owner == NULL || object_get_owner(item.first) == owner) {
            objectlink *add = get_objectlink();
            add->id = item.second;
            add->ob = item.first;
            add->next = list;
            list = add;
        }
    });
    return list;
}

/**
 * Totally clear the friendly list.
 */
void clear_friendly_list(void) {
    friends.clear();
}

/**
 * Get the next object on the friendly list.
 * @param current current object, NULL to get the first item on the list.
 * @return next item, NULL if no more items or current isn't on the list.
 */
object *get_next_friend(object *current) {
    if (friends.empty()) {
        return nullptr;
    }
    if (current == nullptr) {
        return friends.begin()->first;
    };
    auto pos = std::find_if(friends.begin(), friends.end(), [&] (const auto item) { return item.first == current; });
    if (pos == friends.end()) {
        return nullptr;
    }
    pos++;
    return pos == friends.end() ? nullptr : (*pos).first;
}
