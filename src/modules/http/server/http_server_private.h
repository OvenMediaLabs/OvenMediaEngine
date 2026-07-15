//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../http_private.h"

#define OV_LOG_TAG OV_LOG_TAG_PREFIX ".Server"

#define HTTP_CHECK_METHOD(method, flag) OV_CHECK_FLAG((uint16_t)method, (uint16_t)flag)
