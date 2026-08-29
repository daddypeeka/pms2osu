cmake_minimum_required(VERSION 3.16)
project(pms2osu-v2 VERSION 0.3.0 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/convert.cpp
    src/util.cpp
    src/pms.cpp
    src/ogg.cpp
    src/osu.cpp
    src/zip.cpp
)

set(IMGUI third_party/imgui)
set(IMGUI_BACKENDS ${IMGUI}/backends)

add_library(imgui STATIC
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI_BACKENDS}/imgui_impl_glfw.cpp
    ${IMGUI_BACKENDS}/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI}
    third_party/glfw/include
)

# libogg
add_library(ogg STATIC
    third_party/libogg/src/bitwise.c
    third_party/libogg/src/framing.c
)
target_include_directories(ogg PUBLIC third_party/libogg/include)

# libvorbis
set(VORBIS_LIB third_party/libvorbis/lib)
add_library(vorbis STATIC
    ${VORBIS_LIB}/mdct.c ${VORBIS_LIB}/smallft.c
    ${VORBIS_LIB}/block.c ${VORBIS_LIB}/envelope.c
    ${VORBIS_LIB}/window.c ${VORBIS_LIB}/lsp.c
    ${VORBIS_LIB}/lpc.c ${VORBIS_LIB}/analysis.c
    ${VORBIS_LIB}/synthesis.c ${VORBIS_LIB}/psy.c
    ${VORBIS_LIB}/info.c ${VORBIS_LIB}/floor1.c
    ${VORBIS_LIB}/floor0.c ${VORBIS_LIB}/res0.c
    ${VORBIS_LIB}/mapping0.c ${VORBIS_LIB}/registry.c
    ${VORBIS_LIB}/codebook.c ${VORBIS_LIB}/sharedbook.c
    ${VORBIS_LIB}/lookup.c ${VORBIS_LIB}/bitrate.c
    ${VORBIS_LIB}/vorbisenc.c ${VORBIS_LIB}/vorbisfile.c
)
target_include_directories(vorbis PUBLIC
    third_party/libvorbis/include
    ${VORBIS_LIB}
)
target_link_libraries(vorbis PUBLIC ogg)

# WIN32 marks the executable as a Windows GUI-subsystem app (no console window
# on launch). It is ignored on non-Windows platforms. The --convert CLI still
# prints to stdout/stderr when launched from a terminal.
add_executable(pms2osu-v2 WIN32 ${SOURCES})
target_include_directories(pms2osu-v2 PRIVATE
    src
    ${IMGUI}
    ${IMGUI_BACKENDS}
    third_party/glfw/include
)
target_link_libraries(pms2osu-v2 PRIVATE imgui vorbis ogg)

if(WIN32)
    target_link_libraries(pms2osu-v2 PRIVATE
        third_party/glfw/lib-mingw-w64/libglfw3.a
        opengl32 gdi32 user32 shell32 ole32 uuid imm32 winmm
    )
    # Self-contained exe: statically link libgcc / libstdc++ / libwinpthread so
    # no MinGW runtime DLLs are required next to the exe.
    target_link_options(pms2osu-v2 PRIVATE -static -static-libgcc -static-libstdc++)
else()
    find_package(OpenGL REQUIRED)
    # Debian/Ubuntu and Homebrew both ship the config as lowercase "glfw3"
    # (e.g. .../cmake/glfw3/glfw3Config.cmake). On case-sensitive filesystems
    # "find_package(GLFW3)" would miss it, so search for the lowercase name.
    find_package(glfw3 REQUIRED)
    target_link_libraries(pms2osu-v2 PRIVATE glfw OpenGL::GL)
endif()

# GLFW is linked statically (libglfw3.a), so no glfw3.dll is needed next to
# the exe on Windows — the build is fully self-contained.