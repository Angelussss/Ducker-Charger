FWSRC = ../cubeMX/Core/Src
FWINC = ../cubeMX/Core/Inc

# real firmware sources, compiled unmodified against the fake HAL
FW  = $(FWSRC)/system/fsm.c \
      $(FWSRC)/system/charge.c \
      $(FWSRC)/system/event.c \
      $(FWSRC)/app/initialization.c \
      $(FWSRC)/app/provisioning.c \
      $(FWSRC)/app/telemetry.c \
      $(FWSRC)/app/encoder.c \
      $(FWSRC)/app/tps25750_bundle.c \
      $(FWSRC)/display/ili9341.c \
      $(FWSRC)/display/gfx.c \
      $(FWSRC)/ui/ui_state.c \
      $(FWSRC)/ui/screens.c \
      $(FWSRC)/ui/widgets.c

# emulator: fake HAL + IC models + board physics + SDL front-end
EMU = src/hal_impl.c \
      src/board.c \
      src/emu_config.c \
      src/dev_bq34z100.c \
      src/dev_tps25750.c \
      src/dev_cypd3175.c \
      src/dev_stpd01.c \
      src/dev_ina3221.c \
      src/dev_ili9341.c \
      src/emu_main.c

INC    = -Isrc -I$(FWINC)
CFLAGS = -Wall -Wextra -g -O1 -pthread $(INC)
SDL    = $(shell pkg-config --cflags --libs sdl2)

emulator: $(FW) $(EMU) fw_main.o
	gcc -o $@ $(FW) $(EMU) fw_main.o $(CFLAGS) $(SDL) -lm

# firmware main() renamed so the emulator owns the process entry point
fw_main.o: $(FWSRC)/main.c
	gcc -c -o $@ $< $(CFLAGS) -Dmain=fw_main

clean:
	rm -f emulator fw_main.o screen.ppm

.PHONY: clean
