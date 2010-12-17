# $Id: Makefile,v 1.6 2005/01/09 09:16:43 raven Exp $
#
# Makefile for autofs utility library
#

-include ../Makefile.conf
include ../Makefile.rules

RPCGEN = /usr/bin/rpcgen
RANLIB = /usr/bin/ranlib

SRCS = cache.c listmount.c cat_path.c rpc_subs.c mounts.c lock.c syslog.c \
	vsprintf.c nsswitch.c
RPCS = mount.h mount_clnt.c mount_xdr.c
OBJS = cache.o mount_clnt.o mount_xdr.o listmount.o \
	cat_path.o rpc_subs.o mounts.o lock.o syslog.o vsprintf.o nsswitch.o

LIB = autofs.a

CFLAGS += -I../include -fPIC -D_GNU_SOURCE -DHAVE_TCP_WRAPPER -DNFS3_SUPPORTED

.PHONY: all install clean

all: autofs.a

autofs.a: $(OBJS)
	rm -f $(LIB)
	$(AR) $(ARFLAGS) $(LIB) $(OBJS)
	-$(RANLIB) $(LIB)

mount.h: mount.x
	$(RPCGEN) -h -o mount.h mount.x

mount_clnt.c: mount.h
	$(RPCGEN) -l -o mount_clnt.c mount.x

mount_clnt.o: mount_clnt.c
	$(CC) $(CFLAGS) -o mount_clnt.o -c mount_clnt.c
	$(STRIP) mount_clnt.o

mount_xdr.c: mount.h
	$(RPCGEN) -c -o mount_xdr.c mount.x

mount_xdr.o: mount_xdr.c
	$(CC) $(CFLAGS) -Wno-unused-variable -o mount_xdr.o -c mount_xdr.c
	$(STRIP) mount_xdr.o

listmount.o: listmount.c
	$(CC) $(CFLAGS) -o listmount.o -c listmount.c
	$(STRIP) listmount.o

install: all

clean:
	rm -f $(LIB) $(RPCS) $(OBJS) *~

