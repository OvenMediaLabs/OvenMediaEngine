//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "path_manager.h"

#include <dirent.h>
#include <errno.h>

#include <regex>
#include <utility>

#include "./assert.h"
#include "./memory_utilities.h"
#if defined(__APPLE__)
#	include <mach-o/dyld.h>
#	include <sys/stat.h>
typedef mode_t __mode_t;
#else
#	include <linux/limits.h>
#endif
#include <unistd.h>
#include <wordexp.h>

#include <filesystem>

namespace ov
{
	String PathManager::GetAppPath(String sub_path)
	{
#if defined(__APPLE__)
		uint32_t path_length = 0;

		if (_NSGetExecutablePath(nullptr, &path_length) == -1 && path_length != 0)
		{
			auto buffer = std::make_unique<char[]>(path_length);
			if (_NSGetExecutablePath(buffer.get(), &path_length) != -1)
			{
				return Combine(Combine(ExtractPath(String(buffer.get(), path_length - 1)), std::move(sub_path)), "");
			}
		}
#elif !defined(__CYGWIN__)
		char buffer[4096] = {0};

		if (readlink("/proc/self/exe", buffer, OV_COUNTOF(buffer)) != -1)
		{
			return Combine(Combine(ExtractPath(buffer), std::move(sub_path)), "");
		}
#endif

		return "";
	}

	String PathManager::GetCurrentPath(String sub_path)
	{
		char buffer[4096] = {0};

		if (getcwd(buffer, OV_COUNTOF(buffer)) == nullptr)
		{
			return "";
		}

		return Combine(Combine(buffer, std::move(sub_path)), "");
	}

	String PathManager::ExpandPath(String path)
	{
		wordexp_t exp_result;

		if (wordexp(path, &exp_result, 0) == 0)
		{
			path = exp_result.we_wordv[0];
		}

		wordfree(&exp_result);

		return path;
	}

	String PathManager::ExtractPath(String path)
	{
		off_t position = path.IndexOfRev('/');

		if (position >= 0)
		{
			return path.Left(static_cast<size_t>(position + 1));
		}

		return "./";
	}

	String PathManager::ExtractFileName(String path)
	{
		off_t position = path.IndexOfRev('/');

		if (position >= 0)
		{
			return path.Substring(position + 1);
		}

		return path;
	}

	bool PathManager::MakeDirectory(const char *path, int mask)
	{
		if ((path == nullptr) || (path[0] == '\0'))
		{
			return false;
		}

		return (mkdir(path, static_cast<mode_t>(mask)) == 0) || (errno == EEXIST);
	}

	bool PathManager::MakeDirectoryRecursive(const String path)
	{
		off_t pos = 0;
		ov::String s = path;
		ov::String dir;

		// Check if the directory already exists
		if (access(path.CStr(), R_OK | W_OK) == 0)
		{
			return true;
		}

		// Append '/' to the end of the path
		if (s.Get(s.GetLength() - 1) != '/')
		{
			s.Append('/');
		}

		// Create directories recursively
		while ((pos = s.IndexOf('/', pos)) != -1)
		{
			dir = s.Substring(0, pos++);
			if (dir.GetLength() == 0)
				continue;

			if (ov::PathManager::MakeDirectory(dir.CStr()) == false)
			{
				return false;
			}
		}

		// Confirm the directory is created
		if (access(path.CStr(), R_OK | W_OK) != 0)
		{
			return false;
		}

		return true;
	}

	String PathManager::Combine(String path1, String path2)
	{
		if ((path1.HasSuffix("/") == false) && (path2.HasPrefix("/") == false))
		{
			path1.Append("/");
		}

		path1.Append(path2);

		return path1;
	}

	bool PathManager::IsFile(String path)
	{
		struct stat file_stat;

		if (::stat(path, &file_stat) == 0)
		{
			return S_ISREG(file_stat.st_mode) ? true : false;
		}

		return false;
	}

	bool PathManager::IsDirectory(String path)
	{
		struct stat file_stat;

		if (::stat(path, &file_stat) == 0)
		{
			return S_ISDIR(file_stat.st_mode) ? true : false;
		}

		return false;
	}

	bool PathManager::IsAbsolute(const char *path)
	{
		return (path != nullptr) && (path[0] == '/');
	}

	String PathManager::GetCanonicalPath(const char *path)
	{
		try
		{
			auto canonical_path = std::filesystem::canonical(path);
			return canonical_path.c_str();
		}
		catch (std::exception &e)
		{
			return path;
		}
	}

	String PathManager::GetNormalizedPath(const char *path)
	{
		std::filesystem::path target = path;

		return target.lexically_normal().c_str();
	}

	String PathManager::ExtractExtension(String path)
	{
		String extension = "";

		off_t idx = path.IndexOfRev('.', path.GetLength() - 1);
		if (idx != -1L)
		{
			extension = path.Substring(idx + 1);
		}
		extension.MakeLower();

		return extension;
	}

	std::shared_ptr<ov::Error> PathManager::GetFileList(const String &base_file_name, const String &pattern, std::vector<String> *file_list, bool exclude_base_path)
	{
		auto base_path = ov::PathManager::ExtractPath(base_file_name);
		String path_pattern;
		bool is_absolute = ov::PathManager::IsAbsolute(pattern);

		path_pattern = (is_absolute) ? pattern : ov::PathManager::Combine(base_path, pattern);

		// Extract path from the pattern
		auto path = ov::PathManager::ExtractPath(path_pattern);
		// Escape special characters: '[', '\', '.', '/', '+', '{', '}', '$', '^', '|' to \<char>
		auto special_characters = std::regex(R"([[\\.\/+{}$^|])");
		String escaped = std::regex_replace(path_pattern.CStr(), special_characters, R"(\$&)").c_str();
		// Change '*'/'?' to .<char>
		escaped = escaped.Replace(R"(*)", R"(.*)");
		escaped = escaped.Replace(R"(?)", R"(.?)");
		escaped.Prepend("^");
		escaped.Append("$");

		std::regex expression;

		try
		{
			expression = std::regex(escaped);
		}
		catch (std::exception &e)
		{
			// Invalid expression
			return ov::Error::CreateError("", "Invalid expression: %s ([%s], from: [%s])", e.what(), escaped.CStr(), path_pattern.CStr());
		}

		auto dir_item = ::opendir(path);

		if (dir_item == nullptr)
		{
			return ov::Error::CreateErrorFromErrno();
		}

		while (true)
		{
			dirent *item = ::readdir(dir_item);

			if (item == nullptr)
			{
				// End of list
				break;
			}

			auto file_name = ov::PathManager::Combine(path, item->d_name);

			switch (item->d_type)
			{
				case DT_REG:
					// Regular file
					break;

				case DT_LNK:
					// Make sure symbolic points to file
					[[fallthrough]];

				default:
					// d_type isn't valid for filesystem like xfs
					struct stat file_stat;

					if (::stat(file_name, &file_stat) == 0)
					{
						if (S_ISREG(file_stat.st_mode))
						{
							break;
						}
						else
						{
							// Target is not a regular file
						}
					}
					else
					{
						// Could not obtain stat of the file
					}

					continue;
			}

			// Test the pattern
			if (std::regex_match(file_name.CStr(), expression))
			{
				// matched
				if (exclude_base_path && (is_absolute == false))
				{
					if (file_name.HasPrefix(base_path))
					{
						file_name = file_name.Substring(base_path.GetLength());
					}
					else
					{
						// file_name MUST always have base_path prefix
						OV_ASSERT2(false);
					}
				}

				file_list->push_back(std::move(file_name));
			}
			else
			{
				// not matched
			}
		}

		::closedir(dir_item);

		return nullptr;
	}

	std::shared_ptr<ov::Error> PathManager::Rename(const String &file_name, const String &to_file_name)
	{
		if (::rename(file_name.CStr(), to_file_name.CStr()) == 0)
		{
			return nullptr;
		}

		return ov::Error::CreateErrorFromErrno();
	}

	std::shared_ptr<ov::Error> PathManager::DeleteFile(const String &file_name)
	{
		if (::unlink(file_name.CStr()) == 0)
		{
			return nullptr;
		}

		return ov::Error::CreateErrorFromErrno();
	}
}  // namespace ov