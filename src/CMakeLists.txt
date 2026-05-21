##############################################################################
# blosc2_j2k: Grok (JPEG2000 codec) plugin for Blosc2
#
# Copyright (c) 2023  The Blosc Development Team <blosc@blosc.org>
# https://blosc.org
# License: GNU Affero General Public License v3.0 (see LICENSE.txt)
##############################################################################

set(GRK_BUILD_LIBPNG OFF CACHE BOOL "Build libpng library" FORCE)
set(GRK_BUILD_LIBTIFF OFF CACHE BOOL "Build libtiff library" FORCE)
set(GRK_BUILD_LCMS2 ON CACHE BOOL "Build lcms2 library" FORCE)
set(GRK_BUILD_JPEG OFF CACHE BOOL "Build jpeg library" FORCE)
set(GRK_BUILD_CORE_EXAMPLES OFF CACHE BOOL "Build Grok core examples" FORCE)

# Avoid adding unneeded files to the wheel
set(GRK_BUILD_PKGCONFIG_FILES OFF CACHE BOOL "Build Grok pkg-config files" FORCE)
set(GRK_BUILD_CODEC OFF CACHE BOOL "Build Grok codec executables" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build statically
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (APPLE)
    # Apple Rosetta cannot emulate AVX due to Intel patents
    set(ENABLE_AVX2 OFF)
endif()
set(CMAKE_FIND_FRAMEWORK NEVER)
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# Grok's static-library bundling step writes to ${CMAKE_BINARY_DIR}/bin.
# Create it explicitly so isolated wheel/editable builds do not depend on
# another target creating that directory first.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(grok)
 # explicitly include headers as not exported by grok
target_include_directories(grokj2k
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/grok/src/lib/core  # source headers
        ${CMAKE_CURRENT_BINARY_DIR}/grok/src/lib/core  # generated headers
)
# Build libblosc2_j2k as a shared library on every platform.  Replacement
# backends link to the native Grok entry points, so the main target must be
# linkable and not only a Python extension module.
set(BLOSC2_J2K_CORE_SOURCES
    b2nd_layout.cpp
    blosc2_j2k.cpp
    codec_requests.cpp
    codestream_detector.cpp
    float_quantization.cpp
    jpeg2000_codec_paths.cpp
    plugin_loader.cpp
    runtime_config.cpp
)
add_library(blosc2_j2k SHARED ${BLOSC2_J2K_CORE_SOURCES})

if (MSVC OR MINGW)
    # Necessary for compiling blosc2_j2k.lib
    set_property(TARGET blosc2_j2k PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

# The plugin API header is private to this package but needed by the core
# loader and by the replacement backends built below.
target_include_directories(blosc2_j2k PUBLIC ${BLOSC2_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_link_libraries(blosc2_j2k PRIVATE grokj2k)
if(APPLE OR WIN32)
    # Keep the original Linux wheel behavior: libblosc2 is provided by the
    # host process/package and is not a direct dependency of this shared
    # library.  Linking it on Linux makes auditwheel try to vendor
    # libblosc2.so.8 from the python-blosc2 wheel and breaks wheel repair.
    target_link_libraries(blosc2_j2k PRIVATE ${BLOSC2_LIBRARIES})
endif()
if (UNIX AND NOT APPLE)
    target_link_libraries(blosc2_j2k PRIVATE dl)
endif()

# Add runtime path just in case (e.g. load plugin before blosc2 for example)
# will not always be used, but if can't find blosc2 gives another thing to check
# before failing
if(UNIX AND NOT APPLE)
    set_target_properties(blosc2_j2k PROPERTIES
        INSTALL_RPATH "$ORIGIN/../blosc2/lib"
    )
    # C-Blosc2's HDF5 filter can load external codecs with dlopen()/dlclose()
    # without the Python module keeping a reference to this shared library.  The
    # codec owns process-global state and lazily loaded backend handles, so it
    # must remain resident for the lifetime of the process when used through
    # HDF5's dynamic plugin path.
    target_link_options(blosc2_j2k PRIVATE "-Wl,-z,nodelete")
elseif(APPLE)
    set_target_properties(blosc2_j2k PROPERTIES
        INSTALL_RPATH "@loader_path/../blosc2/lib"
    )
endif()

# Install
install(TARGETS blosc2_j2k
    RUNTIME DESTINATION blosc2_j2k
    LIBRARY DESTINATION blosc2_j2k
)
install(FILES "${CMAKE_SOURCE_DIR}/blosc2_j2k/blosc2_j2k_plugins.json"
    DESTINATION blosc2_j2k
)

add_library(blosc2_jpeg2000_bootstrap SHARED jpeg2000_bootstrap.c)
target_include_directories(blosc2_jpeg2000_bootstrap PRIVATE ${BLOSC2_INCLUDE_DIR})
if(APPLE OR WIN32)
    target_link_libraries(blosc2_jpeg2000_bootstrap PRIVATE ${BLOSC2_LIBRARIES})
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(blosc2_jpeg2000_bootstrap PRIVATE dl)
    set_target_properties(blosc2_jpeg2000_bootstrap PROPERTIES
        INSTALL_RPATH "$ORIGIN/../blosc2/lib"
    )
elseif(APPLE)
    set_target_properties(blosc2_jpeg2000_bootstrap PROPERTIES
        INSTALL_RPATH "@loader_path/../blosc2/lib"
    )
endif()
if (MSVC OR MINGW)
    set_property(TARGET blosc2_jpeg2000_bootstrap PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()
install(TARGETS blosc2_jpeg2000_bootstrap
    RUNTIME DESTINATION blosc2_j2k
    LIBRARY DESTINATION blosc2_j2k
)
# Note: blosc2_j2k.h and grok headers are not installed in the wheel
# Python users don't need them, and C developers should build from source

# Optional Kakadu backend.  The core codec does not link to Kakadu; this target
# is built only when the Kakadu headers and libraries are explicitly available.
set(KAKADU_ROOT "" CACHE PATH "Kakadu installation root")
set(KAKADU_LIBRARY_DIR "" CACHE PATH "Kakadu library directory")
set(KAKADU_INCLUDE_HINTS "")
set(KAKADU_LIBRARY_HINTS "")

if(NOT KAKADU_ROOT AND DEFINED ENV{KAKADU_ROOT})
    set(KAKADU_ROOT "$ENV{KAKADU_ROOT}")
elseif(NOT KAKADU_ROOT AND EXISTS "${CMAKE_SOURCE_DIR}/../kakadu_install")
    set(KAKADU_ROOT "${CMAKE_SOURCE_DIR}/../kakadu_install")
endif()
if(DEFINED ENV{KAKADU_INCLUDE_DIR})
    list(APPEND KAKADU_INCLUDE_HINTS "$ENV{KAKADU_INCLUDE_DIR}")
endif()
if(NOT KAKADU_LIBRARY_DIR AND DEFINED ENV{KAKADU_LIBRARY_DIR})
    set(KAKADU_LIBRARY_DIR "$ENV{KAKADU_LIBRARY_DIR}")
elseif(NOT KAKADU_LIBRARY_DIR AND DEFINED ENV{KAKADU_LIB_PATH})
    set(KAKADU_LIBRARY_DIR "$ENV{KAKADU_LIB_PATH}")
endif()
if(KAKADU_LIBRARY_DIR)
    list(APPEND KAKADU_LIBRARY_HINTS "${KAKADU_LIBRARY_DIR}")
endif()

find_path(KAKADU_INCLUDE_DIR
    NAMES kdu_stripe_compressor.h
    HINTS ${KAKADU_INCLUDE_HINTS} ${KAKADU_ROOT}
    PATH_SUFFIXES managed/all_includes all_includes include
)
find_library(KAKADU_CORE_LIBRARY
    NAMES kdu_v86R kdu_v85R kdu_v84R kdu_v83R kdu_v82R kdu_v81R kdu_v80R kdu
    HINTS ${KAKADU_LIBRARY_HINTS} ${KAKADU_ROOT}
    PATH_SUFFIXES lib lib/Linux-x86-64-gcc apps/make
)
find_library(KAKADU_AUX_LIBRARY
    NAMES kdu_a86R kdu_a85R kdu_a84R kdu_a83R kdu_a82R kdu_a81R kdu_a80R kdu_aux
    HINTS ${KAKADU_LIBRARY_HINTS} ${KAKADU_ROOT}
    PATH_SUFFIXES lib lib/Linux-x86-64-gcc apps/make
)

if(KAKADU_INCLUDE_DIR AND KAKADU_CORE_LIBRARY AND KAKADU_AUX_LIBRARY)
    add_library(blosc2_kakadu_j2k_backend SHARED b2nd_layout.cpp plugin/kakadu_backend.cpp plugin/j2k_kakadu_plugin.cpp)
    target_include_directories(blosc2_kakadu_j2k_backend PRIVATE ${KAKADU_INCLUDE_DIR} ${BLOSC2_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
    target_link_libraries(blosc2_kakadu_j2k_backend PRIVATE ${KAKADU_CORE_LIBRARY} ${KAKADU_AUX_LIBRARY})

    if(UNIX AND NOT APPLE)
        set_target_properties(blosc2_kakadu_j2k_backend PROPERTIES
            INSTALL_RPATH "$ORIGIN/../../../../blosc2/lib"
        )
    elseif(APPLE)
        set_target_properties(blosc2_kakadu_j2k_backend PROPERTIES
            INSTALL_RPATH "@loader_path/../../../../blosc2/lib"
        )
    endif()

    if (MSVC OR MINGW)
        set_property(TARGET blosc2_kakadu_j2k_backend PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    endif()

    install(TARGETS blosc2_kakadu_j2k_backend
        RUNTIME DESTINATION blosc2_j2k/plugins/j2k/kakadu
        LIBRARY DESTINATION blosc2_j2k/plugins/j2k/kakadu
    )
    message(STATUS "Kakadu J2K backend enabled")
else()
    message(STATUS "Kakadu J2K backend disabled (Kakadu headers/libraries not found)")
endif()

# Reference replacement backend.  It uses the native Grok path so the runtime
# replacement mechanism is always testable, even when Kakadu is unavailable.
add_library(blosc2_j2k_backend SHARED plugin/j2k_grok_plugin.cpp)
target_include_directories(blosc2_j2k_backend PRIVATE
    ${BLOSC2_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/grok/src/lib/core
    ${CMAKE_CURRENT_BINARY_DIR}/grok/src/lib/core
)
target_link_libraries(blosc2_j2k_backend PRIVATE blosc2_j2k)
if (MSVC OR MINGW)
    set_property(TARGET blosc2_j2k_backend PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()
if(UNIX AND NOT APPLE)
    set_target_properties(blosc2_j2k_backend PROPERTIES
        INSTALL_RPATH "$ORIGIN/../../..;$ORIGIN/../../../../blosc2/lib"
    )
elseif(APPLE)
    set_target_properties(blosc2_j2k_backend PROPERTIES
        INSTALL_RPATH "@loader_path/../../..;@loader_path/../../../../blosc2/lib"
    )
endif()
install(TARGETS blosc2_j2k_backend
    RUNTIME DESTINATION blosc2_j2k/plugins/j2k/grok
    LIBRARY DESTINATION blosc2_j2k/plugins/j2k/grok
)

# Test program
if(NOT DEFINED ENV{DONT_BUILD_EXAMPLES})
    message(STATUS "DONT_BUILD_EXAMPLES not set-> Building examples")
    add_executable(test_grok test_grok.cpp ${BLOSC2_J2K_CORE_SOURCES} utils.cpp)
    target_include_directories(test_grok PRIVATE ${BLOSC2_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
    add_executable(roundtrip roundtrip.cpp ${BLOSC2_J2K_CORE_SOURCES} utils.cpp)
    target_include_directories(roundtrip PRIVATE ${BLOSC2_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
    if(MSVC OR MINGW)
        target_link_libraries(test_grok ${BLOSC2_LIBRARIES} grokj2k)
        target_link_libraries(roundtrip ${BLOSC2_LIBRARIES} grokj2k)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(test_grok ${BLOSC2_LIBRARIES} grokj2k m dl)
        target_link_libraries(roundtrip ${BLOSC2_LIBRARIES} grokj2k m dl)
    else()
        target_link_libraries(test_grok ${BLOSC2_LIBRARIES} grokj2k m)
        target_link_libraries(roundtrip ${BLOSC2_LIBRARIES} grokj2k m)
    endif()
    if(DEFINED BLOSC2_LIBRARY_DIR_RESOLVED)
        set_target_properties(test_grok PROPERTIES
            BUILD_RPATH "${BLOSC2_LIBRARY_DIR_RESOLVED}"
            INSTALL_RPATH "${BLOSC2_LIBRARY_DIR_RESOLVED}"
        )
        set_target_properties(roundtrip PROPERTIES
            BUILD_RPATH "${BLOSC2_LIBRARY_DIR_RESOLVED}"
            INSTALL_RPATH "${BLOSC2_LIBRARY_DIR_RESOLVED}"
        )
    endif()
else()
    message(STATUS "DONT_BUILD_EXAMPLES set")
endif()
