# Experimental MSVC/Ninja benchmark only. Load after project() enables languages.
# Keep CMake's generated header, transitive includes, and include order exactly as
# they are in the PCH build, but parse that header for each ordinary translation
# unit. PCH creation jobs remain in the graph and still bypass the cache.
# These are CMake implementation variables, not a proposed production interface.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR MSVC_VERSION LESS 1900)
    message(FATAL_ERROR "The textual PCH benchmark requires modern MSVC")
endif()
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR "The textual PCH benchmark requires Ninja")
endif()
set(CMAKE_C_COMPILE_OPTIONS_USE_PCH /FI<PCH_HEADER>)
set(CMAKE_CXX_COMPILE_OPTIONS_USE_PCH /FI<PCH_HEADER>)
