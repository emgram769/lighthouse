OBJDIR=objs
SRCDIR=src
INCDIR=$(SRCDIR)/inc
CFLAGS+=-I$(INCDIR)

CC=gcc

SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

CFLAGS+=-O2 -Wall -Werror
LDFLAGS+=-lxcb -lcairo

all: lighthouse

run: lighthouse
	./lighthouse

lighthouse: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(OBJS): | $(OBJDIR)
$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(INCDIR)/*.h) Makefile
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -rf $(OBJDIR) lighthouse

