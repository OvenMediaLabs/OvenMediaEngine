//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

struct ParseOption
{
	// -h
	// Show help messages
	bool help			   = false;

	// -v
	// Show OME version
	bool version		   = false;

	// -c <config_path>
	// Load configurations from the path
	ov::String config_path = "";

	// -e <env_path>
	// Load environment variables from the path
	ov::String env_path	   = "";

	// -d
	// Run as a service
	bool start_service	   = false;

	// -p <pid_path>
	// If -d is set then the path of the PID can be set
	ov::String pid_path	   = "";
};

bool TryParseOption(int argc, char *argv[], ParseOption *parse_option);

MAY_THROWS(std::shared_ptr<ov::Error>)
std::unordered_map<ov::String, ov::String> ParseEnvFile(const ov::String &env_file_path);
