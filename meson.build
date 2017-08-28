project(
  'telepathy-account-widgets', 'c',
  version: '0.1',
  license: 'LGPL2.1',
  default_options: [
    'buildtype=debugoptimized',
    'warning_level=1'
  ],
  meson_version: '>= 0.41.0'
)

tpaw_name = 'tpaw'
tpaw_version = meson.project_version()

tpaw_prefix = get_option('prefix')
tpaw_datadir = join_paths(tpaw_prefix, get_option('datadir'))

tpaw_schemadir = join_paths(tpaw_datadir, 'glib-2.0', 'schemas')

enable_unreleased_checks = get_option('enable-unreleased-checks')
enable_settings = get_option('enable-settings')
enable_coding_style_checks = get_option('enable-coding-style-checks')

# Allow to overwrite where to install data files
tpaw_pkgdatadir = get_option('with-pkgdatadir').strip()
if tpaw_pkgdatadir == ''
  tpaw_pkgdatadir = join_paths(tpaw_datadir, meson.project_name())
endif

tpaw_icondir = get_option('with-icondir').strip()
if tpaw_icondir == ''
  tpaw_icondir = join_paths(tpaw_pkgdatadir, 'icons')
endif

tpaw_debug = get_option('buildtype').contains('debug')

tpaw_gettext_package = get_option('with-gettext-package').strip()
if tpaw_gettext_package == ''
  tpaw_gettext_package = tpaw_name
endif

cc = meson.get_compiler('c')

config_h = configuration_data()

# defines
set_defines = [
  # package
  ['PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/browse.cgi?product=empathy'],
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), tpaw_version)],
  ['PACKAGE_TARNAME', meson.project_name()],
  ['PACKAGE_URL', ''],
  ['PACKAGE_VERSION', tpaw_version],
  # i18n
  ['GETTEXT_PACKAGE', tpaw_gettext_package]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# debug options
config_h.set('ENABLE_DEBUG', tpaw_debug)

uoa_provider = 'im.telepathy.Account.Storage.UOA'

# library options
set_values = [
  # glib
  ['GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_34', 'Prevent post 2.34 APIs'],
  ['GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_30', 'Ignore post 2.30 deprecations'],
  # gtk
  ['GDK_VERSION_MAX_ALLOWED', 'GDK_VERSION_3_4', 'Prevent post 3.4 APIs'],
  ['GDK_VERSION_MIN_REQUIRED', 'GDK_VERSION_3_4', 'Ignore post 3.4 deprecations'],
  # telepathy
  ['TP_VERSION_MIN_REQUIRED', 'TP_VERSION_0_22', 'Ignore post 0.22 deprecations'],
  ['TP_VERSION_MAX_ALLOWED', 'TP_VERSION_0_22', 'Prevent post 0.22 APIs'],
  ['TP_SEAL_ENABLE', 1, 'Prevent to use sealed variables'],
  ['TP_DISABLE_SINGLE_INCLUDE', 1, 'Disable single include header'],
  # UOA configuration bits
  ['TPAW_UOA_PROVIDER', uoa_provider, 'Name of provider for accounts imported from libaccounts']
]

foreach values: set_values
  config_h.set(values[0], values[1], description: values[2])
endforeach

# headers
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
]

foreach header: check_headers
  config_h.set(header[0], cc.has_header(header[1]))
endforeach

# compiler flags
common_flags = [
  '-DHAVE_CONFIG_H',
  '-DDATADIR="@0@"'.format(tpaw_datadir),
  '-DPKGDATADIR="@0@"'.format(tpaw_pkgdatadir),
  '-DICONDIR="@0@"'.format(tpaw_icondir),
  '-DG_LOG_DOMAIN="tp-account-widgets"',
  '-DGCR_API_SUBJECT_TO_CHANGE'
]
compiler_flags = []

if tpaw_debug
  test_flags = [
    '-Wdeclaration-after-statement',
    '-Wmissing-declarations',
    '-Wmissing-prototypes',
    '-Wshadow',
    '-Wstrict-prototypes'
  ]

  if enable_unreleased_checks
    test_flags = [
      '-Wno-missing-field-initializers',
      '-Wno-unused-parameter'
    ]
  endif

  foreach flag: test_flags
    if cc.has_argument(flag)
      compiler_flags += [flag]
    endif
  endforeach
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')

glib_req_version = '>= 2.33.3'
gtk_req_version = '>= 3.5.1'
libsecret_req_version = '>= 0.5'
telepathy_glib_req_version = '>= 0.22.0'

required = [
  'pkg-config >= 0.21',
  'dbus-glib-1',
  'gio-2.0 ' + glib_req_version,
  'glib-2.0 ' + glib_req_version,
  'gobject-2.0 ' + glib_req_version,
  'gtk+-3.0 ' + gtk_req_version,
  'libsecret-1 ' + libsecret_req_version,
  'telepathy-glib ' + telepathy_glib_req_version,
  'libxml-2.0'
]

tpaw_deps = [
  dependency('dbus-glib-1'),
  dependency('gio-2.0', version: glib_req_version),
  dependency('glib-2.0', version: glib_req_version),
  dependency('gobject-2.0', version: glib_req_version),
  dependency('gtk+-3.0', version: gtk_req_version),
  dependency('libsecret-1', version: libsecret_req_version),
  dependency('libxml-2.0'),
  dependency('telepathy-glib', version: telepathy_glib_req_version)
]

# *** Check for gudev ***
enable_gudev = get_option('enable-gudev')
have_gudev = false

if enable_gudev != 'no'
  gudev_dep = dependency('gudev-1.0', required: (enable_gudev == 'yes'))
  have_gudev = gudev_dep.found()
  tpaw_deps += gudev_dep

  if have_gudev
    required += 'gudev-1.0'
  endif
endif

config_h.set('HAVE_UDEV', have_gudev)

# Cheese (optional dependency for avatar selection)
enable_cheese = get_option('with-cheese')
have_cheese = false

if enable_cheese != 'no'
  cheese_dep = dependency('cheese-gtk', required: (enable_cheese == 'yes'))
  have_cheese = cheese_dep.found()
  tpaw_deps += cheese_dep

  if have_cheese
    required += 'cheese-gtk'
  endif
endif

config_h.set('HAVE_CHESE', have_cheese)

# ubuntu-online-accounts support
enable_uoa = get_option('enable-ubuntu-online-accounts')
have_uoa = false

if enable_uoa != 'no'
  missing_deps = []

  account_plugin_dep = dependency('account-plugin', required: false)
  if not account_plugin_dep.found()
    missing_deps += 'account-plugin'
  endif

  mission_control_plugins_dep = dependency('mission-control-plugins', version: '>= 5.13.1', required: false)
  if not mission_control_plugins_dep.found()
    missing_deps += 'mission-control-plugins'
  endif

  libaccounts_glib_dep = dependency('libaccounts-glib', version: '>= 1.4', required: false)
  if not libaccounts_glib_dep.found()
    missing_deps += 'libaccounts-glib'
  endif

  libsignon_glib_dep = dependency('libsignon-glib', version: '>= 1.8', required: false)
  if not libsignon_glib_dep.found()
    missing_deps += 'libsignon-glib'
  endif

  have_uoa = (missing_deps.length() == 0)

  if have_uoa
    accounts_provider_plugindir = account_plugin_dep.get_pkgconfig_variable('provider_plugindir')
    message(accounts_provider_plugindir)

    accounts_app_plugindir = account_plugin_dep.get_pkgconfig_variable('application_plugindir')
    message(accounts_app_plugindir)

    accounts_provider_filesdir = libaccounts_glib_dep.get_pkgconfig_variable('providerfilesdir')
    message(accounts_provider_filesdir)

    accounts_service_filesdir = libaccounts_glib_dep.get_pkgconfig_variable('servicefilesdir')
    message(accounts_service_filesdir)

    accounts_app_filesdir = libaccounts_glib_dep.get_pkgconfig_variable('applicationfilesdir')
    message(accounts_app_filesdir)

    mcp_abi_version = mission_control_plugins_dep.get_pkgconfig_variable('MCP_ABI_VERSION')
    mission_control_pluginsdir = join_paths(tpaw_libdir, 'mission-control-plugins.', mcp_abi_version)
    message(mission_control_pluginsdir)

    tpaw_deps += [
      account_plugin_dep,
      mission_control_plugins_dep,
      libaccounts_glib_dep,
      libsignon_glib_dep
    ]
  else
    str = 'Could not find Ubuntu Online Accounts dependencies: ' + ' '.join(missing_deps)
    if enable_uoa == 'yes'
      error(str)
    endif
    message(str)
  endif
endif

configure_file(
  output: 'config.h',
  configuration: config_h
)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

top_inc = include_directories('.')

subdir('tools')
subdir('po')
subdir('data')
subdir('tp-account-widgets')

if enable_settings
  meson.add_install_script('meson_post_install.py', tpaw_icondir, tpaw_schemadir)
else
  meson.add_install_script('meson_post_install.py', tpaw_icondir)
endif

# FIXME: These are not fully working due to unspecified working dir
# Coding style checks
'''
if enable_coding_style_checks
  res = run_command(check_misc, sources)
  res = run_command(check_c_style, sources + headers)
  if res.returncode() != 0
    message(res.stdout())
    message(res.stderr())
  endif
endif
'''

output = '\nConfigure summary:\n\n'
output += '        Compiler....................:  ' + cc.get_id() + '\n'
output += '        Compiler Flags..............:  ' + ' '.join(compiler_flags) + '\n'
# FIXME: set linker flags
#output += '        Linker Flags................:  \n'
output += '        Prefix......................:  ' + tpaw_prefix + '\n'
output += '        Data directory .............:  ' + tpaw_pkgdatadir + '\n'
output += '        Icon directory .............:  ' + tpaw_icondir + '\n'
output += '        GSettings schema support....:  ' + enable_settings.to_string() + '\n'
output += '        Coding style checks.........:  ' + enable_coding_style_checks.to_string() + '\n'
output += '        Unreleased version checks...:  ' + enable_unreleased_checks.to_string() + '\n'
output += '        GETTEXT_PACKAGE.............:  ' + tpaw_gettext_package + '\n\n'
output += '    Features:\n'
output += '        Cheese webcam support ......:  ' + have_cheese.to_string() + '\n'
output += '        Camera monitoring...........:  ' + have_gudev.to_string() + '\n'
output += '        Ubuntu Online plugins.......:  ' + have_uoa.to_string() + '\n'
message(output)
