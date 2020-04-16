{
  'targets': [
    {
      'target_name': 'httpp',
      'include_dirs': [
        'src/UDT4/src/',
        'src/',
      ],
      'sources': [
        'src/udt_wrap.cc',
        'src/udt_wrap.h',
        'src/stream_wrap.h',
        'src/handle_wrap.h',
        'src/req_wrap.h',
        'src/node_internals.h',
        'src/uvudt.h',
        'src/UDT4/src/udtc.h',
        'src/UDT4/src/api.cpp',
        'src/UDT4/src/buffer.cpp',
        'src/UDT4/src/cache.cpp',
        'src/UDT4/src/ccc.cpp',
        'src/UDT4/src/channel.cpp',
        'src/UDT4/src/common.cpp',
        'src/UDT4/src/udt_core.cpp',
        'src/UDT4/src/epoll.cpp',
        'src/UDT4/src/list.cpp',
        'src/UDT4/src/md5.cpp',
        'src/UDT4/src/packet.cpp',
        'src/UDT4/src/queue.cpp',
        'src/UDT4/src/udtc.cpp',
        'src/UDT4/src/window.cpp',
      ],
      'conditions': [
        ['OS=="win"',
          {
            'defines': [
              'EVPIPE_OSFD',
              'UDT_EXPORTS',
            ],
            'sources': [
              'src/uvudt_win.c',
            ],
            'link_settings': {
              'libraries': [
                '-lws2_32.lib',
                '-lpsapi.lib',
                '-liphlpapi.lib',
                '-lwsock32.lib'
              ],
            },
          }
        ],
        ['OS!="win"',
          {
             'cflags_cc': [
              '-pedantic',
              '-Wall',
              '-Wextra',
              '-Wno-unused-parameter'
              '-finline-functions',
              '-fno-strict-aliasing',
              '-fvisibility=hidden',
              '-DLINUX',
              '-DEVPIPE_OSFD',
              '-frtti',
              '-fexceptions',
            ],
            'sources': [
              'src/uvudt_unix.c',
            ],
            'libraries': [
              '-lm',
              '-lstdc++',
              '-lpthread',
            ]
          }
        ]
      ]
    }
  ]
}
