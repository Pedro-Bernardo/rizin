executable('rz-agent', 'rz-agent.c',
  c_args: executable_cflags,
  include_directories: [platform_inc],
  dependencies: [
    rz_util_dep,
    rz_main_dep,
    rz_socket_dep,
    rz_cons_dep
  ],
  install: true,
  install_rpath: rpath_exe,
  implicit_include_directories: false,
  link_args: executable_linkflags
)
