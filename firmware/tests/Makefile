CC     = gcc
# -MMD -MP: each source also gets a .d listing the headers it pulled in, so
# a header-only edit (e.g. a constant in defines.h) invalidates every test
# binary that (transitively) includes it, not just ones whose .c changed.
CFLAGS = -std=c11 -g \
         -Istubs \
         -I../cubeMX/Core/Inc \
         -Wno-unused-parameter \
         -Wno-unused-variable \
         -MMD -MP

CORE   = ../cubeMX/Core/Src/system/event.c \
         ../cubeMX/Core/Src/system/fsm.c   \
         ../cubeMX/Core/Src/system/charge.c \
         ../cubeMX/Core/Src/system/tps25750_io.c
STUB      = stubs/stm32_hal_stub.c stubs/i2c_device_model.c
MODELS    = stubs/cypd3175_model.c stubs/stpd01_model.c

.PHONY: all clean

all: test_event test_fsm test_charge test_cypd3175 test_hpi_schematic test_math
	./test_event && ./test_fsm && ./test_charge && ./test_cypd3175 && ./test_hpi_schematic && ./test_math

test_event: test_event.c ../cubeMX/Core/Src/system/event.c
	$(CC) $(CFLAGS) $^ -o $@

test_fsm: test_fsm.c $(CORE) $(STUB)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_charge: test_charge.c $(CORE) $(STUB)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_cypd3175: test_cypd3175.c $(CORE) $(STUB)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_hpi_schematic: test_hpi_schematic.c $(CORE) $(STUB) $(MODELS)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_math: test_math.c $(CORE) $(STUB)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -f test_event test_fsm test_charge test_cypd3175 test_hpi_schematic test_math *.d

-include $(wildcard *.d)
