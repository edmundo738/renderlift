# Vendored MinHook 1.3.4 — https://github.com/TsudaKageyu/minhook
# License: BSD 2-Clause (see LICENSE.txt, recorded in THIRD_PARTY_NOTICES.md)
# Built only on Windows; the rest of the tree uses the stub hook engine.

if(NOT WIN32)
    message(STATUS "minhook: skipped (Windows-only dependency)")
    return()
endif()

set(MINHOOK_SOURCES
    src/buffer.c
    src/hook.c
    src/trampoline.c
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND MINHOOK_SOURCES src/hde/hde64.c)
else()
    list(APPEND MINHOOK_SOURCES src/hde/hde32.c)
endif()

add_library(minhook STATIC ${MINHOOK_SOURCES})
add_library(RenderLift::MinHook ALIAS minhook)

target_include_directories(minhook PUBLIC include)
target_include_directories(minhook PRIVATE src src/hde)

# MinHook is upstream C code; keep its warnings local and quiet.
if(MSVC)
    target_compile_options(minhook PRIVATE /W3)
else()
    target_compile_options(minhook PRIVATE -w)
endif()
