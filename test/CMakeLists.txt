add_definitions(-DSOURCE_ROOT="${CMAKE_SOURCE_DIR}/" -DLOGDIR="${CMAKE_CURRENT_BINARY_DIR}")
include_directories(${CHECK_INCLUDE_DIRS} include/)

add_library(test_common unit/common/stubs_common.cpp toolkit/toolkit_common.cpp)
function(add_common_test name)
    add_executable(${name} unit/common/${name}.cpp)
    target_link_libraries(${name} cross ${CHECK_LDFLAGS} test_common)
endfunction()

add_library(test_server unit/common/stubs_common.cpp toolkit/toolkit_common.cpp toolkit/toolkit_server.cpp)
function(add_server_test name)
    add_executable(${name} unit/server/${name}.cpp)
    target_link_libraries(${name} cross cfserver ${CHECK_LDFLAGS} test_server)
endfunction()
function(add_regression_test name)
    add_executable(${name} bugs/bugtrack/${name}.cpp)
    target_link_libraries(${name} cross cfserver ${CHECK_LDFLAGS} test_server)
endfunction()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK check REQUIRED)

set(COMMON_TESTS check_arch check_artifact check_friend check_holy check_image check_item check_living check_loader check_map check_minheap check_object check_path check_readable check_recipe check_region check_shstr check_stringbuffer check_time check_treasure check_utils check_asset_load_write)

set(SERVER_TESTS check_account check_account_char check_alchemy check_apply check_attack check_monster check_c_object comet_perf check_c_party check_shop check_swap check_monster_change check_player perf_tests)

set(REGRESSION_TESTS check_1727944 check_2879249 check_free_objects check_weight_reduction check_883 check_duplicate_skills)

include(CTest)
make_directory(${CMAKE_CURRENT_BINARY_DIR}/unit/common) # for test log output
foreach(test IN ITEMS ${COMMON_TESTS})
    add_common_test(${test})
    add_test(NAME ${test} COMMAND ./${test})
endforeach()

# This is a manual test executable, so build it separately without Check. Fixes CI build.
add_executable(check_mapsave unit/common/check_mapsave.cpp unit/common/stubs_common.cpp)
target_link_libraries(check_mapsave cfserver)

make_directory(${CMAKE_CURRENT_BINARY_DIR}/unit/server) # for test log output
foreach(test IN ITEMS ${SERVER_TESTS})
    add_server_test(${test})
    add_test(NAME ${test} COMMAND ./${test})
endforeach()

make_directory(${CMAKE_CURRENT_BINARY_DIR}/bugs/bugtrack) # for test log output
foreach(test IN ITEMS ${REGRESSION_TESTS})
    add_regression_test(${test})
    add_test(NAME ${test} COMMAND ./${test})
endforeach()
