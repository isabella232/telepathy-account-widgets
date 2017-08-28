env_conf = configuration_data()
env_conf.set('abs_top_builddir', meson.build_root())

env = 'telepathy-glib-env'

configure_file(
  input: env + '.in',
  output: env,
  configuration: env_conf
)

check_misc = find_program('check-misc.sh')
check_c_style = find_program('check-c-style.sh')
