CC=gcc
OBJDIR=objs
SRCDIR=src
INCDIR=$(SRCDIR)/inc
CFLAGS+=-I$(INCDIR)

SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

CFLAGS+=-O2 -Wall
CFLAGS_DEBUG+=-O0 -Werror -DDEBUG=1
LDFLAGS+=-lxcb -lxcb-xkb -lxcb-xinerama -lxcb-randr -lcairo -lpthread

all: lighthouse

debug: lighthouse_debug

config: lighthouse .FORCE
	cp -ir config/* ~/.config/

.FORCE:

lighthouse: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

lighthouse_debug: $(OBJS)
	$(CC) $(CFLAGS) $(CFLAGS_DBG) $(LDFLAGS) $^ -o $@

$(OBJS): | $(OBJDIR)
$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(INCDIR)/*.h) Makefile
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -rf $(OBJDIR) lighthouse

