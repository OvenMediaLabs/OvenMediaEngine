#pragma once

// https://www.ietf.org/archive/id/draft-deping-avtcore-video-bframe-01.html

#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension.h"

// 0                   1                   2
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | ID    | Len=2 |              cts              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// a=extmap:4 uri:ietf:rtc:rtp-hdrext:video:CompositionTime

