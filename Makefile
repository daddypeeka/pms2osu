# =====================================================================
# pms2osu-v2 - PMS -> osu! beatmap (.osz) converter with GUI
# Build:  w64devkit on Windows (PATH contains bin), or any MinGW/clang/gcc
# Usage:  make            build pms2osu-v2.exe
#         make clean
# =====================================================================
CXX      := g++
CC       := gcc
RM       := rm -f
MKDIR    := mkdir -p

IMGUI    := third_party/imgui
GLFW     := third_party/glfw
OGG      := third_party/libogg
VORBIS   := third_party/libvorbis
BUILD    := build
BIN      := dist

INC  := -Isrc -I$(IMGUI) -I$(IMGUI)/backends -I$(GLFW)/include \
        -I$(OGG)/include -I$(VORBIS)/include -I$(VORBIS)/lib

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -MMD -MP $(INC)
CFLAGS   := -O2 -w $(INC)

APP_SRCS := src/main.cpp src/app.cpp src/convert.cpp src/util.cpp \
            src/pms.cpp src/ogg.cpp src/osu.cpp src/zip.cpp

IMGUI_SRCS := $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp \
              $(IMGUI)/imgui_tables.cpp $(IMGUI)/imgui_widgets.cpp \
              $(IMGUI)/backends/imgui_impl_glfw.cpp \
              $(IMGUI)/backends/imgui_impl_opengl3.cpp

OGG_SRCS := $(OGG)/src/bitwise.c $(OGG)/src/framing.c

VORBIS_SRCS := $(VORBIS)/lib/mdct.c $(VORBIS)/lib/smallft.c \
               $(VORBIS)/lib/block.c $(VORBIS)/lib/envelope.c \
               $(VORBIS)/lib/window.c $(VORBIS)/lib/lsp.c \
               $(VORBIS)/lib/lpc.c $(VORBIS)/lib/analysis.c \
               $(VORBIS)/lib/synthesis.c $(VORBIS)/lib/psy.c \
               $(VORBIS)/lib/info.c $(VORBIS)/lib/floor1.c \
               $(VORBIS)/lib/floor0.c $(VORBIS)/lib/res0.c \
               $(VORBIS)/lib/mapping0.c $(VORBIS)/lib/registry.c \
               $(VORBIS)/lib/codebook.c $(VORBIS)/lib/sharedbook.c \
               $(VORBIS)/lib/lookup.c $(VORBIS)/lib/bitrate.c \
               $(VORBIS)/lib/vorbisenc.c $(VORBIS)/lib/vorbisfile.c

OBJS := $(BUILD)/app_main.o $(BUILD)/app_app.o $(BUILD)/app_convert.o \
        $(BUILD)/app_util.o $(BUILD)/app_pms.o $(BUILD)/app_ogg.o \
        $(BUILD)/app_osu.o $(BUILD)/app_zip.o \
        $(BUILD)/imgui.o $(BUILD)/imgui_draw.o $(BUILD)/imgui_tables.o \
        $(BUILD)/imgui_widgets.o $(BUILD)/imgui_impl_glfw.o \
        $(BUILD)/imgui_impl_opengl3.o \
        $(BUILD)/ogg_bitwise.o $(BUILD)/ogg_framing.o \
        $(BUILD)/vorbis_mdct.o $(BUILD)/vorbis_smallft.o \
        $(BUILD)/vorbis_block.o $(BUILD)/vorbis_envelope.o \
        $(BUILD)/vorbis_window.o $(BUILD)/vorbis_lsp.o \
        $(BUILD)/vorbis_lpc.o $(BUILD)/vorbis_analysis.o \
        $(BUILD)/vorbis_synthesis.o $(BUILD)/vorbis_psy.o \
        $(BUILD)/vorbis_info.o $(BUILD)/vorbis_floor1.o \
        $(BUILD)/vorbis_floor0.o $(BUILD)/vorbis_res0.o \
        $(BUILD)/vorbis_mapping0.o $(BUILD)/vorbis_registry.o \
        $(BUILD)/vorbis_codebook.o $(BUILD)/vorbis_sharedbook.o \
        $(BUILD)/vorbis_lookup.o $(BUILD)/vorbis_bitrate.o \
        $(BUILD)/vorbis_vorbisenc.o $(BUILD)/vorbis_vorbisfile.o

# Statically link the MinGW runtime (libgcc / libstdc++ / libwinpthread) so the
# exe is fully self-contained and does not need libgcc_s_seh-1.dll,
# libstdc++-6.dll or libwinpthread-1.dll next to it.
# -mwindows links as a GUI-subsystem app so no console window appears when the
# app is launched; the CLI mode still works when run from a terminal.
LDFLAGS := -L$(GLFW)/lib-mingw-w64 -mwindows -static -static-libgcc -static-libstdc++
LDLIBS  := -lglfw3 -lopengl32 -lgdi32 -luser32 -lshell32 -lole32 -luuid -limm32 -lwinmm -lwinpthread

all: $(BIN)/pms2osu-v2.exe

$(BIN)/pms2osu-v2.exe: $(OBJS) | $(BIN)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	@echo "[built] $@ (self-contained, no glfw3.dll needed)"

$(BUILD)/app_main.o: src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_app.o: src/app.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_convert.o: src/convert.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_util.o: src/util.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_pms.o: src/pms.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_ogg.o: src/ogg.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_osu.o: src/osu.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/app_zip.o: src/zip.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/imgui.o: $(IMGUI)/imgui.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/imgui_draw.o: $(IMGUI)/imgui_draw.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/imgui_tables.o: $(IMGUI)/imgui_tables.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/imgui_widgets.o: $(IMGUI)/imgui_widgets.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/imgui_impl_glfw.o: $(IMGUI)/backends/imgui_impl_glfw.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/imgui_impl_opengl3.o: $(IMGUI)/backends/imgui_impl_opengl3.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ogg_bitwise.o: $(OGG)/src/bitwise.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/ogg_framing.o: $(OGG)/src/framing.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vorbis_%.o: $(VORBIS)/lib/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) $(BIN):
	$(MKDIR) $@

-include $(wildcard $(BUILD)/*.d)

clean:
	$(RM) -r $(BUILD) $(BIN)

.PHONY: all clean
