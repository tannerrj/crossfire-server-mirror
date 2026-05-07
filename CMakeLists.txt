cmake_minimum_required(VERSION 3.15...4.1)
project(crossfire LANGUAGES CXX)
set(VERSION 1.75.0)

option(METASERVER2 "Metaserver2 support (requires curl)" ON)
option(MAPPER "Build crossfire-mapper (requires gdlib)" ON)
option(TEST "Build tests (requires check)" OFF)
option(EXTRAS "Build extra utilities" OFF)
option(CRE "Build resource editor (requires Qt5)" OFF)

if(NOT WIN32)
    set(BUILD_SHARED_LIBS ON)
endif()

find_package(FLEX REQUIRED)

find_package(Python COMPONENTS Interpreter Development)

include_directories(include include/random_maps ${PROJECT_BINARY_DIR})
include(GNUInstallDirs)
add_compile_definitions(
    CONFDIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}"
    DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${CMAKE_PROJECT_NAME}"
    LIBDIR="${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_PROJECT_NAME}"
    LOCALDIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LOCALSTATEDIR}/${CMAKE_PROJECT_NAME}"
)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_PROJECT_NAME}")

# libcross {{{
add_custom_command(OUTPUT arch_types_valid.cpp
    COMMAND ${Python_EXECUTABLE}
    ARGS ${CMAKE_CURRENT_SOURCE_DIR}/common/is_valid_types_gen.py ${CMAKE_CURRENT_SOURCE_DIR}/include/object.h > ${CMAKE_CURRENT_BINARY_DIR}/arch_types_valid.cpp)

flex_target(LOADER common/loader.l loader.cpp COMPILE_FLAGS "-i -B -Cf")

add_library(cross
    ${FLEX_LOADER_OUTPUTS}
	arch_types_valid.cpp

	common/anim.cpp
	common/arch.cpp
	common/artifact.cpp
	common/assets/AnimationWriter.cpp
	common/assets/Animations.cpp
	common/assets/ArchetypeLoader.cpp
	common/assets/ArchetypeWriter.cpp
	common/assets/Archetypes.cpp
	common/assets/ArtifactLoader.cpp
	common/assets/ArtifactWriter.cpp
	common/assets/AssetCollector.cpp
	common/assets/AssetsManager.cpp
	common/assets/FaceLoader.cpp
	common/assets/FaceWriter.cpp
	common/assets/Faces.cpp
	common/assets/FacesetLoader.cpp
	common/assets/FacesetWriter.cpp
	common/assets/Facesets.cpp
	common/assets/FormulaeWriter.cpp
	common/assets/MessageLoader.cpp
	common/assets/MessageWriter.cpp
	common/assets/Messages.cpp
	common/assets/microtar.cpp
	common/assets/PngLoader.cpp
	common/assets/QuestLoader.cpp
	common/assets/QuestWriter.cpp
	common/assets/Quests.cpp
	common/assets/TarLoader.cpp
	common/assets/TreasureLoader.cpp
	common/assets/TreasureWriter.cpp
	common/assets/Treasures.cpp
	common/assets/Utils.cpp
	common/assets/assets.cpp
	common/bufferreader.cpp
	common/button.cpp
	common/cf_random.cpp
	common/dialog.cpp
	common/events.cpp
	common/exp.cpp
	common/friend.cpp
	common/holy.cpp
	common/info.cpp
	common/image.cpp
	common/init.cpp
	common/item.cpp
	common/languages.cpp
	common/links.cpp
	common/living.cpp
	common/logger.cpp
	common/los.cpp
	common/map.cpp
	common/minheap.cpp
	common/ob_methods.cpp
	common/ob_types.cpp
	common/object.cpp
	common/output_file.cpp
	common/path.cpp
	common/porting.cpp
	common/player.cpp
	common/re-cmp.cpp
	common/readable.cpp
	common/recipe.cpp
	common/region.cpp
	common/shstr.cpp
	common/stringbuffer.cpp
	common/time.cpp
	common/treasure.cpp
	common/utils.cpp
)
target_precompile_headers(cross PRIVATE include/global.h)
# }}}

# random maps {{{
flex_target(READER random_maps/reader.l reader.cpp
    COMPILE_FLAGS "-P rm"
)
add_library(random_map_lib
    ${FLEX_READER_OUTPUTS}
	random_maps/decor.cpp
	random_maps/door.cpp
	random_maps/exit.cpp
	random_maps/expand2x.cpp
	random_maps/floor.cpp
	random_maps/maze_gen.cpp
	random_maps/monster.cpp
	random_maps/random_map.cpp
	random_maps/rogue_layout.cpp
	random_maps/room_gen_onion.cpp
	random_maps/room_gen_spiral.cpp
	random_maps/snake.cpp
	random_maps/special.cpp
	random_maps/square_spiral.cpp
	random_maps/style.cpp
	random_maps/treasure.cpp
	random_maps/wall.cpp
	random_maps/room_gen_crawl.cpp
)
set_target_properties(random_map_lib PROPERTIES OUTPUT_NAME random_map)

target_precompile_headers(random_map_lib PRIVATE include/global.h)
target_link_libraries(random_map_lib PUBLIC cross)
add_executable(random_map random_maps/main.cpp)
target_link_libraries(random_map random_map_lib)
# }}}

# server {{{
add_library(cfserver
	server/account.cpp
	server/account_char.cpp
	server/alchemy.cpp
	server/apply.cpp
	server/attack.cpp
	server/ban.cpp
	server/build_map.cpp
	server/c_chat.cpp
	server/c_misc.cpp
	server/c_move.cpp
	server/c_new.cpp
	server/c_object.cpp
	server/c_party.cpp
	server/c_range.cpp
	server/c_wiz.cpp
	server/commands.cpp
	server/disease.cpp
	server/gods.cpp
	server/hiscore.cpp
	server/init.cpp
	server/knowledge.cpp
	server/login.cpp
	server/modules/cfcitybell.cpp
	server/modules/cfweather.cpp
	server/modules/citylife.cpp
	server/modules/random_house_generator.cpp
	server/monster.cpp
	server/move.cpp
	server/ob_methods.cpp
	server/ob_types.cpp
	server/party.cpp
	server/pets.cpp
	server/player.cpp
	server/plugins.cpp
	server/quest.cpp
	server/races.cpp
	server/resurrection.cpp
	server/rune.cpp
	server/server.cpp
	server/shop.cpp
	server/skill_util.cpp
	server/skills.cpp
	server/spell_attack.cpp
	server/spell_effect.cpp
	server/spell_util.cpp
	server/swap.cpp
	server/time.cpp
	server/timers.cpp
	server/weather.cpp
	server/win32.cpp
	socket/image.cpp
	socket/info.cpp
	socket/init.cpp
	socket/item.cpp
	socket/loop.cpp
	socket/lowlevel.cpp
	socket/metaserver.cpp
	socket/request.cpp
	socket/requestinfo.cpp
	socket/sounds.cpp
	types/altar/altar.cpp
	types/armour_improver/armour_improver.cpp
	types/arrow/arrow.cpp
	types/blindness/blindness.cpp
	types/book/book.cpp
	types/button/button.cpp
	types/cf_handle/cf_handle.cpp
	types/check_inv/check_inv.cpp
	types/clock/clock.cpp
	types/common/common_apply.cpp
	types/common/describe.cpp
	types/common/light_object.cpp
	types/common/projectile.cpp
	types/container/container.cpp
	types/converter/converter.cpp
	types/creator/creator.cpp
	types/deep_swamp/deep_swamp.cpp
	types/detector/detector.cpp
	types/director/director.cpp
	types/dragon_focus/dragon_focus.cpp
	types/duplicator/duplicator.cpp
	types/exit/exit.cpp
	types/food/food.cpp
	types/gate/gate.cpp
	types/hole/hole.cpp
	types/identify_altar/identify_altar.cpp
	types/lamp/lamp.cpp
	types/legacy/apply.cpp
	types/legacy/legacy_describe.cpp
	types/legacy/process.cpp
	types/lightable/lightable.cpp
	types/lighter/lighter.cpp
	types/marker/marker.cpp
	types/mimic/mimic.cpp
	types/mood_floor/mood_floor.cpp
	types/peacemaker/peacemaker.cpp
	types/pedestal/pedestal.cpp
	types/player_changer/player_changer.cpp
	types/player_mover/player_mover.cpp
	types/poison/poison.cpp
	types/poisoning/poisoning.cpp
	types/potion/potion.cpp
	types/power_crystal/power_crystal.cpp
	types/savebed/savebed.cpp
	types/scroll/scroll.cpp
	types/shop_inventory/shop_inventory.cpp
	types/shop_mat/shop_mat.cpp
	types/sign/sign.cpp
	types/skillscroll/skillscroll.cpp
	types/spell_effect/spell_effect.cpp
	types/spellbook/spellbook.cpp
	types/spinner/spinner.cpp
	types/teleporter/teleporter.cpp
	types/thrown_object/thrown_object.cpp
	types/transport/transport.cpp
	types/trap/common_trap.cpp
	types/trap/rune.cpp
	types/trap/trap.cpp
	types/trapdoor/trapdoor.cpp
	types/treasure/treasure.cpp
	types/trigger/trigger.cpp
	types/trigger_altar/trigger_altar.cpp
	types/trigger_button/trigger_button.cpp
	types/trigger_pedestal/trigger_pedestal.cpp
	types/weapon_improver/weapon_improver.cpp
)
add_executable(crossfire-server
    server/main.cpp
)
target_precompile_headers(cfserver PRIVATE include/global.h)
target_compile_definitions(cfserver PRIVATE PLUGIN_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
target_link_libraries(cfserver PUBLIC cross random_map_lib)
if (WIN32)
    target_link_libraries(cfserver PUBLIC ws2_32)
else()
    target_link_libraries(cfserver PUBLIC crypt)
endif()
target_link_libraries(crossfire-server cfserver)
install(TARGETS crossfire-server RUNTIME)
install(TARGETS cross cfserver random_map_lib DESTINATION ${CMAKE_INSTALL_LIBDIR}/${CMAKE_PROJECT_NAME})
install(DIRECTORY lib/config/ DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}/example)
install(DIRECTORY lib/i18n lib/help lib/wizhelp DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${CMAKE_PROJECT_NAME})

if(METASERVER2)
    find_package(CURL REQUIRED)
    set(HAVE_LIBCURL TYPE INTERNAL ${CURL_FOUND})
    target_include_directories(cfserver PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(cfserver PUBLIC ${CURL_LIBRARIES})

    find_package(Threads)
    target_link_libraries(cfserver PUBLIC Threads::Threads)
endif()
# }}}

# mapper {{{
if(MAPPER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GD gdlib REQUIRED)
    add_executable(crossfire-mapper utils/mapper/mapper.cpp)
    target_include_directories(crossfire-mapper PRIVATE utils/mapper ${GD_INCLUDE_DIRS})
    target_link_libraries(crossfire-mapper PUBLIC cross ${GD_LDFLAGS})
endif()
# }}}

if(EXTRAS)
    add_executable(bwp utils/bwp.cpp test/unit/common/stubs_common.cpp)
    target_link_libraries(bwp PUBLIC cross)

    add_executable(gridarta-types-convert utils/gridarta-types-convert.cpp)

    add_executable(test_weather utils/test_weather.cpp)
    target_link_libraries(test_weather PUBLIC cfserver)
endif()

# tests
if(TEST)
    add_subdirectory(test)
endif()

if(CRE)
    add_subdirectory(utils/cre)
endif()

include(CheckIncludeFiles)
check_include_files(crypt.h HAVE_CRYPT_H)
check_include_files(dirent.h HAVE_DIRENT_H)
check_include_files(gperftools/profiler.h HAVE_GPERFTOOLS_PROFILER_H)
check_include_files(ndir.h HAVE_NDIR_H)
check_include_files(sys/dir.h HAVE_SYS_DIR_H)
check_include_files(sys/ndir.h HAVE_SYS_NDIR_H)
check_include_files(syslog.h HAVE_SYSLOG_H)

include(CheckFunctionExists)

function(AC_CHECK_FUNCS)
    foreach(ARG ${ARGV})
        string(TOUPPER ${ARG} ARG_UPPER)
        check_function_exists(${ARG} HAVE_${ARG_UPPER})
    endforeach()
endfunction()

AC_CHECK_FUNCS(getdtablesize strcasecmp strcasestr strdup strncasecmp sysconf tempnam getaddrinfo getnameinfo)
AC_CHECK_FUNCS(strlcat strlcpy)

configure_file("include/cmake.h.in" "autoconf.h")

find_package(Git)
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags
    OUTPUT_VARIABLE SVN_REV
    RESULT_VARIABLE result
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(result EQUAL 0)
    message(STATUS "Last Git commit: ${SVN_REV}")
  endif()
endif()
configure_file("include/svnversion.h.in" "svnversion.h")

configure_file("utils/crossfire-server-local.in" "crossfire-server-local")

add_subdirectory(plugins/cfpython)
