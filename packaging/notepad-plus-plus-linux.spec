Name:           notepad-plus-plus-linux
Version:        0.1.0
Release:        0.1.alpha1%{?dist}
Summary:        Experimental native Linux port of Notepad++

License:        GPL-3.0-or-later
URL:            https://github.com/aedamasceno/notepad-plus-plus
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qt5compat-devel
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

%description
An experimental native Linux port of Notepad++ using Qt 6 and Scintilla.

This alpha release is intended for testing and development feedback.
It is not yet feature-complete and should not be considered production-ready.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/notepad-plus-plus.desktop
appstream-util validate-relax --nonet \
    %{buildroot}%{_metainfodir}/io.github.aedamasceno.notepad-plus-plus.metainfo.xml

%files
%license LICENSE
%doc README.md LINUX-PORT.md
%{_bindir}/npp_linux
%{_datadir}/applications/notepad-plus-plus.desktop
%{_datadir}/icons/hicolor/256x256/apps/notepad-plus-plus.png
%{_metainfodir}/io.github.aedamasceno.notepad-plus-plus.metainfo.xml

%changelog
* Wed Aug 19 2026 Emanuel Damasceno <aedamasceno@gmail.com> - 0.1.0-0.1.alpha1
- Initial experimental Linux alpha release
