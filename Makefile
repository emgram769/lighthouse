CC=gcc
OBJDIR=objs
SRCDIR=src
INCDIR=$(SRCDIR)/inc
CFLAGS+=-I$(INCDIR)

SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
TARGET=lighthouse

CFLAGS+=-O2 -Wall -std=c99
CFLAGS_DEBUG+=-O0 -g3 -Werror -DDEBUG -pedantic
LDFLAGS+=-lxcb -lxcb-xkb -lxcb-xinerama -lxcb-randr -lcairo -lpthread

# OS X keeps xcb in a different spot
platform=$(shell uname)
ifeq ($(platform),Darwin)
	CFLAGS+=-I/usr/X11/include
	LDFLAGS+=-L/usr/X11/lib
endif

# Library specific
HAS_GDK := $(shell pkg-config --exists gdk-2.0 echo $?)
ifdef $(HAS_GDK)
	CFLAGS+=`pkg-config --cflags gdk-2.0`
	LDFLAGS+=`pkg-config --libs gdk-2.0`
else
	CFLAGS+=-DNO_GDK
endif
HAS_PANGO := $(shell pkg-config --exists pango echo $?)
ifdef $(HAS_PANGO)
	CFLAGS+=`pkg-config --cflags pango`
	LDFLAGS+=`pkg-config --libs pango`
else
	CFLAGS+=-DNO_PANGO
endif


all: $(TARGET)

debug: CC+=$(CFLAGS_DEBUG)
debug: $(TARGET) .FORCE

config: $(TARGET) .FORCE
	mkdir -p ~/.config/$(TARGET)
	if [ ! -s ~/.config/$(TARGET)/lighthouserc ]; then cp -ir ./config/* ~/.config/ && chmod +x ~/.config/$(TARGET)/cmd*; fi;

.FORCE:

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJS): | $(OBJDIR)
$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(INCDIR)/*.h) Makefile
	$(CC) $(CFLAGS) $< -c -o $@

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

