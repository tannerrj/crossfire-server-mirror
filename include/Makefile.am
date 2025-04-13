noinst_HEADERS = \
commands.h     libproto.h     sounds.h	     attack.h       config.h \
living.h       party.h        output_file.h  \
player.h       define.h       dialog.h       loader.h \
material.h     spells.h       face.h         logger.h \
re-cmp.h       sproto.h       artifact.h     map.h          recipe.h \
treasure.h     global.h       path.h         shstr.h        stringbuffer.h \
tod.h          god.h          newserver.h    book.h \
object.h       skills.h	    sockproto.h     plugin.h \
timers.h       typesproto.h   ob_methods.h   ob_types.h     svnversion.h \
version.h      shared/newclient.h            account_char.h \
image.h		  metaserver2.h \
random_maps/expand2x.h \
random_maps/maze_gen.h \
random_maps/random_map.h \
random_maps/room_gen.h \
random_maps/rproto.h \
compat.h \
shop.h \
server.h \
cf_profiling.h

BUILT_SOURCES = svnversion.h

CLEANFILES = svnversion.h

svnversion.h: FORCE
	OUTPUT_DATA='/* Auto-generated at build time. */'; \
	if test -n "$(GIT)" -a -d "$(top_srcdir)/.git"; \
	then VER_STR=`$(GIT) -C "$(top_srcdir)" describe --tags`; OUTPUT_DATA=`echo "$$OUTPUT_DATA"; echo "#define SVN_REV \"$$VER_STR\""`; \
	else OUTPUT_DATA=`echo "$$OUTPUT_DATA"; echo '#define SVN_REV "unknown"'`; \
	fi; \
	if test ! -e svnversion.h; then \
		echo "$$OUTPUT_DATA" > svnversion.h; \
	elif test "$$OUTPUT_DATA" != "`cat svnversion.h`"; then \
		echo "$$OUTPUT_DATA" > svnversion.h; \
	fi

FORCE:

