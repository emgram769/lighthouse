CC=gcc

OBJDIR=objs
SRCDIR=.
INCDIR=$(SRCDIR)/inc

CFLAGS+=-I$(INCDIR) -Wall -Werror -O3
LDFLAGS+=-lxcb

SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

all: lighthouse

lighthouse:
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/*.h Makefile
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -rf $(OBJDIR) lighthouse
