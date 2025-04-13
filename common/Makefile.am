noinst_LIBRARIES = libcross.a

libcross_a_SOURCES = \
	anim.cpp \
	arch.cpp\
	arch_types_valid.cpp \
	artifact.cpp \
	assets/AnimationWriter.cpp \
	assets/Animations.cpp \
	assets/ArchetypeLoader.cpp \
	assets/ArchetypeWriter.cpp \
	assets/Archetypes.cpp \
	assets/ArtifactLoader.cpp \
	assets/ArtifactWriter.cpp \
	assets/AssetCollector.cpp \
	assets/AssetsManager.cpp \
	assets/FaceLoader.cpp \
	assets/FaceWriter.cpp \
	assets/Faces.cpp \
	assets/FacesetLoader.cpp \
	assets/FacesetWriter.cpp \
	assets/Facesets.cpp \
	assets/FormulaeWriter.cpp \
	assets/MessageLoader.cpp \
	assets/MessageWriter.cpp \
	assets/Messages.cpp \
	assets/microtar.cpp \
	assets/PngLoader.cpp \
	assets/QuestLoader.cpp \
	assets/QuestWriter.cpp \
	assets/Quests.cpp \
	assets/TarLoader.cpp \
	assets/TreasureLoader.cpp \
	assets/TreasureWriter.cpp \
	assets/Treasures.cpp \
	assets/Utils.cpp \
	assets/assets.cpp\
	bufferreader.cpp \
	button.cpp \
	cf_random.cpp \
	dialog.cpp \
	events.cpp \
	exp.cpp \
	friend.cpp \
	holy.cpp \
	info.cpp \
	image.cpp \
	init.cpp \
	item.cpp \
	languages.cpp \
	links.cpp \
	living.cpp \
	loader.cpp \
	logger.cpp \
	los.cpp \
	map.cpp \
	minheap.cpp \
	ob_methods.cpp \
	ob_types.cpp \
	object.cpp \
	output_file.cpp \
	path.cpp \
	porting.cpp \
	player.cpp \
	cf_profiling.cpp \
	re-cmp.cpp \
	readable.cpp \
	recipe.cpp \
	region.cpp \
	shstr.cpp \
	stringbuffer.cpp \
	time.cpp \
	treasure.cpp \
	utils.cpp

loader.cpp: loader.l
	$(LEX) $(AM_LFLAGS) -o loader.cpp $(top_srcdir)/common/loader.l

EXTRA_DIST = README is_valid_types_gen.py
AM_CPPFLAGS = -I$(top_srcdir)/include -DDATADIR=\"$(pkgdatadir)\" -DCONFDIR=\"$(pkgconfdir)\" \
	      -DLIBDIR=\"$(pkglibdir)\" -DLOCALDIR=\"$(pkgstatedir)\"
AM_LFLAGS = -i -B -Cf

CLEANFILES = loader.cpp arch_types_valid.cpp

arch_types_valid.cpp: is_valid_types_gen.py $(top_srcdir)/include/object.h
	$(PY) $(top_srcdir)/common/is_valid_types_gen.py $(top_srcdir)/include/object.h > $(builddir)/arch_types_valid.cpp
