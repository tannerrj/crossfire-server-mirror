if(NOT WIN32)
# cfanim link is broken on Windows
add_subdirectory(cfanim)
endif()
add_subdirectory(cfpython)
