{
  'targets': [
    {
      'target_name': 'httpp',
      'include_dirs': [
        "<!(node -e \"require('nan')\")",
        'src/',
        'uvudt/',
        'uvudt/UDT4/src/'
      ],
      'sources': [
        'src/httpp_addon.cc',
        'src/udthandle_wrap.cc',
        'src/udtstream_wrap.cc',
        'uvudt/UDT4/src/api.cpp',
        'uvudt/UDT4/src/buffer.cpp',
        'uvudt/UDT4/src/cache.cpp',
        'uvudt/UDT4/src/ccc.cpp',
        'uvudt/UDT4/src/channel.cpp',
        'uvudt/UDT4/src/common.cpp',
        'uvudt/UDT4/src/udt_core.cpp',
        'uvudt/UDT4/src/epoll.cpp',
        'uvudt/UDT4/src/list.cpp',
        'uvudt/UDT4/src/md5.cpp',
        'uvudt/UDT4/src/packet.cpp',
        'uvudt/UDT4/src/queue.cpp',
        'uvudt/UDT4/src/udtc.cpp',
        'uvudt/UDT4/src/window.cpp'
      ],
      'conditions': [
        ['OS=="win"', {
            'defines': [
              'UDT_EXPORTS'
            ]
          }
        ],
        ['OS=="linux"', {
            'defines': [
                'LINUX=1'
            ]
          }
        ],
        ['OS in "linux freebsd openbsd solaris android aix cloudabi"', {
            'cflags_cc': [
                '-fexceptions'
            ]
          }
        ],
        ['OS in "mac ios"', {
            'defines': [
                'DARWIN=1'
            ],
            'xcode_settings': {
                'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'              # -fexceptions
            }
        }]
      ]
    }
  ]
}
