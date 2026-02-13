//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "ovlibrary.h"

namespace ov
{
    class Hex
    {
    public:
        static String Encode(const void *data, size_t length);
        static String Encode(const std::shared_ptr<ov::Data> &data);

        // Automatically remove hyphens to support UUID format.
        static std::shared_ptr<ov::Data> Decode(const ov::String &hex);
    };
}