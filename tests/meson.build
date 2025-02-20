blocaled_tests = [
  'locale-read',
  'keyboard-read',
  'xkbd-read',
  'locale-write',
  'keyboard-write',
  'keyboard-write-no-file',
  'keyboard-write-no-dir',
  'locale-write-no-file',
  'locale-write-no-dir',
  'locale-write-no-cr',
  'locale-write-bogus-var',
  'locale-write-twice',
  'locale-write-comment',
  'locale-erase-write',
  'locale-read-userconf',
  'locale-read-unreadable',
  'keyboard-read-userconf',
  'xkbd-read-userconf',
  'xkbd-write-no-file',
  'xkbd-write-no-dir',
  'xkbd-write',
  'bad-read-userconf',
  'bad-locale-read',
  'bad-model-map',
  'try-options',
  ]

#Auxiliary programs
#Configuration in ./config.h (different from top config.h)
scratch_dir = meson.current_build_dir() / 'scratch'
test_config_h=configuration_data()
test_config_h.set_quoted('PACKAGE_STRING',package_str)
test_config_h.set_quoted('PIDFILE',scratch_dir / 'mylocaled.pid')
test_config_h.set_quoted('SYSCONFDIR',scratch_dir)
test_config_h.set_quoted('LOCALECONFIG',scratch_dir / 'locale')
test_config_h.set_quoted('KEYBOARDCONFIG',scratch_dir / 'keyboard')
test_config_h.set_quoted('XKBDCONFIG',scratch_dir / 'xkeyboard')
test_config_h.set_quoted('PKGDATADIR',scratch_dir)
configure_file(configuration:test_config_h, output:'config.h')

mylocaled_exe = executable(
  'mylocaled',
  meson.project_source_root() / 'src/main.c',
  dependencies: deps,
  include_directories: src_inc,
  install: false,
  objects: blocaled_objs,
  build_by_default: false,
)
gdbus_mock_polkit_exe = executable('gdbus-mock-polkit', 'gdbus-mock-polkit.c',
  dependencies: deps,
  build_by_default: false,
)
#Configure files in scratch directory
subdir('scratch')

#Running tests
foreach t: blocaled_tests
  test(t,files(t),
    depends:[mylocaled_exe,gdbus_mock_polkit_exe],
    env:['PACKAGE_STRING='+package_str, 'LANG=en_US.UTF-8', 'srcdir='+meson.current_source_dir()],
    is_parallel:false,
    workdir:meson.current_build_dir(),
  )
endforeach
