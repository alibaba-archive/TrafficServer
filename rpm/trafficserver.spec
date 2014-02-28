# https://fedoraproject.org/wiki/Packaging:Guidelines#PIE
%define _hardened_build 1
Summary:	Fast, scalable and extensible HTTP/1.1 compliant caching proxy server
Name:		trafficserver
Version:	3.2.0
Release:	4%(echo ${RELEASE:+.${RELEASE}})%{?dist}
License:	ASL 2.0
Group:		System Environment/Daemons
Source0:	%{name}-%{version}.tar.bz2
#Source1:	trafficserver.sysconf
URL:		http://trafficserver.apache.org/index.html
# BuildRoot is only needed for EPEL5:
#BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
BuildRequires:	autoconf, automake, libtool, t-openssl-devel, t-spdylay-devel, tcl-devel, expat-devel
BuildRequires:	pcre-devel, zlib-devel, xz-devel, gcc-c++
Provides:	t-cdn-trafficserver, trafficserver_ssd

#Patch8:		lock.patch

%description
Apache Traffic Server is a fast, scalable and extensible HTTP/1.1 compliant
caching proxy server.

%prep
getent group ats >/dev/null || groupadd -r ats -g 176 &>/dev/null
getent passwd ats >/dev/null || \
useradd -r -u 176 -g ats -d / -s /sbin/nologin \
        -c "Apache Traffic Server" ats &>/dev/null

#%setup -q -n %{name}-%{version}-unstable
%setup -q

#%patch8 -p1 -b .patch8

%build
## this will change the default -O3 to -O2.
#export CFLAGS="$RPM_OPT_FLAGS"
#export CXXFLAGS="$RPM_OPT_FLAGS"
export CFLAGS="-I/home/a/include $CFLAGS"
export CXXFLAGS="-I/home/a/include $CXXFLAGS"
export LDFLAGS="-L/home/a/lib64 $LDFLAGS"
export PKG_CONFIG_PATH="/home/a/lib64/pkgconfig"
./configure --enable-layout=Gentoo \
	    --libdir=%{_libdir}/trafficserver \
	    --with-tcl=%{_libdir} \
	    --with-user=ats \
	    --with-group=ats \
	    --enable-reclaimable-freelist \
	    --enable-spdy \
	    --with-openssl=/home/a
make %{?_smp_mflags}

%install
echo $RPM_BUILD_ROOT
#rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install

# the traffic_shell manual conflict with bash: exit enable,
# so we rename these to ts-enable, ts-exit and ts-disable.
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man1
cp doc/man/*.1 $RPM_BUILD_ROOT/usr/share/man/man1/
mv $RPM_BUILD_ROOT/usr/share/man/man1/enable.1 \
$RPM_BUILD_ROOT/usr/share/man/man1/ts-enable.1
mv $RPM_BUILD_ROOT/usr/share/man/man1/disable.1 \
$RPM_BUILD_ROOT/usr/share/man/man1/ts-disable.1
mv $RPM_BUILD_ROOT/usr/share/man/man1/exit.1 \
$RPM_BUILD_ROOT/usr/share/man/man1/ts-exit.1
cat <<EOF > README.fedora
The man-pages for enable, disable and exit was renamed to ts-enable, 
ts-disable and ts-exit to avoid conflicts with other man-pages.
EOF

mkdir -p $RPM_BUILD_ROOT/etc/init.d/
mv $RPM_BUILD_ROOT/usr/bin/trafficserver $RPM_BUILD_ROOT/etc/init.d

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
install -m 644 -p rpm/trafficserver.sysconf \
   $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/trafficserver

# Remove static libs (needs to go to separate -static subpackage if we
# want these:
rm -f $RPM_BUILD_ROOT/%{_libdir}/trafficserver/libtsmgmt.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/trafficserver/libtsutil.a

# Don't include libtool archives:
rm -f $RPM_BUILD_ROOT/%{_libdir}/trafficserver/libtsmgmt.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/trafficserver/libtsutil.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/trafficserver/plugins/*.la

# 
perl -pi -e 's/^CONFIG.*proxy.config.proxy_name STRING.*$/CONFIG proxy.config.proxy_name STRING FIXME.example.com/' \
	$RPM_BUILD_ROOT/etc/trafficserver/records.config
perl -pi -e 's/^CONFIG.*proxy.config.ssl.server.cert.path.*$/CONFIG proxy.config.ssl.server.cert.path STRING \/etc\/pki\/tls\/certs\//' \
	$RPM_BUILD_ROOT/etc/trafficserver/records.config
perl -pi -e 's/^CONFIG.*proxy.config.ssl.server.private_key.path.*$/CONFIG proxy.config.ssl.server.private_key.path STRING \/etc\/pki\/tls\/private\//' \
	$RPM_BUILD_ROOT/etc/trafficserver/records.config

# The clean section  is only needed for EPEL and Fedora < 13
# http://fedoraproject.org/wiki/PackagingGuidelines#.25clean
%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, ats, ats, -)
%doc README CHANGES NOTICE README.fedora LICENSE
%attr(0644, root, root) /usr/share/man/man1/*
%attr(0644, root, root) /usr/share/doc/trafficserver/trafficshell/*
%attr(0755,root,root) /usr/bin/traffic*
%attr(0755,root,root) /usr/bin/ts_remap_check
%attr(0755,root,root) %dir %{_libdir}/trafficserver
%attr(0755,root,root) %dir %{_libdir}/trafficserver/plugins
%attr(0755,root,root) %{_libdir}/trafficserver/*.so.*
%attr(0755,root,root) %{_libdir}/trafficserver/plugins/*.so
%config(noreplace) /etc/trafficserver/*
%attr(0755, root, root) /usr/bin/trafficrecovery.sh
%attr(0755, root, root) /etc/init.d/trafficserver
%attr(0755, ats, ats) %dir /etc/trafficserver
%config(noreplace) %attr(0644, root, root) %{_sysconfdir}/sysconfig/trafficserver
%dir /var/log/trafficserver
%dir /var/run/trafficserver
%dir /var/cache/trafficserver

%post
/sbin/ldconfig
if [ $1 -eq 1 ] ; then
  /sbin/chkconfig --add %{name}
fi

%pre
getent group ats >/dev/null || groupadd -r ats -g 176 &>/dev/null
getent passwd ats >/dev/null || \
useradd -r -u 176 -g ats -d / -s /sbin/nologin \
	-c "Apache Traffic Server" ats &>/dev/null

%preun
if [ $1 -eq 0 ] ; then
  /sbin/service %{name} stop > /dev/null 2>&1
  /sbin/chkconfig --del %{name}
fi

%postun
/sbin/ldconfig
if [ $1 -eq 1 ] ; then
   /sbin/service trafficserver condrestart &>/dev/null || :
fi


%package devel
Summary: Apache Traffic Server development libraries and header files
Group: Development/Libraries
Requires: trafficserver = %{version}-%{release}
%description devel
The trafficserver-devel package include plug-in development libraries and
header files, and Apache httpd style module build system.

%files devel
%defattr(-,root,root,-)
%attr(0755,root,root) /usr/bin/tsxs
%attr(0755,root,root) %dir /usr/include/ts
%attr(0644,root,root) /usr/include/ts/*
%attr(0755,root,root) %dir %{_libdir}/trafficserver
%attr(0755,root,root) %dir %{_libdir}/trafficserver/plugins
%attr(0644,root,root) %{_libdir}/trafficserver/*.so

%changelog
* Mon Nov 26 2012 <zym@apache.org> - 3.2.0-2.0
- first abs version

* Mon Aug 31 2012 <zym@apache.org> - 3.2.0-1.6
- First Range request change to full request and return 416.

* Wed Aug 29 2012 <zym@apache.org> - 3.2.0-1.5
- [TS-1415] return 400 if the length of request hostname is zero
  fix cache empty doc and set a new tag 'C' if CL not match doc_len.
  if strict_round_robin is 1, select_best_http must skip down entry in fail_window.

* Fri Aug 03 2012 <zym@apache.org> - 3.2.0-1.4
- vc_ok_read(vc) will cause connections throttling
  fix a bug in health check in L7
  TS-1351 raw disk cache disabled when system start
  bug that when ts recived more bytes than server_response CL

* Fri Jul 06 2012 <zym@apache.org> - 3.2.0-1.3
- remove the active timeout when releasing the client session
- testing for some stats

* Wed Jun 27 2012 <zym@apache.org> - 3.2.0-1.2
- rebase to 3.2.x
- fix the ram issues in the last porting
- fix the rt issue with cluster purge miss

* Thu Jun 20 2012 <zym@apache.org> - 3.2.0-1.1
- backout range patch from amc, TS-475 TS-1265

* Fri Jun 15 2012 <zym@apache.org> - 3.2.0-1.0
- should be the first work tree, with taobao's patches

* Thu Jun 07 2012 <zym@apache.org> - 3.2.0-1
- init v3.2 release, see tb_changelog for full change

* Sun Feb 12 2012 <zym@apache.org> - 3.0.2-2.2
- Change CFLAGS and CXXFLAGS to $RPM_OPT_FLAGS

* Mon Jan 30 2012 <zym@apache.org> - 3.0.2-2.0
- Update to v3.0.2
- with patch from taobao for ram_cache(TS-1006) and zero size body fix(TS-621)

* Tue Jul 19 2011 <janfrode@tanso.net> - 3.0.1-0
- Update to v3.0.1
- Remove uninstall-hook from trafficserver_make_install.patch, removed in v3.0.1.

* Thu Jun 30 2011 <janfrode@tanso.net> - 3.0.0-6
- Note FIXME's on top.
- Remove .la and static libs.
- mktemp'd buildroot.
- include license

* Mon Jun 27 2011 <janfrode@tanso.net> - 3.0.0-5
- Rename patches to start with trafficserver-.
- Remove odd version macro.
- Clean up mixed-use-of-spaces-and-tabs.

* Wed Jun 23 2011 <janfrode@tanso.net> - 3.0.0-4
- Use dedicated user/group ats/ats.
- Restart on upgrades.

* Thu Jun 16 2011 <zym@apache.org> - 3.0.0-3
- update man pages, sugest from Jan-Frode Myklebust <janfrode@tanso.net>
- patch records.config to fix the crashing with cluster iface is noexist
- cleanup spec file

* Wed Jun 15 2011 <zym@apache.org> - 3.0.0-2
- bump to version 3.0.0 stable release
- cleanup the spec file and patches

* Tue May 24 2011 <yonghao@taobao.com> - 2.1.8-2
- fix tcl linking

* Thu May  5 2011 <yonghao@taobao.com> - 2.1.8-1
- bump to 2.1.8
- comment out wccp

* Fri Apr  1 2011 <yonghao@taobao.com> - 2.1.7-3
- enable wccp and fixed compile warning
- never depends on sqlite and db4, add libz and xz-libs
- fix libary permission, do post ldconfig updates

* Sun Mar 27 2011 <yonghao@taobao.com> - 2.1.7-2
- patch traffic_shell fix

* Tue Mar 22 2011 <yonghao@taobao.com> - 2.1.7-1
- bump to v2.1.7
- fix centos5 building
- drop duplicated patches

* Tue Mar 19 2011 <yonghao@taobao.com> - 2.1.6-2
- fix gcc 4.6 building
- split into -devel package for devel libs
- fix init scripts for rpmlint requirement
- fix install scripts to build in mock, without root privileges

* Tue Mar 01 2011 <yonghao@taobao.com> - 2.1.6-1
- bump to 2.1.6 unstable
- replace config layout name as Fedora

* Thu Nov 18 2010 <yonghao@taobao.com> - 2.1.4
- initial release for public
- original spec file is from neomanontheway@gmail.com
