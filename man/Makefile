# $Id: Makefile,v 1.3 2003/09/29 08:22:35 raven Exp $

-include ../Makefile.conf
include ../Makefile.rules

GENFILES = $(patsubst %.in, %, $(wildcard *.[58].in))

.SUFFIXES: .in

.in:
	sed -e 's|@@INIT.D@@|$(initdir)|g' < $< > $@

all: $(GENFILES)

install: all
	install -d -m 755 $(INSTALLROOT)$(mandir)/man5
	install -c *.5 -m 644 $(INSTALLROOT)$(mandir)/man5
	install -d -m 755 $(INSTALLROOT)$(mandir)/man8
	install -c *.8 -m 644 $(INSTALLROOT)$(mandir)/man8

clean:
	rm -f $(GENFILES)
