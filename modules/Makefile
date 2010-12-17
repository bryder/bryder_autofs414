# $Id: Makefile,v 1.5 2004/08/29 12:46:23 raven Exp $
#
# Makefile for autofs
#

-include ../Makefile.conf
include ../Makefile.rules

SRCS :=	lookup_yp.c  lookup_file.c  lookup_program.c  lookup_userhome.c \
	lookup_multi.c \
	parse_sun.c    \
	mount_generic.c  mount_nfs.c  mount_afs.c  mount_autofs.c \
	mount_changer.c  mount_bind.c

MODS :=	lookup_yp.so lookup_file.so lookup_program.so lookup_userhome.so \
	lookup_multi.so \
	parse_sun.so \
	mount_generic.so mount_nfs.so mount_afs.so mount_autofs.so \
	mount_changer.so mount_bind.so

ifeq ($(EXT2FS), 1)
  SRCS += mount_ext2.c
  MODS += mount_ext2.so
else
 ifeq ($(EXT3FS), 1)
   SRCS += mount_ext2.c
   MODS += mount_ext2.so
 endif
endif

ifeq ($(HESIOD), 1)
  SRCS += lookup_hesiod.c  parse_hesiod.c
  MODS += lookup_hesiod.so parse_hesiod.so
endif

ifeq ($(NISPLUS), 1)
  SRCS += lookup_nisplus.c
  MODS += lookup_nisplus.so
endif

ifeq ($(LDAP), 1)
  SRCS += lookup_ldap.c
  MODS += lookup_ldap.so
endif

CFLAGS += -I../include -fPIC -DAUTOFS_LIB_DIR=\"$(autofslibdir)\" -DPATH_AUTOMOUNT=\"$(sbindir)/automount\"

all: $(MODS)

clean:
	rm -f *.o *.s *.so *~

# mount_smbfs.so is an obsolete module which must be removed
install: all
	install -d -m 755 $(INSTALLROOT)$(autofslibdir)
	install -c $(MODS) -m 755 $(INSTALLROOT)$(autofslibdir)
	-rm -f $(INSTALLROOT)$(autofslibdir)/mount_smbfs.so
ifeq ($(EXT2FS), 1)
 ifeq ($(EXT3FS), 1)
	ln -fs mount_ext2.so $(INSTALLROOT)$(autofslibdir)/mount_ext3.so
 endif
else
 ifeq ($(EXT3FS), 1)
	mv $(INSTALLROOT)$(autofslibdir)/mount_ext2.so $(INSTALLROOT)$(autofslibdir)/mount_ext3.so
 endif
endif

#
# Ad hoc compilation rules for modules which need auxilliary libraries
#
lookup_yp.so: lookup_yp.c
	$(CC) $(SOLDFLAGS) $(CFLAGS) -o lookup_yp.so lookup_yp.c $(AUTOFS_LIB) $(LIBNSL)
	$(STRIP) lookup_yp.so

lookup_nisplus.so: lookup_nisplus.c
	$(CC) $(SOLDFLAGS) $(CFLAGS) -o lookup_nisplus.so lookup_nisplus.c \
		$(AUTOFS_LIB) $(LIBNSL)
	$(STRIP) lookup_nisplus.so

lookup_hesiod.so: lookup_hesiod.c
	$(CC) $(SOLDFLAGS) $(CFLAGS) $(HESIOD_FLAGS) -o lookup_hesiod.so \
		lookup_hesiod.c $(AUTOFS_LIB) $(LIBHESIOD) $(LIBRESOLV)
	$(STRIP) lookup_hesiod.so

lookup_ldap.so: lookup_ldap.c
	$(CC) $(SOLDFLAGS) $(CFLAGS) $(LDAP_FLAGS) -o lookup_ldap.so \
		lookup_ldap.c $(AUTOFS_LIB) $(LIBLDAP)
	$(STRIP) lookup_ldap.so

parse_sun.so: parse_sun.c
	$(CC) $(SOLDFLAGS) $(CFLAGS) -o parse_sun.so parse_sun.c $(AUTOFS_LIB) $(LIBNSL)
	$(STRIP) parse_sun.so
