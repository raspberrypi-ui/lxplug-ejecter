project(
    'lxplug_ejecter',
    'c',
    version: '0.8.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        'werror=false',
    ],
)

incdir = include_directories('/usr/include/lxpanel')

share_dir = join_paths(get_option('prefix'), 'share')
resource_dir = join_paths(share_dir, 'lxpanel')

gettext_package = meson.project_name()

add_project_arguments('-DPACKAGE_DATA_DIR="' + resource_dir + '"', language : [ 'c', 'cpp' ])
add_project_arguments('-DGETTEXT_PACKAGE="' + gettext_package + '"', language : [ 'c', 'cpp' ])
add_project_arguments('-DPACKAGE_LOCALE_DIR="' + share_dir + '/locale"', language : [ 'c', 'cpp' ])

subdir('src')
subdir('po')
