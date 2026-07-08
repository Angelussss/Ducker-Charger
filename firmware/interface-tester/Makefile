FWSRC = ../cubeMX/Core/Src
FWINC = ../cubeMX/Core/Inc

SHARED = $(FWSRC)/display/gfx.c \
         $(FWSRC)/ui/widgets.c   \
         $(FWSRC)/ui/screens.c   \
         $(FWSRC)/ui/ui_state.c

STUB = src/sim_display.c src/sim_stubs.c src/sim_config.c

INC    = -Isrc -I$(FWINC)
CFLAGS = -Wall -Wextra $(INC)
SDL    = $(shell pkg-config --cflags --libs sdl2)

sim_ui: $(SHARED) $(STUB) src/sdl_main.c
	gcc -o $@ $^ $(CFLAGS) $(SDL) -lm

clean:
	rm -f sim_ui

.PHONY: clean
