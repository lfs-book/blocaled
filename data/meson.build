man_cfg=configuration_data()
man_cfg.set('sysconfdir',sysconfdir)
blocaled_8=configure_file(
  input:'blocaled.8.in',
  output:'blocaled.8',
  configuration:man_cfg
)
install_man(blocaled_8)

conf_cfg=configuration_data()
conf_cfg.set('localeconfig',localeconfig)
conf_cfg.set('keyboardconfig',keyboardconfig)
conf_cfg.set('xkbdconfig',xkbdconfig)
blocaled_conf = configure_file(
  input: 'blocaled.conf.in',
  output: 'blocaled.conf',
  configuration:conf_cfg
)
install_data(blocaled_conf, install_dir: sysconfdir)

serv_cfg=configuration_data()
serv_cfg.set('libexecdir', prefix + '/' + get_option('libexecdir'))
service = configure_file(
  input: 'org.freedesktop.locale1.service.in',
  output: 'org.freedesktop.locale1.service',
  configuration:serv_cfg
)
install_data(
  service,
  install_dir: dbussystemservicesdir,
)
install_data(
  'org.freedesktop.locale1.xml',
  install_dir: dbusinterfacesdir,
)
install_data(
  'org.freedesktop.locale1.conf',
  install_dir: get_option('sysconfdir') + '/dbus-1/system.d',
)
install_data(
  'org.freedesktop.locale1.policy',
  install_dir: polkitactiondir,
)
install_data(
  'kbd-model-map',
  install_dir: pkgdatadir,
)
