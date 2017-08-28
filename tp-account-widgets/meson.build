install_data(
  'irc-networks.xml',
  install_dir: tpaw_pkgdatadir
)

headers = files(
  'tpaw-account-settings.h',
  'tpaw-account-widget.h',
  'tpaw-account-widget-irc.h',
  'tpaw-account-widget-sip.h',
  'tpaw-avatar-chooser.h',
  'tpaw-builder.h',
  'tpaw-calendar-button.h',
  'tpaw-camera-monitor.h',
  'tpaw-connection-managers.h',
  'tpaw-contactinfo-utils.h',
  'tpaw-debug.h',
  'tpaw-gsettings.h',
  'tpaw-images.h',
  'tpaw-keyring.h',
  'tpaw-irc-network-chooser-dialog.h',
  'tpaw-irc-network-chooser.h',
  'tpaw-irc-network-dialog.h',
  'tpaw-irc-network-manager.h',
  'tpaw-irc-network.h',
  'tpaw-irc-server.h',
  'tpaw-live-search.h',
  'tpaw-pixbuf-utils.h',
  'tpaw-protocol.h',
  'tpaw-string-parser.h',
  'tpaw-time.h',
  'tpaw-user-info.h',
  'tpaw-utils.h'
)

sources = files(
  'tpaw-account-settings.c',
  'tpaw-account-widget.c',
  'tpaw-account-widget-irc.c',
  'tpaw-account-widget-private.h',
  'tpaw-account-widget-sip.c',
  'tpaw-avatar-chooser.c',
  'tpaw-builder.c',
  'tpaw-calendar-button.c',
  'tpaw-camera-monitor.c',
  'tpaw-connection-managers.c',
  'tpaw-contactinfo-utils.c',
  'tpaw-debug.c',
  'tpaw-keyring.c',
  'tpaw-irc-network-chooser.c',
  'tpaw-irc-network-chooser-dialog.c',
  'tpaw-irc-network-dialog.c',
  'tpaw-irc-network-manager.c',
  'tpaw-irc-network.c',
  'tpaw-irc-server.c',
  'tpaw-live-search.c',
  'tpaw-pixbuf-utils.c',
  'tpaw-protocol.c',
  'tpaw-string-parser.c',
  'tpaw-time.c',
  'tpaw-user-info.c',
  'tpaw-utils.c'
)

# nocheck sources
sources += files(
  'cheese-camera-device-monitor.c',
  'totem-subtitle-encoding.c'
)

# these are sources that depend on Ubuntu Online Accounts
if have_uoa
  sources = files('tpaw-uoa-utils.c')
endif

resource_data = files(
  'tpaw-account-widget-aim.ui',
  'tpaw-account-widget-generic.ui',
  'tpaw-account-widget-groupwise.ui',
  'tpaw-account-widget-icq.ui',
  'tpaw-account-widget-irc.ui',
  'tpaw-account-widget-jabber.ui',
  'tpaw-account-widget-local-xmpp.ui',
  'tpaw-account-widget-msn.ui',
  'tpaw-account-widget-sip.ui',
  'tpaw-account-widget-yahoo.ui',
  'tpaw-irc-networks.dtd'
)

built_sources = gnome.compile_resources(
  'tpaw-account-widgets-resources',
  'tpaw-account-widgets.gresource.xml',
  source_dir: '.',
  dependencies: resource_data
)

libtp_account_widgets = static_library(
  'tp-account-widgets',
  sources: sources + built_sources,
  include_directories: top_inc,
  dependencies: tpaw_deps
)

libtp_account_widgets_dep = declare_dependency(
  link_with: libtp_account_widgets,
  include_directories: include_directories('.')
)

pkg.generate(
  libraries: libtp_account_widgets,
  version: tpaw_version,
  name: 'telepathy-account-widgets (uninstalled copy)',
  description: 'Widgets and utility functions for configuring Telepathy accounts',
  filebase: 'telepathy-account-widgets-uninstalled',
  requires: ', '.join(required),
  variables: [
    'abs_top_srcdir=' + meson.source_root(),
    'abs_top_builddir=' + meson.build_root(),
    'uoa_provider=' + uoa_provider
  ],
  install_dir: meson.build_root()
)
