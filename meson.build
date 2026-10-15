project('webm-pixbuf-loader', ['c', 'cpp'],
  version: '0.1.0',
  license: 'BSD-2-Clause',
  meson_version: '>=0.56',
  default_options: ['c_std=c11', 'cpp_std=c++14', 'warning_level=2'])

cxx = meson.get_compiler('cpp')

gdkpb = dependency('gdk-pixbuf-2.0', version: '>=2.36')
gmodule = dependency('gmodule-2.0')

moddir = get_option('gdk_pixbuf_moduledir')
if moddir == ''
  moddir = gdkpb.get_variable(pkgconfig: 'gdk_pixbuf_moduledir',
    pkgconfig_define: ['prefix', get_option('prefix')])
endif

# libwebm only ships a CMake package, no pkg-config file.
webm = cxx.find_library('webm')
if not cxx.has_header('webm/webm_parser.h')
  error('libwebm headers (webm/webm_parser.h) not found')
endif

# Both decoders are optional for now, but are expected to become mandatory:
# nobody who wants WebM in gdk-pixbuf wants only some of the codecs.
vpx = dependency('vpx', required: get_option('vpx'))
dav1d = dependency('dav1d', required: get_option('dav1d'))
if not vpx.found() and not dav1d.found()
  error('Need at least one of libvpx and dav1d')
endif

cdata = configuration_data()
if vpx.found()
  cdata.set('HAVE_VPX', 1)
endif
if dav1d.found()
  cdata.set('HAVE_DAV1D', 1)
endif
config_h = configure_file(output: 'config.h', configuration: cdata)
add_project_arguments('-include', meson.current_build_dir() / 'config.h',
  language: ['c', 'cpp'])

loader = shared_module('pixbufloader-webm',
  'src/io-webm.cc',
  'src/io-webm-animation.cc',
  'src/player.cc',
  'src/demux.cc',
  'src/decoder.cc',
  'src/convert.cc',
  dependencies: [gdkpb, gmodule, webm, vpx, dav1d],
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: moddir)

subdir('tests')

if get_option('update_cache')
  query = gdkpb.get_variable(pkgconfig: 'gdk_pixbuf_query_loaders')
  meson.add_install_script('sh', '-c',
    'test -n "$DESTDIR" || ' + query + ' --update-cache')
endif
