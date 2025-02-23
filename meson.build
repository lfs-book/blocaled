project('blocaled', 'c', version: '0.7')
package_str=meson.project_name()+' '+meson.project_version()
cc=meson.get_compiler('c')

glib_ver='>=2.61'
dbus_dep=dependency('dbus-1')
polkit_dep=dependency('polkit-gobject-1')
deps= [
  dependency('gio-unix-2.0', version: glib_ver),
  dependency('gio-2.0', version: glib_ver),
  dependency('glib-2.0', version: glib_ver),
  dbus_dep,
  polkit_dep,
  dependency('libdaemon'),
]

gnome=import('gnome')

#Options
prefix=get_option('prefix')
sysconfdir=get_option('sysconfdir')
pidfile=get_option('pidfile')
localeconfig=get_option('localeconfig')
keyboardconfig=get_option('keyboardconfig')
xkbdconfig=get_option('xkbdconfig')
pkgdatadir=prefix / get_option('datadir') / meson.project_name()

#dbus and polkit directories
dbusinterfacesdir=dbus_dep.get_variable(pkgconfig: 'interfaces_dir',
                                pkgconfig_define: ['prefix',prefix])
dbussystemservicesdir=dbus_dep.get_variable(pkgconfig: 'system_bus_services_dir',
                                pkgconfig_define: ['prefix',prefix])
polkitactiondir=polkit_dep.get_variable(pkgconfig: 'actiondir',
                                pkgconfig_define: ['prefix',prefix])

#Configuration in config.h (very different from autotools)
config_h=configuration_data()
config_h.set_quoted('PACKAGE_STRING',package_str)
config_h.set_quoted('PIDFILE',pidfile)
config_h.set_quoted('SYSCONFDIR',sysconfdir)
config_h.set_quoted('LOCALECONFIG',localeconfig)
config_h.set_quoted('KEYBOARDCONFIG',keyboardconfig)
config_h.set_quoted('XKBDCONFIG',xkbdconfig)
config_h.set_quoted('PKGDATADIR',pkgdatadir)
configure_file(configuration:config_h, output:'config.h')
top_inc=include_directories('.')

subdir('src')
subdir('data')
subdir('tests')

summary('prefix',prefix,section:package_str)
summary('sysconfdir',sysconfdir,section:package_str)
summary('pid file',pidfile,section:package_str)
summary('locale config file',localeconfig,section:package_str)
summary('keyboard config file',keyboardconfig,section:package_str)
summary('X11 keyboard config file',xkbdconfig,section:package_str)
summary('compiler',cc.get_id(),section:package_str)
