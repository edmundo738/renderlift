cmake_minimum_required(VERSION 3.20)

project(RenderLift
    VERSION 0.1.0
    DESCRIPTION "Cross-API low-resolution rendering and reconstruction framework for low-end gaming PCs"
    HOMEPAGE_URL "https://github.com/edmundo738/renderlift"
    LANGUAGES CXX)

# The reconstruction engine inside RenderLift versions independently.
#   0.1 → ALRR Spatial · 0.5 → ALRR Edge · 1.0 → ALRR Temporal
set(ALRR_CORE_VERSION "0.1.0")

# ── Options ─────────────────────────────────────────────────────
option(RENDERLIFT_BUILD_TESTS    "Build the RenderLift test suite"            ON)
option(RENDERLIFT_BUILD_TOOLS    "Build RenderLift command-line tools"        ON)
option(RENDERLIFT_BUILD_BACKENDS "Build the per-API backend modules (stubs)"  ON)
option(RENDERLIFT_WERROR         "Treat compiler warnings as errors"          OFF)

# ── Language level ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(GNUInstallDirs)

# ── Shared version definitions ──────────────────────────────────
add_compile_definitions(
    RENDERLIFT_VERSION="${PROJECT_VERSION}"
    ALRR_CORE_VERSION="${ALRR_CORE_VERSION}"
)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
    if(RENDERLIFT_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(RENDERLIFT_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# ── Targets ─────────────────────────────────────────────────────
add_subdirectory(src)            # ALRR Core (API-agnostic engine)

if(RENDERLIFT_BUILD_BACKENDS)
    add_subdirectory(backends)   # per-API capture/injection modules
endif()

if(RENDERLIFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RENDERLIFT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

message(STATUS "")
message(STATUS "RenderLift ${PROJECT_VERSION} — ALRR Core ${ALRR_CORE_VERSION}")
message(STATUS "  tests ....... ${RENDERLIFT_BUILD_TESTS}")
message(STATUS "  tools ....... ${RENDERLIFT_BUILD_TOOLS}")
message(STATUS "  backends .... ${RENDERLIFT_BUILD_BACKENDS}")
message(STATUS "")
