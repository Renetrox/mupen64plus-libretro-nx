CC ?= cc

TARGET := m64p-modular-probe
SOURCES := probe.c m64p_loader.c
OBJECTS := $(SOURCES:.c=.o)

API_DIR := ../mupen64plus-core/src/api

CPPFLAGS += -I$(API_DIR)
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=c11
LDLIBS += -ldl

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c m64p_loader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECTS)
