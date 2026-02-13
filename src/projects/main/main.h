//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "git_info.h"

// Version format: [MAJOR].[MINOR].[MICRO]
// Major changes
#define OME_VERSION_MAJOR               0
// Minor changes
#define OME_VERSION_MINOR               20
// Micro changes
#define OME_VERSION_MICRO               0

#define OME_STR_INTERNAL(x)             # x
#define OME_STR(x)                      OME_STR_INTERNAL(x)
#define OME_VERSION                     OME_STR(OME_VERSION_MAJOR) "." OME_STR(OME_VERSION_MINOR) "." OME_STR(OME_VERSION_MICRO)
