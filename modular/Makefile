CC ?= cc

PROBE_TARGET := m64p-modular-probe
CORE_TARGET := mupen64plus_modular_libretro.so

PROBE_OBJECTS := probe.o m64p_loader.o
CORE_OBJECTS := libretro_core.o m64p_loader_pic.o libco.o

API_DIR := ../mupen64plus-core/src/api
LIBRETRO_COMMON := ../libretro-common

CPPFLAGS += -I$(API_DIR) -I$(LIBRETRO_COMMON)/include
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=c11
LDLIBS += -ldl

.PHONY: all probe core clean

all: $(PROBE_TARGET) $(CORE_TARGET)
probe: $(PROBE_TARGET)
core: $(CORE_TARGET)

$(PROBE_TARGET): $(PROBE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(PROBE_OBJECTS) $(LDLIBS)

$(CORE_TARGET): $(CORE_OBJECTS)
	$(CC) -shared $(LDFLAGS) -Wl,--version-script=../libretro/link.T -Wl,--no-undefined -o $@ $(CORE_OBJECTS) $(LDLIBS)

probe.o: probe.c m64p_loader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

m64p_loader.o: m64p_loader.c m64p_loader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

libretro_core.o: libretro_core.c m64p_loader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -include stdarg.h -c -o $@ $<

m64p_loader_pic.o: m64p_loader.c m64p_loader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

libco.o: $(LIBRETRO_COMMON)/libco/libco.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

clean:
	rm -f $(PROBE_TARGET) $(CORE_TARGET) $(PROBE_OBJECTS) $(CORE_OBJECTS)
