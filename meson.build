project('libwebrtc-c', ['c', 'cpp'],
  version: '0.1.0',
  license: 'Apache-2.0',
  meson_version: '>= 1.0.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=debugoptimized',
  ])

# webrtc-sdk/libwebrtc (the portable C++ wrapper, libwebrtc.pc). Its Cflags carry
# RTC_DESKTOP_DEVICE, which changes RTCPeerConnectionFactory's vtable: always build with them.
webrtc_dep = dependency('libwebrtc')

cpp_args = ['-DLWRTC_BUILDING']
if host_machine.system() == 'windows'
  cpp_args += ['-DLIB_WEBRTC_API_DLL']
endif

inc = include_directories('include')

# only the C API: libstdc++'s template instantiations would be exported too
link_args = []
link_depends = []
if host_machine.system() not in ['windows', 'darwin']
  link_args += ['-Wl,--version-script=' + meson.current_source_dir() / 'src/lwrtc.map']
  link_depends += ['src/lwrtc.map']
endif

lwrtc_lib = library('webrtc-c',
  'src/lwrtc.cpp',
  'src/media.cpp',
  'src/peer_connection.cpp',
  'src/rtp.cpp',
  include_directories: inc,
  dependencies: webrtc_dep,
  cpp_args: cpp_args,
  gnu_symbol_visibility: 'hidden',
  link_args: link_args,
  link_depends: link_depends,
  version: meson.project_version(),
  install: true)

install_headers('include/lwrtc/lwrtc.h', subdir: 'lwrtc')

pkg = import('pkgconfig')
pkg.generate(lwrtc_lib,
  name: 'libwebrtc-c',
  description: 'C API over libwebrtc for FreeRDP\'s multimedia redirection')

lwrtc_dep = declare_dependency(link_with: lwrtc_lib, include_directories: inc)
meson.override_dependency('libwebrtc-c', lwrtc_dep)

if get_option('tests')
  loopback = executable('lwrtc-loopback', 'tests/loopback.c',
    dependencies: [lwrtc_dep, dependency('threads')])
  test('loopback', loopback, timeout: 60)
endif
