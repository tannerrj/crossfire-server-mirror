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
 * This is the unit tests file for common/artifact.c
 */

#include <global.h>
#include <stdlib.h>
#include <check.h>
#include <loader.h>
#include <toolkit_common.h>

static void setup(void) {
    cctk_setdatadir(SOURCE_ROOT"lib");
    cctk_setlog(LOGDIR"/unit/common/artifact.out");
    cctk_init_std_archetypes();
}

static void teardown(void) {
    /* put any cleanup steps here, they will be run after each testcase */
}

/** Ensure there is a face available for each artifact */
START_TEST(test_face_for_each_artifact) {
    const artifactlist *al;
    const artifact *art;
    al = first_artifactlist;
    while (al) {
        art = al->items;
        while (art) {
            fail_unless(artifact_get_face(art) != (uint16_t)-1, "failed to find a face for %s", art->item->name);
            art = art->next;
        }
        al = al->next;
    }
}
END_TEST

static Suite *artifact_suite(void) {
    Suite *s = suite_create("artifact");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 60);

    /*setup and teardown will be called before each test in testcase 'tc_core' */
    tcase_add_checked_fixture(tc_core, setup, teardown);

    suite_add_tcase(s, tc_core);
    tcase_add_test(tc_core, test_face_for_each_artifact);

    return s;
}

int main(void) {
    int nf;
    Suite *s = artifact_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, LOGDIR "/unit/common/artifact.xml");
    srunner_set_log(sr, LOGDIR "/unit/common/artifact.out");
    srunner_run_all(sr, CK_ENV); /*verbosity from env variable*/
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
