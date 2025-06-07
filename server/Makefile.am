bin_PROGRAMS = crossfire-server

noinst_LIBRARIES = libserver.a
crossfire_server_SOURCES = main.cpp
libserver_a_SOURCES = \
	account.cpp \
	account_char.cpp \
	alchemy.cpp \
	apply.cpp \
	attack.cpp \
	ban.cpp \
	build_map.cpp \
	c_chat.cpp \
	c_misc.cpp \
	c_move.cpp \
	c_new.cpp \
	c_object.cpp \
	c_party.cpp \
	c_range.cpp \
	c_wiz.cpp \
	commands.cpp \
	disease.cpp \
	hiscore.cpp \
	gods.cpp \
	init.cpp \
	knowledge.cpp \
	login.cpp \
	monster.cpp \
	move.cpp \
	ob_methods.cpp \
	ob_types.cpp \
	party.cpp \
	pets.cpp \
	player.cpp \
	plugins.cpp \
	quest.cpp \
	races.cpp \
	resurrection.cpp \
	rune.cpp \
	shop.cpp \
	skills.cpp \
	skill_util.cpp \
	spell_attack.cpp \
	spell_effect.cpp \
	spell_util.cpp \
	swap.cpp \
	time.cpp \
	timers.cpp \
	weather.cpp \
	server.cpp \
	modules/cfcitybell.cpp \
	modules/citylife.cpp \
	modules/random_house_generator.cpp \
	win32.cpp

AM_CPPFLAGS = -I$(top_srcdir)/include -DDATADIR=\"$(pkgdatadir)\" -DCONFDIR=\"$(pkgconfdir)\" \
	      -DLIBDIR=\"$(pkglibdir)\" -DLOCALDIR=\"$(pkgstatedir)\" \
	      -DPLUGIN_SUFFIX=\".so\" $(ECL_CFLAGS)

crossfire_server_LDFLAGS=$(ECL_LDFLAGS) $(LIBDL_LDFLAGS)

crossfire_server_LDADD = libserver_lisp.a libserver.a ../common/libcross.a ../random_maps/librandom_map.a \
		../socket/libsocket.a ../types/libtypes.a

libserver_lisp.a: server_lisp.lisp
	$(ECL) -norc --shell build.lisp

../common/libcross.a:
	( cd ../common ; make libcross.a )

../random_maps/librandom_map.a:
	( cd ../random_maps ; make librandom_map.a )

../socket/libsocket.a:
	( cd ../socket ; make libsocket.a )

../types/libtypes.a:
	( cd ../types ; make libtypes.a )
