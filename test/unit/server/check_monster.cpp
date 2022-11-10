/*
 * static char *rcsid_check_monster_c =
 *   "$Id$";
 */

/*
 * CrossFire, A Multiplayer game for X-windows
 *
 * Copyright (C) 2002 Mark Wedel & Crossfire Development Team
 * Copyright (C) 1992 Frank Tore Johansen
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
 * This is the unit tests file for server/monster.c
 */

#include <stdlib.h>
#include <check.h>

#include <global.h>
#include <sproto.h>
#include <toolkit_common.h>

void setup(void) {
}

void teardown(void) {
}

START_TEST(test_monster_find_nearest_enemy) {
    object *first, *second, *third, *owner, *found;
    player pl;
    socket_struct sock;

    // The function randomizes directions, so check 200 times to be safe.

    memset(&pl, 0, sizeof(pl));
    memset(&sock, 0, sizeof(sock));
    pl.socket = &sock;

    mapstruct *map = get_empty_map(3, 3);

    first = create_archetype("kobold");
    fail_unless(QUERY_FLAG(first, FLAG_MONSTER));
    object_insert_in_map_at(first, map, NULL, 0, 1, 1);

    for (uint8_t i = 0; i < 200; i++) {
        fail_unless(monster_find_nearest_enemy(first, NULL) == NULL, "Found something when nothing?");
    }

    second = create_archetype("kobold");
    object_insert_in_map_at(second, map, NULL, 0, 0, 1);
    for (uint8_t i = 0; i < 200; i++) {
        fail_unless(monster_find_nearest_enemy(first, NULL) == second, "Didn't find second monster?");
    }

    owner = create_archetype("dwarf_player");
    owner->contr = &pl;
    object_insert_in_map_at(owner, map, NULL, 0, 0, 0);
    object_set_owner(first, owner);
    for (uint8_t i = 0; i < 200; i++) {
        found = monster_find_nearest_enemy(first, owner);
        fail_if(found == owner, "Found owner?");
        fail_unless(found == second, "Should find second!");
    }

    object_set_owner(second, owner);
    for (uint8_t i = 0; i < 200; i++) {
        found = monster_find_nearest_enemy(first, owner);
        fail_if(found == owner, "Found owner?");
        fail_unless(found == NULL, "Shouldn't find anything since both are pets");
    }

    pl.petmode = pet_sad;
    for (uint8_t i = 0; i < 200; i++) {
        found = monster_find_nearest_enemy(first, owner);
        fail_unless(found == NULL, "Pets shouldn't attack other pets");
    }

    third = create_archetype("kobold");
    object_insert_in_map_at(third, map, NULL, 0, 1, 0);
    for (uint8_t i = 0; i < 200; i++) {
        found = monster_find_nearest_enemy(first, owner);
        fail_unless(found == third, "Should find third monster");
    }

    uint8_t co = 0, cs = 0, ct = 0;
    for (uint16_t i = 0; i < 2000; i++) {
        found = monster_find_nearest_enemy(first, NULL);
        fail_unless(found, "Should find a target!");
        if (found == owner) {
            co++;
        } else if (found == second) {
            cs++;
        } else {
            ct++;
        }
    }
    fail_unless(co != 0, "Should have found the owner");
    fail_unless(cs != 0, "Should have found second");
    fail_unless(ct != 0, "Should have found third");
}
END_TEST

Suite *monster_suite(void) {
    Suite *s = suite_create("monster");
    TCase *tc_core = tcase_create("Core");

    /*setup and teardown will be called before each test in testcase 'tc_core' */
    tcase_add_checked_fixture(tc_core, setup, teardown);

    suite_add_tcase(s, tc_core);
    tcase_add_test(tc_core, test_monster_find_nearest_enemy);

    return s;
}

int main(void) {
    int nf;

    cctk_setdatadir(SOURCE_ROOT "lib");
    init(0, NULL);

    Suite *s = monster_suite();
    SRunner *sr = srunner_create(s);

    // Uncomment to debug
    // srunner_set_fork_status(sr, CK_NOFORK);

    srunner_set_xml(sr, LOGDIR "/unit/server/monster.xml");
    srunner_set_log(sr, LOGDIR "/unit/server/monster.out");
    srunner_run_all(sr, CK_ENV); /*verbosity from env variable*/
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
