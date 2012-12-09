# Copyright (c) 2012 Tomas Gunnarsson. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'afx2lg',
      'type': 'executable',
      'defines': [
      ],
      'include_dirs': [
        '..',
      ],
      'dependencies': [
        '../axefx/axefx.gyp:axefx',
        '../lg/lg.gyp:lg',
      ],
      'sources': [
        '../common/common_types.h',
        'main.cc',
      ],
    },
  ],
}
