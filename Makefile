# The host samples: a C compiler, make, and nothing else.
#
# `make` builds both, `make run` builds them and runs them (which is also the
# check - `host_snapshot` exits non-zero when a pixel is not where the scene
# put it), `make clean` removes what they wrote.
#
# The firmware sample is built by PlatformIO instead: `pio run -e rp2350_panel`.

CC ?= gcc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter
LDLIBS ?= -lm

ifdef OS
EXE := .exe
endif

INCLUDES = -I. -Icore -Ipanels -Itargets/host -Isamples/common

CORE = core/lvds_canvas.c core/lvds_draw.c core/lvds_palette.c \
       core/lvds_font_6x8.c core/lvds_font_8x14.c
HOST = targets/host/host_panel.c
SCENE = samples/common/scene.c

LOOP = samples/host_loop/host_loop$(EXE)
SNAPSHOT = samples/host_snapshot/host_snapshot$(EXE)

.PHONY: all run clean

all: $(LOOP) $(SNAPSHOT)

$(LOOP): $(CORE) $(HOST) $(SCENE) samples/host_loop/main.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDLIBS)

$(SNAPSHOT): $(CORE) $(HOST) $(SCENE) samples/host_snapshot/main.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDLIBS)

run: all
	$(LOOP) -n 3 -o frame.bmp
	$(SNAPSHOT) snapshot.bmp

clean:
	rm -f $(LOOP) $(SNAPSHOT) frame.bmp snapshot.bmp
