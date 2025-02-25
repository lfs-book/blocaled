# main_src is used in test directory
main_src = files('main.c')
# The other sources are only used here
localed_src = [
  'localed.c',
  'shellparser.c',
  'polkitasync.c',
  main_src,
]
gdbus_src = gnome.gdbus_codegen('locale1-generated',
  interface_prefix: 'org.freedesktop.',
  sources: meson.project_source_root()+ '/data/org.freedesktop.locale1.xml',
  namespace: 'BLocaled',
)
blocaled_exe = executable('blocaled', localed_src, gdbus_src,
  dependencies: deps,
  include_directories: top_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)
# Needed for building mylocaled in tests directory
blocaled_objs = blocaled_exe.extract_objects(
  gdbus_src,
  'localed.c',
  'polkitasync.c',
  'shellparser.c',
  )
src_inc=include_directories('.')
