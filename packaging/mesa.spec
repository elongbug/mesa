Name:           mesa
Version:        17.1.0
Release:        0
License:        MIT
Summary:        System for rendering interactive 3-D graphics
Url:            http://www.mesa3d.org
Group:          Graphics & UI Framework/Hardware Adaptation
Source:         %{name}-%{version}.tar.gz
Source1001:     %{name}.manifest

BuildRequires:  autoconf >= 2.59
BuildRequires:  automake
BuildRequires:  bison
BuildRequires:  fdupes
BuildRequires:  flex
BuildRequires:  gcc-c++
BuildRequires:  gettext-tools
BuildRequires:  libtool
BuildRequires:  libxml2-python
BuildRequires:  pkgconfig
BuildRequires:  python
BuildRequires:  python-lxml
BuildRequires:  python-xml
BuildRequires:  python-mako
BuildRequires:  pkgconfig(expat)
BuildRequires:  pkgconfig(libdrm) >= 2.4.75
BuildRequires:  pkgconfig(libudev) > 150
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(tpl-egl)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(libtdm)
BuildRequires:  pkgconfig(zlib)

%description
Mesa is a 3-D graphics library with an API which is very similar to
that of OpenGL.* To the extent that Mesa utilizes the OpenGL command
syntax or state machine, it is being used with authorization from
Silicon Graphics, Inc.(SGI). However, the author does not possess an
OpenGL license from SGI, and makes no claim that Mesa is in any way a
compatible replacement for OpenGL or associated with SGI. Those who
want a licensed implementation of OpenGL should contact a licensed
vendor.

Please do not refer to the library as MesaGL (for legal reasons). It's
just Mesa or The Mesa 3-D graphics library.

* OpenGL is a trademark of Silicon Graphics Incorporated.

%prep
%setup -q -n %{name}-%{version}

%build
cp %{SOURCE1001} .

./autogen.sh --prefix=%{_prefix} --enable-gles2 --with-dri-drivers="" --with-egl-platforms=tizen --enable-shared-glapi --with-gallium-drivers=vc4 --disable-glx --disable-dri3 --disable-gbm

make %{?jobs:-j%jobs}

%install
%make_install

mkdir -p %{buildroot}%{_libdir}/driver
cp -a  %{buildroot}%{_libdir}/libEGL* %{buildroot}%{_libdir}/driver
cp -a  %{buildroot}%{_libdir}/libGLES* %{buildroot}%{_libdir}/driver

mkdir -p %{buildroot}/etc/udev/rules.d
cp packaging/99-GPU-Acceleration.rules %{buildroot}/etc/udev/rules.d

%files
%define _unpackaged_files_terminate_build 0
%manifest %{name}.manifest
%defattr(-,root,root)
%{_libdir}/libglapi*
%{_libdir}/driver/*
%{_libdir}/dri/*
/etc/udev/rules.d/99-GPU-Acceleration.rules
