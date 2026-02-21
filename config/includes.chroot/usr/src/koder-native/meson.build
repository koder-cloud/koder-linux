project('kterm', 'c',
  version: '0.3.3',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 1.3',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

i18n = import('i18n')

gtk4 = dependency('gtk4', version: '>= 4.14')
vte = dependency('vte-2.91-gtk4', version: '>= 0.76')
gnome_desktop = dependency('gnome-desktop-4')
json_glib = dependency('json-glib-1.0')
webkitgtk = dependency('webkitgtk-6.0')

sources = files(
  'src/main.c',
  'src/kterm-application.c',
  'src/kterm-window.c',
  'src/kterm-terminal.c',
  'src/kterm-explorer.c',
  'src/kterm-browser.c',
  'src/kterm-settings.c',
  'src/kterm-preferences.c',
)

localedir = get_option('prefix') / get_option('localedir')

executable('kterm', sources,
  dependencies: [gtk4, vte, gnome_desktop, json_glib, webkitgtk],
  c_args: [
    '-DKTERM_VERSION="' + meson.project_version() + '"',
    '-DGETTEXT_PACKAGE="kterm"',
    '-DLOCALEDIR="' + localedir + '"',
  ],
  install: true,
)

subdir('po')

install_data('data/com.koder.kterm.desktop.in',
  rename: 'com.koder.kterm.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data('data/com.koder.kterm.metainfo.xml.in',
  rename: 'com.koder.kterm.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)

foreach size : ['48x48', '128x128', '256x256', '512x512']
  install_data(
    'data/icons/hicolor' / size / 'apps' / 'com.koder.kterm.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size / 'apps',
  )
endforeach
