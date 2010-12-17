dnl
dnl $Id: aclocal.m4,v 1.2 2003/09/29 08:22:35 raven Exp $
dnl
dnl --------------------------------------------------------------------------
dnl AF_PATH_INCLUDE:
dnl
dnl Like AC_PATH_PROGS, but add to the .h file as well
dnl --------------------------------------------------------------------------
AC_DEFUN(AF_PATH_INCLUDE,
[AC_PATH_PROGS($1,$2,$3,$4)
if test -n "$$1"; then
  AC_DEFINE(HAVE_$1,1,[define if you have $1])
  AC_DEFINE_UNQUOTED(PATH_$1, "$$1", [define if you have $1])
  HAVE_$1=1
else
  HAVE_$1=0
fi
AC_SUBST(HAVE_$1)])

dnl --------------------------------------------------------------------------
dnl AF_SLOPPY_MOUNT
dnl
dnl Check to see if mount(8) supports the sloppy (-s) option, and define
dnl the cpp variable HAVE_SLOPPY_MOUNT if so.  This requires that MOUNT is
dnl already defined by a call to AF_PATH_INCLUDE or AC_PATH_PROGS.
dnl --------------------------------------------------------------------------
AC_DEFUN(AF_SLOPPY_MOUNT,
[if test -n "$MOUNT" ; then
  AC_MSG_CHECKING([if mount accepts the -s option])
  if "$MOUNT" -s > /dev/null 2>&1 ; then
    AC_DEFINE(HAVE_SLOPPY_MOUNT, 1, [define if the mount command supports the -s option])
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi
fi])

dnl --------------------------------------------------------------------------
dnl AF_INIT_D
dnl
dnl Check the location of the init.d directory
dnl --------------------------------------------------------------------------
AC_DEFUN(AF_INIT_D,
[if test -z "$initdir"; then
  AC_MSG_CHECKING([location of the init.d directory])
  for init_d in /etc/init.d /etc/rc.d/init.d; do
    if test -z "$initdir"; then
      if test -d "$init_d"; then
	initdir="$init_d"
	AC_MSG_RESULT($initdir)
      fi
    fi
  done
fi])
