query = gdkpb.get_variable(pkgconfig: 'gdk_pixbuf_query_loaders')

# A loaders cache that knows only the module we just built.
cache = custom_target('loaders.cache',
  input: loader,
  output: 'loaders.cache',
  command: [query, '@INPUT@'],
  capture: true,
  build_by_default: true)

test_loader = executable('test-loader', 'test-loader.c',
  dependencies: [gdkpb])

test('loader', test_loader,
  args: [meson.current_source_dir() / 'data'],
  env: {'GDK_PIXBUF_MODULE_FILE': cache.full_path()},
  depends: cache)
