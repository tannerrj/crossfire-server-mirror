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

/** List of all friendly objects, as weak references. */
static std::vector<object_ref> friends;

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

    friends.push_back(*op->self);
}

/**
 * Removes the specified object from the linked list of friendly objects.
 *
 * @param op
 * object to remove from list.
 */
void remove_friendly_object(object *op) {
    CLEAR_FLAG(op, FLAG_FRIENDLY);
    auto find = std::find_if(friends.begin(), friends.end(), [&] (auto item) { return item.lock().get() == op; });
    if (find != friends.end()) {
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
        auto l = item.lock();
        if (l) {
            LOG(llevError, "%s (%u)\n", l->name, l->count);
        }
    });
}

/**
 * It traverses the friendly list removing objects that should not be here
 * (ie, do not have friendly flag set, freed, etc)
 */
void clean_friendly_list(void) {
    int count = 0;

    auto weak = friends.begin();
    while (weak != friends.end()) {
        auto item = weak->lock();
        if (!item || item.get()->count == 0 || QUERY_FLAG(item.get(), FLAG_FREED) || !QUERY_FLAG(item.get(), FLAG_FRIENDLY)) {
            ++count;
            weak = friends.erase(weak);
        } else {
            ++weak;
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
    return std::find_if(friends.begin(), friends.end(), [&] (auto item) { return item.lock().get() == op; }) != friends.end();
}

/**
 * Get a list of friendly objects for the specified owner.
 * @param owner who to get objects of, may be NULL to get all friendly objects.
 * @return list of objects that must be freed with free_objectlink(), may be NULL.
 */
objectlink *get_friends_of(const object *owner) {
    objectlink *list = NULL;
    std::for_each(friends.begin(), friends.end(), [&] (auto ref) {
        auto item = ref.lock();
        if (item) {
            if (owner == NULL || object_get_owner(item.get()) == owner) {
                objectlink *add = get_objectlink();
                add->id = item->count;
                add->ob = item.get();
                add->next = list;
                list = add;
            }
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
        return friends.begin()->lock().get();
    };
    auto pos = std::find_if(friends.begin(), friends.end(), [&] (const auto item) { return item.lock().get() == current; });
    if (pos == friends.end()) {
        return nullptr;
    }
    pos++;
    return pos == friends.end() ? nullptr : (*pos).lock().get();
}
