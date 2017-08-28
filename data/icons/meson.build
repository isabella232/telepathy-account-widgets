common_app_names = [
  'im-aim',
  'im-ekiga',
  'im-gizmo',
  'im-google-talk',
  'im-icq',
  'im-irc',
  'im-jabber',
  'im-msn',
  'im-silc',
  'im-sip',
  'im-local-xmpp',
  'im-gadugadu',
  'im-groupwise',
  'im-qq',
  'im-meanwhile',
  'im-mxit',
  'im-myspace',
  'im-sametime',
  'im-yahoo',
  'im-zephyr'
]

complete_app_names = common_app_names + [
  'im-facebook',
  'im-skype'
]

icons = [
  ['16x16', complete_app_names, 'png'],
  ['22x22', complete_app_names, 'png'],
  ['24x24', common_app_names, 'png'],
  ['32x32', common_app_names, 'png'],
  ['48x48', complete_app_names, 'png'],
  ['scalable', common_app_names, 'svg']
]

foreach icon: icons
  foreach name: icon[1]
    install_data(
      '_'.join(['hicolor', 'apps', icon[0], name + '.' + icon[2]]),
      install_dir: join_paths(tpaw_icondir, 'hicolor', icon[0], 'apps')
    )
  endforeach
endforeach
