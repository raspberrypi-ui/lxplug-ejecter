name = 'ejecter'

sources = files(
  'ejecter.c'
)

gtk = dependency('gtk+-3.0')
deps = [ gtk ]

shared_module(name, sources,
        dependencies: deps,
        install: true,
        install_dir: get_option('libdir') / 'lxpanel/plugins',
        include_directories : incdir,
        name_prefix: ''
)
