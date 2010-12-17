# $Id: Makefile,v 1.5 2004/04/03 07:14:33 raven Exp $
#
# Makefile for autofs
#

-include ../Makefile.conf
include ../Makefile.rules

SRCS = automount.c spawn.c module.c mount.c
OBJS = automount.o spawn.o module.o mount.o

version := $(shell cat ../.version)

CFLAGS += -rdynamic $(DAEMON_CFLAGS) -DAUTOFS_LIB_DIR=\"$(autofslibdir)\" -DVERSION_STRING=\"$(version)\" -I../include
LDFLAGS += -rdynamic
LIBS = -ldl

all: automount

automount: $(OBJS) $(AUTOFS_LIB)
	$(CC) $(LDFLAGS) $(DAEMON_LDFLAGS) -o automount $(OBJS) $(AUTOFS_LIB) $(LIBS)
	$(STRIP) automount

clean:
	rm -f *.o *.s *~ automount

install: all
	install -d -m 755 $(INSTALLROOT)$(sbindir)
	install -c automount -m 755 $(INSTALLROOT)$(sbindir)


