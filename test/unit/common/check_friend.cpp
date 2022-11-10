/*
 * static char *rcsid_check_friend_c =
 *   "$Id$";
 */

/*
 * CrossFire, A Multiplayer game for X-windows
 *
 * Copyright (C) 2022 the Crossfire Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * The authors can be reached via e-mail at crossfire-devel@real-time.com
 */

/*
 * This is the unit tests file for common/friend.cpp
 */

#include <stdlib.h>
#include <check.h>

#include "global.h"
#include "libproto.h"

void setup(void) {
}

void teardown(void) {
    clear_friendly_list();
}

START_TEST(test_basic) {
    object *ob = object_new();
    SET_FLAG(ob, FLAG_FRIENDLY);
    add_friendly_object(ob);
    fail_unless(is_friendly(ob), "Object must be friendly");
    remove_friendly_object(ob);
    fail_unless(!is_friendly(ob), "Object must not be friendly");
    fail_unless(!QUERY_FLAG(ob, FLAG_FRIENDLY), "Object must not have FLAG_FRIENDLY");
}
END_TEST

START_TEST(test_clean_friendly_list_count) {
    object *ob = object_new();
    add_friendly_object(ob);
    ob->count = 0;
    clean_friendly_list();
    fail_unless(!is_friendly(ob), "Object must not be friendly");
}
END_TEST

START_TEST(test_clean_friendly_list_freed) {
    object *ob = object_new();
    add_friendly_object(ob);
    SET_FLAG(ob, FLAG_FREED);
    clean_friendly_list();
    fail_unless(!is_friendly(ob), "Object must not be friendly");
}
END_TEST

START_TEST(test_clean_friendly_list_not_friendly) {
    object *ob = object_new();
    add_friendly_object(ob);
    CLEAR_FLAG(ob, FLAG_FRIENDLY);
    clean_friendly_list();
    fail_unless(!is_friendly(ob), "Object must not be friendly");
}
END_TEST

START_TEST(test_get_friends_of_null) {
    object *ob = object_new(), *second = object_new();
    add_friendly_object(ob);
    add_friendly_object(second);
    objectlink *list = get_friends_of(NULL);
    fail_unless(list, "Must get a list");
    // List is inverted because of its construction
    fail_unless(list->ob == second, "Second must be first in list");
    fail_unless(list->next != NULL, "List must have a second item");
    fail_unless(list->next->ob == ob, "Ob must be the second on the list");
    fail_unless(list->next->next == NULL, "List must not have a third item");
}
END_TEST

START_TEST(test_get_friends_of_owner) {
    object *ob = object_new(), *owner = object_new();
    CLEAR_FLAG(owner, FLAG_REMOVED);
    object_set_owner(ob, owner);
    fail_unless(ob->owner && ob->owner->lock().get() == owner);
    add_friendly_object(ob);
    objectlink *list = get_friends_of(owner);
    fail_unless(list, "Must get a list of owner's friends");
    fail_unless(list->ob == ob, "Ob Must be first in list");
}
END_TEST

START_TEST(test_get_friends_of_owner_only) {
    object *ob = object_new(), *owner = object_new(), *second = object_new();
    CLEAR_FLAG(owner, FLAG_REMOVED);
    object_set_owner(ob, owner);
    fail_unless(ob->owner && ob->owner->lock().get() == owner);
    add_friendly_object(ob);
    add_friendly_object(second);
    objectlink *list = get_friends_of(owner);
    fail_unless(list, "Must get a list of owner's friends");
    fail_unless(list->ob == ob, "Ob Must be first in list");
    fail_unless(list->next == NULL, "There must be only one item on the list");
}
END_TEST

START_TEST(test_get_next_friend_none) {
    fail_unless(get_next_friend(NULL) == NULL, "Must get NULL");
}
END_TEST

START_TEST(test_get_next_friend_single) {
    object *ob = object_new();
    add_friendly_object(ob);
    fail_unless(get_next_friend(NULL) == ob, "Must get ob");
    fail_unless(get_next_friend(ob) == NULL, "Must get NULL, last friend");
}
END_TEST

START_TEST(test_get_next_friend_two) {
    object *ob = object_new(), *second = object_new();
    add_friendly_object(ob);
    add_friendly_object(second);
    fail_unless(get_next_friend(NULL) == ob, "Must get ob");
    fail_unless(get_next_friend(ob) == second, "Must get second");
    fail_unless(get_next_friend(second) == NULL, "Must get NULL");
}
END_TEST

Suite *friend_suite(void) {
    Suite *s = suite_create("friend");
    TCase *tc_core = tcase_create("Core");

    /*setup and teardown will be called before each test in testcase 'tc_core' */
    tcase_add_checked_fixture(tc_core, setup, teardown);

    suite_add_tcase(s, tc_core);
    tcase_add_test(tc_core, test_basic);
    tcase_add_test(tc_core, test_clean_friendly_list_count);
    tcase_add_test(tc_core, test_clean_friendly_list_freed);
    tcase_add_test(tc_core, test_clean_friendly_list_not_friendly);
    tcase_add_test(tc_core, test_get_friends_of_null);
    tcase_add_test(tc_core, test_get_friends_of_owner);
    tcase_add_test(tc_core, test_get_friends_of_owner_only);
    tcase_add_test(tc_core, test_get_next_friend_none);
    tcase_add_test(tc_core, test_get_next_friend_single);
    tcase_add_test(tc_core, test_get_next_friend_two);

    return s;
}

int main(void) {
    int nf;
    Suite *s = friend_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_set_xml(sr, LOGDIR "/unit/common/friend.xml");
    srunner_set_log(sr, LOGDIR "/unit/common/friend.out");
    srunner_run_all(sr, CK_ENV); /*verbosity from env variable*/
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
