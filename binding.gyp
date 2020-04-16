{
  'targets': [
    {
      'target_name': 'httpp',
      'include_dirs': [
        'src/',
        'uvudt/',
        'uvudt/UDT4/src/',
      ],
      'sources': [
        'src/httpp.cc',
        'src/udt_wrap.cc',
        'src/udtstream_wrap.cc',
        'src/udthandle_wrap.cc',
        'uvudt/uvudt.c',
        'uvudt/udtstream.c'
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
        'uvudt/UDT4/src/window.cpp',
      ],
      'conditions': [
        ['OS=="win"',
          {
            'defines': [
              'UDT_EXPORTS',
            ],
            'link_settings': {
              'libraries': [
                '-lws2_32.lib',
                '-lpsapi.lib',
                '-liphlpapi.lib',
                '-lwsock32.lib'
              ],
            },
          },
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
            ],
            'libraries': [
              '-lm',
              '-lstdc++',
              '-lpthread',
            ],
          },
        ],
        ['OS=="linux"', 
          {
              'cflags_cc': [ '-DLINUX=1' ],
          },
        ],
        ['OS in "mac ios"',
          {
              'cflags_cc': [ '-DDARWIN=1' ],
          },
        ],
      ],
    },
  ],
}
