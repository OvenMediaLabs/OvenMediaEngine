//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Benjamin
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstring>

#include "daemon.h"

namespace ov
{
    int Daemon::_pipe_event[2];

    Daemon::State Daemon::Initialize(const char* pid_path)
    {
        char result[2] = {0,};

        if (::pipe(_pipe_event) == -1)
        {
            return State::PIPE_FAIL;
        }

        pid_t pid = fork();

        if (pid < 0)
        {
            return State::FORK_FAIL;
        }
        else if (pid > 0)
        {
            ::close(_pipe_event[PIPE_WRITE]);

            int pid_file = ::open(pid_path, O_RDWR|O_CREAT, 0644);
            if (pid_file < 0)
            {
                return State::PARENT_FAIL;
            }

            std::string pid_str = std::to_string(pid);
            ssize_t bytes_write = ::write(pid_file, pid_str.c_str(), pid_str.size());
            if (bytes_write < 0)
            {
                ::close(pid_file);
                return State::PARENT_FAIL;
            }

            ssize_t bytes_read = ::read(_pipe_event[PIPE_READ], result, sizeof(result));
            ::close(_pipe_event[PIPE_READ]);

            if (bytes_read > 0 && (::strcmp(result, PRC_SUCCESS) == 0))
            {
                ::close(pid_file);
                return State::PARENT_SUCCESS;
            }

            ::close(pid_file);
            return State::PARENT_FAIL;
        }
        else
        {
            ::close(_pipe_event[PIPE_READ]);
            setsid();
        }

        return State::CHILD_SUCCESS;
    }

    void Daemon::SetEvent(bool success)
    {
        ssize_t bytes_write = ::write(_pipe_event[PIPE_WRITE], (success ? PRC_SUCCESS : PRC_FAIL), 1);
        
        if (bytes_write < 0)
        {
            // write fail
        }
        ::close(_pipe_event[PIPE_WRITE]);
    }
}