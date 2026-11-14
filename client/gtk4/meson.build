project('ltm-flight-desktop', 'c', default_options : ['c_std=c11', 'warning_level=2'])

cc = meson.get_compiler('c')
gtk = dependency('gtk4')
json = dependency('json-glib-1.0')
threads = dependency('threads')
deps = [gtk, json, threads]
gnome = import('gnome')
resources = gnome.compile_resources('ltm-resources',
  'resources/ltm.gresource.xml',
  source_dir : 'resources')
if host_machine.system() == 'windows'
  deps += cc.find_library('ws2_32')
endif

executable('ltm-flight',
  ['src/main.c', 'src/net_client.c', resources],
  dependencies : deps,
  install : true)
