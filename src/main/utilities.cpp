//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "utilities.h"

#include <getopt.h>

bool TryParseOption(int argc, char *argv[], ParseOption *parse_option)
{
	constexpr const char *opt_string = "hvc:e:dp:";

	while (true)
	{
		int name = ::getopt(argc, argv, opt_string);

		switch (name)
		{
			case -1:
				// end of arguments
				return true;

			case 'h':
				parse_option->help = true;
				return true;

			case 'v':
				parse_option->version = true;
				return true;

			case 'c':
				parse_option->config_path = optarg;
				break;

			case 'e':
				parse_option->env_path = optarg;
				break;

			case 'd':
				// Don't use this option manually
				parse_option->start_service = true;
				break;

			case 'p':
				parse_option->pid_path = optarg;
				break;

			default:  // '?'
				// invalid argument
				return false;
		}
	}
}

MAY_THROWS(std::shared_ptr<ov::Error>)
static ov::String LoadFile(const ov::String &file_path)
{
	auto file = ov::RaiiPtr<FILE>(::fopen(file_path, "rb"), [](FILE *f) {
		if (f != nullptr)
		{
			::fclose(f);
		}
	});

	if (file == nullptr)
	{
		auto error = ov::Error::CreateErrorFromErrno();
		throw ov::Error::CreateError("OvenMediaEngine", "Could not open file: %s (%s)", file_path.CStr(), error->What());
	}

	if (::fseek(file, 0, SEEK_END) != 0)
	{
		auto error = ov::Error::CreateErrorFromErrno();
		throw ov::Error::CreateError("OvenMediaEngine", "Could not seek to end of file: %s (%s)", file_path.CStr(), error->What());
	}

	auto file_size = ::ftell(file);

	if (file_size == -1)
	{
		auto error = ov::Error::CreateErrorFromErrno();
		throw ov::Error::CreateError("OvenMediaEngine", "Could not get file size: %s (%s)", file_path.CStr(), error->What());
	}

	if (::fseek(file, 0, SEEK_SET) != 0)
	{
		auto error = ov::Error::CreateErrorFromErrno();
		throw ov::Error::CreateError("OvenMediaEngine", "Could not seek to start of file: %s (%s)", file_path.CStr(), error->What());
	}

	if (file_size > (1024 * 1024))
	{
		throw ov::Error::CreateError("OvenMediaEngine",
									 "The size of the environment file is too large (%ld bytes). "
									 "Please check the file: %s",
									 file_size, file_path.CStr());
	}

	ov::String env_content;
	env_content.SetLength(file_size);
	if (::fread(env_content.GetBuffer(), 1, file_size, file) != static_cast<size_t>(file_size))
	{
		auto error = ov::Error::CreateErrorFromErrno();
		throw ov::Error::CreateError("OvenMediaEngine", "Could not read file: %s (%s)", file_path.CStr(), error->What());
	}

	return env_content;
}

std::unordered_map<ov::String, ov::String> ParseEnvFile(const ov::String &env_file_path)
{
	auto content = LoadFile(env_file_path);

	std::unordered_map<ov::String, ov::String> env_vars;
	auto lines	  = content.Split("\n");

	auto line_num = 0;
	for (auto line : lines)
	{
		line_num++;
		line = line.Trim();

		// Process comment part

		// VAR='This is # not comment' # this is comment
		//   => VAR="This is # not comment"
		// VAR='This is # this is comment"
		//   => VAR="'This is"
		// VAR="This is new\nline"
		//   => VAR="This is new<NEW_LINE>line"
		{
			bool in_single_quote = false;
			bool in_double_quote = false;
			bool escape			 = false;

			auto length			 = line.GetLength();

			for (size_t i = 0; i < length; ++i)
			{
				const char c = line[i];

				if (escape)
				{
					escape = false;
					continue;
				}

				if (c == '\\')
				{
					escape = true;
				}
				else if (c == '\'')
				{
					if (in_double_quote == false)
					{
						in_single_quote = !in_single_quote;
					}
				}
				else if (c == '"')
				{
					if (in_single_quote == false)
					{
						in_double_quote = !in_double_quote;
					}
				}
				else if (c == '#')
				{
					if ((in_single_quote == false) && (in_double_quote == false))
					{
						// Remove the comment part
						line = line.Substring(0, i).Trim();
					}
					break;
				}
			}

			if (in_single_quote || in_double_quote)
			{
				throw ov::Error::CreateError("OvenMediaEngine", "Unmatched quotes in environment file: %s:%d", env_file_path.CStr(), line_num);
			}
		}

		if (line.IsEmpty())
		{
			continue;
		}

		auto tokens = line.Split("=", 2);

		if (tokens.size() < 2)
		{
			throw ov::Error::CreateError("OvenMediaEngine", "Invalid line in environment file: %s:%d", line.CStr(), line_num);
		}

		env_vars[tokens[0].Trim()] = tokens[1]
										 .Trim()
										 .Replace("\\#", "#")
										 .Replace("\\$", "$")
										 .Replace("\\\\", "\\")
										 .Replace("\\n", "\n");
	}

	return env_vars;
}
