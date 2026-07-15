//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Benjamin
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#define PIPE_READ           0
#define PIPE_WRITE          1

#define PRC_SUCCESS         "1"
#define PRC_FAIL            "0"

#define OV_PID_FILE         "/var/run/ovenmediaengine.pid"

namespace ov
{
    class Daemon
    {
    public:

        enum class State
        {
            PARENT_SUCCESS,
            CHILD_SUCCESS,
            PIPE_FAIL,
            FORK_FAIL,
            PARENT_FAIL,
            CHILD_FAIL,
        };

        Daemon() = default;
        virtual ~Daemon() = default;

        static State Initialize(const char* pid_path = OV_PID_FILE);
        static void SetEvent(bool success = true);

    private:
        static int _pipe_event[2];
    };
}

