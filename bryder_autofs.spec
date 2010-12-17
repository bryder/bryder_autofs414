#
# bill.ryder.nz@gmail.com's version of the below
#  Based on this:   $Id: autofs.spec,v 1.11 2003/12/04 15:41:32 raven Exp $
#
Summary: A tool for automatically mounting and unmounting filesystems.
Name: autofs
%define version 4.1.4
%define release 44
Version: %{version}
Release: %{release}
Epoch: 1
License: GPL
Group: System Environment/Daemons
Source: ftp://ftp.kernel.org/pub/linux/daemons/autofs/v4/autofs-%{version}.tar.bz2
Patch1: autofs-4.1.4-reentrant-syslog.patch
Patch2: autofs-4.1.4-reentrant-syslog-copyright.patch
Patch3: autofs-4.1.4-init-nsswitch-comment.patch
Patch4: autofs-4.1.4-init-one-auto-master.patch
Patch5: autofs-4.1.4-init-browse-as-non-first-option.patch
Patch6: autofs-4.1.4-hesiod-bind.patch
Patch7: autofs-4.1.4-non-replicated-ping.patch
Patch8: autofs-4.1.4-check-nsswitch-submount.patch
Patch9: autofs-4.1.3-alt-master-ldap.patch
Patch10: autofs-4.1.4-multi-parse-fix.patch
Patch11: autofs-4.1.4-cache-update-race-fix.patch
Patch12: autofs-4.1.4-solaris-hosts-in-auto-master.patch
Patch13: autofs-4.1.4-keylen-length-check.patch
Patch14: autofs-4.1.4-sun-parse-fixes.patch
Patch15: autofs-4.1.4-check-return-of-is-local-addr.patch
Patch16: autofs-4.1.4-fix-sort-opts.patch
Patch17: autofs-4.1.4-no-slash-misc.patch
Patch18: autofs-4.1.4-locking-fix.patch
Patch19: autofs-4.1.4-configureable-locking.patch
Patch20: autofs-4.1.4-sol10-schema.patch
Patch21: autofs-4.1.4-sockopt-len-type.patch
Patch22: autofs-4.1.4-yp_order-order-type.patch
Patch23: autofs-4.1.4-ldap-depricated.patch
Patch24: autofs-4.1.4-underscore_to_dot.patch
Patch25: autofs-4.1.3-ldap-auto-master.patch
Patch26: autofs-4.1.4-auto_net-escape-hash.patch
Patch27: autofs-4.1.4-auto.smb-cifs.patch
Patch28: autofs-4.1.4-get_best_mount-white-space.patch
Patch29: autofs-4.1.4-discard-bg-nofg-options.patch
Patch30: autofs-4.1.4-no-first-message.patch
Patch31: autofs-4.1.4-bad-key-len-fix.patch
Patch32: autofs-4.1.4-replicated-parse-error.patch
Patch33: autofs-4.1.4-dont-create-remote-dirs.patch
Patch34: autofs-4.1.4-update-changelog-add-bryder-spec.patch
Patch35: autofs-4.1.4-dumpmap.patch
Patch36: autofs-4.1.4-random-multimount-selection.patch
Patch37: autofs-4.1.4-init-unchecked-options-debianrhify-sysconfig.patch
Patch38: autofs-4.1.4-no-unlink-upstream-bryder.patch
Patch39: autofs-4.1.4-lookup-ldap-debug.patch 
Patch40: autofs-4.1.4-ldap-cleanup.patch
Patch41: autofs-4.1.4-no-stupid-paths.patch 
Patch42: autofs-4.1.4-mount-retry.patch
Patch43: autofs-4.1.4-ignore-stupid-samba-paths.patch
Patch44: autofs-4.1.4-fix-handle-packet-sigchld-race

Buildroot: /var/tmp/autofs-tmp
BuildPrereq: autoconf, hesiod-devel, openldap-devel, perl
Prereq: chkconfig
Requires: /bin/bash mktemp sed gawk textutils sh-utils grep /bin/ps
Obsoletes: autofs-ldap
Summary(de): autofs daemon 
Summary(fr): dÃ©mon autofs
Summary(tr): autofs sunucu sÃ¼reci
Summary(sv): autofs-daemon

%description
autofs is a daemon which automatically mounts filesystems when you use
them, and unmounts them later when you are not using them.  This can
include network filesystems, CD-ROMs, floppies, and so forth.

%description -l de
autofs ist ein DÃ¤mon, der Dateisysteme automatisch montiert, wenn sie 
benutzt werden, und sie spÃ¤ter bei Nichtbenutzung wieder demontiert. 
Dies kann Netz-Dateisysteme, CD-ROMs, Disketten und Ã¤hnliches einschlieÃen. 

%description -l fr
autofs est un dÃ©mon qui monte automatiquement les systÃ¨mes de fichiers
lorsqu'on les utilise et les dÃ©monte lorsqu'on ne les utilise plus. Cela
inclus les systÃ¨mes de fichiers rÃ©seau, les CD-ROMs, les disquettes, etc.

%description -l tr
autofs, kullanÃ½lan dosya sistemlerini gerek olunca kendiliÃ°inden baÃ°lar
ve kullanÃ½mlarÃ½ sona erince yine kendiliÃ°inden Ã§Ã¶zer. Bu iÃ¾lem, aÃ° dosya
sistemleri, CD-ROM'lar ve disketler Ã¼zerinde yapÃ½labilir.

%description -l sv
autofs Ã¤r en daemon som mountar filsystem nÃ¤r de anvÃ¤nda, och senare
unmountar dem nÃ¤r de har varit oanvÃ¤nda en bestÃ¤md tid.  Detta kan
inkludera nÃ¤tfilsystem, CD-ROM, floppydiskar, och sÃ¥ vidare.

%prep
%setup -q
echo %{version}-%{release} > .version
%patch1 -p1
%patch2 -p1
%patch3 -p1
%patch4 -p1
%patch5 -p1
%patch6 -p1
%patch7 -p1
%patch8 -p1
%patch9 -p1
%patch10 -p1
%patch11 -p1
%patch12 -p1
%patch13 -p1
%patch14 -p1
%patch15 -p1
%patch16 -p1
%patch17 -p1
%patch18 -p1
%patch19 -p1
%patch20 -p1
%patch21 -p1
%patch22 -p1
%patch23 -p1
%patch24 -p1
%patch25 -p1
%patch26 -p1
%patch27 -p1
%patch28 -p1
%patch29 -p1
%patch30 -p1
%patch31 -p1
%patch32 -p1
%patch33 -p1
%patch34 -p1
%patch35 -p1
%patch36 -p1
%patch37 -p1
%patch38 -p1
%patch39 -p1
%patch40 -p1
%patch41 -p1
%patch42 -p1
%patch43 -p1
%patch44 -p1

%build
#CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=/usr --libdir=%{_libdir}
%configure
make initdir=/etc/rc.d/init.d

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p -m755 $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p -m755 $RPM_BUILD_ROOT%{_sbindir}
mkdir -p -m755 $RPM_BUILD_ROOT%{_libdir}/autofs
mkdir -p -m755 $RPM_BUILD_ROOT%{_mandir}/{man5,man8}
mkdir -p -m755 $RPM_BUILD_ROOT/etc/sysconfig

make install mandir=%{_mandir} initdir=/etc/rc.d/init.d INSTALLROOT=$RPM_BUILD_ROOT
install -m 755 -d $RPM_BUILD_ROOT/misc
install -m 644 redhat/autofs.sysconfig $RPM_BUILD_ROOT/etc/sysconfig/autofs

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%post
chkconfig --add autofs

%postun
if [ $1 -ge 1 ] ; then
	/sbin/service autofs condrestart > /dev/null 2>&1 || :
fi

%preun
if [ "$1" = 0 ] ; then
	/sbin/service autofs stop > /dev/null 2>&1 || :
	/sbin/chkconfig --del autofs
fi
exit 0

%files
%defattr(-,root,root)
%doc CREDITS COPY* README* patches/* samples/ldap* samples/autofs.schema
%doc 
%config /etc/rc.d/init.d/autofs
%config(noreplace,missingok) /etc/auto.master
%config(noreplace,missingok) /etc/auto.misc
%config(noreplace,missingok) /etc/auto.net
%config(noreplace,missingok) /etc/auto.smb
%config(noreplace) /etc/sysconfig/autofs
%dir /misc
%dir %{_libdir}/autofs
%{_sbindir}/automount
%{_mandir}/*/*
%{_libdir}/autofs/*

%changelog
* Mon Jul 19 2010 Bill Ryder <bill.ryder.nz@gmail.com>

- Fix a problem where automount can shut itself down for no good reason.
  The case I had was  very high speed amount of failed mounts triggering the
      if (handle_packet() && errno != EINTR) ; break  
  piece of code in automount.c:handle_mounts which ended up just shutting down the automounter.
  The debug statement confirmed it (ap.state was 1 (READY) instead of 6 (SHUTDOWN))
  The bug is triggered if a child signal is caught when the daemon was not in the middle of a system call - hence errno 
  was not set to EINTR and handle_packet returned non zero - as in when a mount point didn't exist. 
  This was  reproducible when using the ignore-stupid-paths option which meant the daemon spent
  more time in user space handling autofs kernel packets than before.

* Thu Jul 15 2010 Bill Ryder <bill.ryder.nz@gmail.com>

- Added "automount(pid" to the ignore stupid paths list. samba looks for these because it's a bit stupid.

* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>

- Added the --max-nfs-mount-retries|-R <max retries> and --nfs-mount-retry-pause|-P <max secs> options to make the automounter retry on failed mounts
 which return these error messages:
 	"RPC: Remote system error - Connection refused", /* heavy fileserver load */
 	"RPC: Timed out", /* heavy fileserver load */
 	"RPC: Remote system error - Connection timed out", /* heavy fileserver load */
 	"Input/output error", /* too many mounts starting at once on a client  - centos 5.4 with 2.6.18 */
 	"can't read superblock", /* too many mounts starting at once on a client - 2.6.25.18 and others */ 

* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>

- Added the -I aka --ignore-stupid-paths option which will not attempt to mount a key containing '*' or anything starting with '.'
  See the CHANGELOG for a discussion

  
* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>

- Applied the 'ldap-cleanup' patch which greatly improves multiple schema handling - bryder made change to 
  make sure the rfc2307bis schema is checked first and updated man page and help output 

* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>
- Added function names to the ldap debug,error and info type outputs. Makes it a lot easier to figure out were things are occurring.

* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>
- Applied modifed no-unlink-upstream patch from kernel.org - prevents removals of files when removing a monted dir. bryder changes are cosmetic debug changes

* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>
- Added patch to allow for unchecked options in the init script. THis allows passing of ANY option to the automount daemon.
- made debian /etc/default/autofs handling act like the redhat one w.r.t. LOCALOPTIONS
- Applied modifed no-unlink-upstream patch - prevents removals of files when removing a monted dir. bryder changes are cosmetic debug changes
- Put in a modified sysconfig which explains things a little better


* Wed Jul 14 2010 Bill Ryder <bill.ryder.nz@gmail.com>
- Added redhat 4.1.3 random-multimount-patch - included help and manpage update for this also

* Wed Jul 14 2010 Bill Ryder  <bill.ryder.nz@gmail.com> 
- Added dumpmap patch. THe -D or --dumpmap option will make the daemon read the maps, dump them and exit.

* Wed Jul 14 2010 Bill Ryder  <bill.ryder.nz@gmail.com> 
- Updated CHANGELOG for applied patches below
  Added bryder_autofs.spec file for my convenience.
 
* Tue Oct 25 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-33
  p33 - includes autofs-4.1.4-dont-create-remote-dirs.patch
- correction to patch for revision 32.

* Tue Oct 24 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-32
- correction to patch for revision 31.

* Tue Oct 24 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-31
- deal with changed semantics of mkdir in kernel-2.6.18-1.2200.
- Resolves: rhbz#211207

* Fri Jun 23 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-29
- fix parse error in patch for space handling in get_best_mount
  (possibly also fixes bz# 196394).

* Mon Jun 19 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-27
- Change LDAP message severity from crit to degug (bz# 183893).
- Add patch to ignore the "bg" and "fg" mount options as they
  aren't relevant for autofs mounts (bz# 184386).
- Add patch to fix key and mapent length check.

* Tue May 30 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-25
- add patch to fix white space handling in get_best_mount
  function (bz #163999).

* Tue May 4 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-22
- add patch to use "cifs" instead of smbfs and escape speces
  in share names (bz #163999, #187732).

* Tue Apr 11 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-19
- Add patch to allow customization of arguments to the
  autofs-ldap-auto-master program (bz #187525).
- Add patch to escap "#" characters in exports from auto.net
  program mount (bz#178304).

* Fri Feb 10 2006 Jesse Keating <jkeating@redhat.com> - 1:4.1.4-16.2.2
- bump again for double-long bug on ppc(64)

* Tue Feb 07 2006 Jesse Keating <jkeating@redhat.com> - 1:4.1.4-16.2.1
- rebuilt for new gcc4.1 snapshot and glibc changes

* Tue Feb 1 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-16.2
- Add more general patch to translate "_" to "." in map names. (bz #147765)

* Mon Jan 25 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-16.1
- Add patch to use LDAP_DEPRICATED compile option. (bz #173833)

* Mon Jan 17 2006 Ian Kent <ikent@redhat.com> - 1:4.1.4-16
- Replace check-is-multi with more general multi-parse-fix.
- Add fix for premature return when waiting for lock file.
- Update copyright declaration for reentrant-syslog source.
- Add patch for configure option to disable locking during mount.
  But don't disable locking by default.
- Add ability to handle automount schema used in Sun directory server.
- Quell compiler warning about getsockopt parameter.
- Quell compiler warning about yp_order parameter.

* Fri Dec 09 2005 Jesse Keating <jkeating@redhat.com>
- rebuilt

* Thu Nov 17 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-14
- Removed the /misc entry from the default auto.master.  auto.misc has
  an entry for the cdrom device, and the preferred method of mounting the
  cd is via udev/hal.

* Mon Nov  7 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-13
- Changed to sort -k 1, since that should be the same as +0.

* Thu Nov  3 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-12
- The sort command no longer accepts options of the form "+0".  This broke
  auto.net, so the option was removed.  Fixes bz #172111.

* Wed Oct 26 2005  <jmoyer@redhat.com> - 1:4.1.4-11
- Check the return code of is_local_addr in get_best_mount. (bz #169523)

* Wed Oct 26 2005  <jmoyer@redhat.com> - 1:4.1.4-10
- Fix some bugs in the parser
- allow -net instead of /etc/auto.net
- Fix a buffer overflow with large key lengths
- Don't allow autofs to unlink files, only to remove directories
- change to the upstream reentrant syslog patch from the band-aid deferred
  syslog patch.
- Get rid of the init script patch that hard-coded the release to redhat.
  This should be handled properly by all red hat distros.

* Wed May  4 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-8
- Add in the deferred syslog patch.  This fixes a hung automounter issue
  related to unsafe calls to syslog in signal handler context.

* Tue May  3 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-7
- I reversed the checking for multimount entries, breaking those configs!
  This update puts the code back the way it was before I broke it.

* Tue Apr 26 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-6
- Fix a race between mounting a share and updating the cache in the parent
  process.  If the mount completed first, the parent would not expire the
  stale entry, leaving it first on the list.  This causes map updates to not
  be recognized (well, worse, they are recognized after the first expire, but
  not subsequent ones).  Fixes a regression, bug #137026 (rhel3 bug).

* Fri Apr 15 2005 Chris Feist <cfeist@redhat.com> - 1:4.1.4-5
- Fixed regression with -browse not taking effect.

* Wed Apr 13 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-4
- Finish up with the merge breakage.
- Temporary fix for the multimount detection code.  It seems half-baked.

* Wed Apr 13 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-3
- Fix up the one-auto-master patch.  My "improvements" had side-effects.

* Wed Apr 13 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.4-2
- Import 4.1.4 and merge.

* Mon Apr  4 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-123
- Add in an error case that was omitted in the multi-over patch.
- Update our auto.net to reflect the changes that went into 4.1.4_beta2.
  This fixes a problem seen by at least one customer where a malformed entry
  appeared first in the multimount list, thus causing the entire multimount
  to be ignored.  This new auto.net places that entry at the end, purely by
  luck, but it fixes the problem in this one case.

* Thu Mar 31 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-119
- Merge in the multi-over patch.  This resolves an issue whereby multimounts
  (such as those used for /net) could be processed in the wrong order,
  resulting in directories not showing up in a multimount tree.  The fix
  is to process these directories in order, shortest to longer path.

* Wed Mar 23 2005 Chris Feist <cfeist@redhat.com> - 1:4.1.3-115
- Fixed regression causing any entries after a wildcard in an
  indirect map to be ignored. (bz #151668).
- Fixed regression which caused local hosts to be mount instead
  of --bind local directories. (bz #146887)

* Thu Mar 17 2005 Chris Feist <cfeist@redhat.com> - 1:4.1.3-111
- Fixed one off bug in the submount-variable-propagation patch.
  (bz #143074)
- Fixed a bug in the init script which wouldn't find the -browse
  option if it was preceded by another option. (fz #113494)

* Mon Feb 28 2005 Chris Feist <cfeist@redhat.com> - 1:4.1.3-100
- When using ldap if auto.master doesn't exist we now check for auto_master.
  Addresses bz #130079
- When using an auto.smb map we now remove the leading ':' from the path which
  caused mount to fail in the past.  Addresses bz #147492
- Autofs now checks /etc/nsswitch.conf to determine in what order files & nis
  are checked when looking up autofs submount maps which don't specify a
  maptype.  Addresses IT #57612.

* Mon Feb 14 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-99
- Change Copyright to License in the spec file so it will build.

* Fri Feb 11 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-98
- Program maps can repeat the last character of output.  Fix this.  
  Addresses bz #138606
- Return first entry when there are duplicate keys in a map.  Addresses
  bz #140108.
- Propagate custom map variables to submounts.  Fixes bz #143074.
- Create a sysconfig variable to control whether we source only one master
  map (the way sun does), or source all maps found (which is the default for
  backwards compatibility).  Addresses bz #143126.
- Revised version of the get_best_mount patch. (#146887) cfeist@redhat.com
  The previous patch introduced a regression.  Non-replicated mounts would
  not have the white space stripped from the entry and the mount would fail.
- Handle comment characters in the middle of the automount line in
  /etc/nsswitch.conf.  Addresses bz #127457.

* Wed Feb  2 2005 Chris Feist <cfeist@redhat.com> - 1:4.1.3-94
- Stop automount from pinging hosts if there is only one host (#146887)

* Wed Feb  2 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-90
- Fix potential double free in cache_release.  This bug showed up in a
  multi-map setup.  Two calls to cache_release would result in a SIGSEGV,
  and the automount process would never exit.

* Mon Jan 24 2005 Chris Feist <cfeist@redhat.com> - 1:4.3-82
- Fixed documentation so users know that any local mounts override
  any other weighted mount.

* Mon Jan 24 2005 Chris Feist <cfeist@redhat.com> - 1:4.3-80
- Added a variable to determine if we created the directory or not
  so we don't accidently remove a directory that we didn't create when
  we stop autofs.  (bz #134399)

* Tue Jan 11 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-76
- Fix the large program map patch.

* Tue Jan 11 2005 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-75
- Fix some merging breakages that caused the package not to build.

* Thu Jan  6 2005  <jmoyer@redhat.com> - 1:4.1.3-74
- Add in the map expiry patch
- Bring in other patches that have been committed to other branches. This 
  version should now contain all fixes we have to date
- Merge conflicts due to map expiry changes

* Fri Nov 19 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-57
- Pass a socket into clntudp_bufcreate so that we don't use up additional 
  reserved ports.  This patch, along with the socket leak fix, addresses
  bz #128966.

* Wed Nov 17 2004  <jmoyer@redhat.com> - 1:4.1.3-56
- Somehow the -browse patch either didn't get committed or got reverted.
  Fixed.

* Tue Nov 16 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-55
- Fix program maps so that they can have gt 4k characters. (Neil Horman)
  Addresses bz #138994.
- Add a space after the colon here "Starting automounter:" in init script.
  Fixes bz #138513.

* Mon Nov 15 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-53
- Make autofs understand -[no]browse.  Addresses fz #113494.

* Thu Nov 11 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-48
- Fix the umount loop device function in the init script.

* Wed Oct 27 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-34
- Added a patch to fix the automounter failing on ldap maps
  when it couldn't get the whole map.  (ie. when the search
  limit was lower than the number of results)

* Thu Oct 21 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-32
- Fixed the use of +ypmapname so the maps included with +ypmapname
  are used in the correct order.  (In the past the '+' entries
  were always processed after local entries.)

* Thu Oct 21 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-31
- Fixed the duplicate map detection code to detect if maps try
  to mount on top of existing maps. 

* Wed Oct 20 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-29
- Fixed a problem with backwards compatability. Specifying local
  maps without '/etc/' prepended to them now works. (bz #136038)

* Fri Oct 15 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-28
- Fixed a bug which caused directories to never be unmounted. (bz #134403)

* Thu Oct 14 2004 Chris Feist <cfeist@redhat.com> - 1:4.1.3-27
- Fixed an error in the init script which caused duplicate entries to be
  displayed when asking for autofs status.

* Fri Oct  1 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-22
- Comment out map expiry (and related) patch for an FC3 build.

* Thu Sep 23 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-21
- Make local options apply to all maps in a multi-map entry.

* Tue Sep 21 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-20
- Merged my and Ian's socket leak fixes into one, smaller patch. Only
  partially addresses bz #128966.
- Fix some more echo lines for internationalization. bz #77820
- Revert the only one auto.master patch until we implement the +auto_master
  syntax.  Temporarily addresses bz #133055.

* Thu Sep  2 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-18
- Umount loopback filesystems under automount points when stopping the 
  automounter.
- Uncomment the map expiry patch.
- change a close to an fclose in lookup_file.c

* Tue Aug 31 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-17
- Add patch to support parsing nsswitch.conf to determine map sources.
- Disable this patch, and Ian's map expiry patch for a FC build.

* Tue Aug 24 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-16
- Version 3 of Ian's map expiry changes.

* Wed Aug 18 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-15
- Fix a socket leak in the rpc_subs, causing mounts to fail since we are 
  running out of port space fairly quickly.

* Wed Aug 18 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-14
- New map expiry patch from Ian.
- Fix a couple signal races.  No known problem reports of these, but they
  are holes, none-the-less.

* Tue Aug 10 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-13
- Only read one auto.master map (instead of concatenating all found sources).
- Uncomment Ian's experimental mount expiry patch.

* Fri Aug  6 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-12
- Add a sysconfig entry to disable direct map support, and set this to 
  1 by default.
- Disable the beta map expiry logic so I can build into a stable distro.
- Add defaults for all of the sysconfig variables to the init script so 
  we don't trip over user errors (i.e. deleting /etc/sysconfig/autofs).

* Wed Aug  4 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-11
- Add beta map expiry code for wider testing. (Ian Kent)
- Fix check for ghosting option.  I forgot to check for it in DAEMONOPTIONS.
- Remove STRIPDASH from /etc/sysconfig/autofs

* Mon Jul 12 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-10
- Add bad chdir patch from Ian Kent.
- Add a typo fix for the mtab lock file.
- Nuke the stripdash patch.  It didn't solve a problem.

* Tue Jun 22 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-9
- Bump revison for inclusion in RHEL 3.

* Mon Jun 21 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-8
- Change icmp ping to an rpc ping.  (Ian Kent)
- Fix i18n patch
  o Remove the extra \" from one echo line.
  o Use echo -e if we are going to do a \n in the echo string.

* Mon Jun 21 2004 Alan Cox <alan@redhat.com>
- Fixed i18n bug #107463

* Mon Jun 21 2004 Alan Cox <alan@redhat.com>
- Fixed i18n bug #107461

* Tue Jun 15 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Sat Jun  5 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-4
- Perform an icmp ping request before rpc_pings, since the rpc clnt_create
  function has a builtin default timeout of 60 seconds.  This could result
  in a long delay when a server in a replicated mount setup is down.
- For non-replicated server entries, ping a host before attempting to mount.
  (Ian Kent)
- Change to %%configure.
- Put version-release into .version to allow for automount --version to
  print exact info.
- Nuke my get-best-mount patch which always uses the long timeout.  This
  should no longer be needed.
- Put name into changelog entries to make them consistent.  Add e:n-v-r
  into Florian's entry.
- Stop autofs before uninstalling

* Sat Jun 05 2004 Florian La Roche <Florian.LaRoche@redhat.de> - 1:4.1.3-3
- add a preun script to remove autofs

* Tue Jun  1 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-2
- Incorporate patch from Ian which fixes an infinite loop seen by those
  running older versions of the kernel patches (triggered by non-strict mounts
  being the default).

* Tue Jun  1 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.3-1
- Update to upstream 4.1.3.

* Thu May  6 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.2-6
- The lookup_yp module only dealt with YPERR_KEY, all other errors were 
  treated as success.  As a result, if the ypdomain was not bound, the 
  subprocess that starts mounts would SIGSEGV.  This is now fixed.
- Option parsing in the init script was not precise enough, sometimes matching
  filesystem options to one of --ghost, --timeout, --verbose, or --debug.  
  The option-parsing patch addresses this issue by making the regexp's much
  more precise.
- Ian has rolled a third version of the replicated mount fixes.

* Tue May  4 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.2-5
- Ian has a new fix for replicated server and multi-mounts.  Updated the 
  patch for testing.  Still beta.  (Ian Kent)

* Mon May  3 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.2-4
- Fix broken multi-mounts.  test patch.  (Ian Kent)

* Tue Apr 20 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.2-3
- Fix a call to spawnl which forgot to specify a lock file. (nphilipp)

* Wed Apr 14 2004  <jmoyer@redhat.com> - 1:4.1.2-2
- Pass --libdir= to ./configure so we get this right on 64 bit platforms that 
  support backwards compat.

* Wed Apr 14 2004  Jeff Moyer <jmoyer@redhat.com> - 1:4.1.2-1
- Change hard-coded paths in the spec file to the %{_xxx} variety.
- Update to upstream 4.1.2.
- Add a STRIPDASH option to /etc/sysconfig/autofs which allows for
  compatibility with the Sun automounter options specification syntax in
  auto.master.  See /etc/sysconfig/autofs for more information.  Addresses
  bug 113950.

* Tue Apr  6 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-6
- Add the /etc/sysconfig/autofs file, and supporting infrastructure in 
  the init script.
- Add support for UNDERSCORE_TO_DOT for those who want it.
- We no longer own /net.  Move it to the filesystem package.

* Tue Mar 30 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-5
- Clarify documentation on direct maps.
- Send automount daemons a HUP signal during reload.  This tells them to 
  re-read maps (otherwise they use a cached version.  Patch from the autofs
  maintainer.

* Mon Mar 22 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-4
- Fix init script to print out failures where appropriate.
- Build the automount daemon as a PIE.

* Thu Mar 18 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-3
- Fix bug in get_best_mount, whereby if there is only one option, we 
  choose nothing.  This is primarily due to the fact that we pass 0 in to
  the get_best_mount function for the long timeout parameter.  So, we
  timeout trying to contact our first and only server, and never retry.

* Thu Mar 18 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-2
- Prevent startup if a mountpoint is already mounted.

* Thu Mar 18 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.1-1
- Update to 4.1.1, as it fixes problems with wildcards that people are 
  seeing quite a bit.

* Wed Mar 17 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.0-8
- Fix ldap init code to parse server name and options correctly.

* Tue Mar 16 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.0-7
- Moved the freeing of ap.path to cleanup_exit, as we would otherwise 
  reference an already-freed variable.

* Mon Mar 15 2004 Jeff Moyer <jmoyer@redhat.com> - 1:4.1.0-6
- add %config(noreplace) for auto.* config files.

* Wed Mar 10 2004 Jeff Moyer <jmoyer@redhat.com> 1:4.1.0-5
- make the init script only recognize redhat systems.  Nalin seems to remember
  some arcane build system error that can be caused if we don't do this.

* Wed Mar 10 2004 Jeff Moyer <jmoyer@redhat.com> 1:4.1.0-4
- comment out /net and /misc from the default auto.master.  /net is important
  since in a default shipping install, we can neatly co-exist with amd.

* Wed Mar 10 2004 Jeff Moyer <jmoyer@redhat.com> 1:4.1.0-3
- Ported forward Red Hat's patches from 3.1.7 that were not already present
  in 4.1.0.
- Moving autofs from version 3.1.7 to 4.1.0

* Mon Sep 29 2003 Ian Kent <raven@themaw.net>
- Added work around for O(1) patch oddity.

* Sat Aug 17 2003 Ian Kent <raven@themaw.net>
- Fixed tree mounts.
- Corrected transciption error in autofs4-2.4.18 kernel module

* Sun Aug 10 2003 Ian Kent <raven@themaw.net>
- Checked and merged most of the RedHat v3 patches
- Fixed kernel module handling wu-ftpd login problem (again)

* Thu Aug 7 2003 Ian Kent <raven@themaw.net>
- Removed ineffective lock stuff
- Added -n to bind mount to prevent mtab update error
- Added retry to autofs umount to clean matb after fail
- Redirected messages from above to debug log and added info message
- Fixed autofs4 module reentrancy, pwd and chroot handling

* Wed Jul 30 2003 Ian Kent <raven@themaw.net>
- Fixed autofs4 ghosting patch for 2.4.19 and above (again)
- Fixed autofs directory removal on failure of autofs mount
- Fixed lock file wait function overlapping calls to (u)mount

* Sun Jul 27 2003 Ian Kent <raven@themaw.net>
- Implemented LDAP direct map handling for nisMap and automountMap schema
- Fixed autofs4 ghosting patch for 2.4.19 and above (again)
- Added locking to fix overlapping internal calls to (u)mount 
- Added wait for mtab~ to improve tolerance of overlapping external calls to (u)mount
- Fixed ghosted directory removal after failed mount attempt

* Wed May 28 2003 Ian Kent <raven@themaw.net>
- Cleaned up an restructured my added code
- Corrected ghosting problem with 2.4.19 and above
- Added autofs4 ghosting patch for 2.4.19 and above
- Implemented HUP signal to force update of ghosted maps

* Mon Mar 23 2002 Ian Kent <ian.kent@pobox.com>
- Add patch to implement directory ghosting and direct mounts
- Add patch to for autofs4 module to support ghosting

* Wed Jan 17 2001 Nalin Dahyabhai <nalin@redhat.com>
- use -fPIC instead of -fpic for modules and honor other RPM_OPT_FLAGS

* Tue Feb 29 2000 Nalin Dahyabhai <nalin@redhat.com>
- enable hesiod support over libbind

* Fri Aug 13 1999 Cristian Gafton <gafton@redhat.com>
- add patch from rth to avoid an infinite loop
