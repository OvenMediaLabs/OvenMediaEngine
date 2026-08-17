//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./string.h"
#include <iostream>
#include <sys/stat.h>
#include <errno.h>
#include <ftw.h>

namespace ov
{
	bool IsDirExist(const ov::String &path);
	bool CreateDirectories(const ov::String &path);
	static int RemoveFiles(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb);
	bool DeleteDirectories(const ov::String &path);
	std::tuple<bool, std::vector<ov::String>> GetFileList(const ov::String &directory_path);
	ov::String GetBinaryPath();

	// If path is relative path, return absolute path from relative_root (default : binary path)
	// If path is absolute path, return path.
	ov::String GetDirPath(const ov::String &dir_path, const ov::String &relative_root="");
	ov::String GetFilePath(const ov::String &file_path, const ov::String &relative_root="");

	ov::String GetFileName(const ov::String &path);
	ov::String GetFileNameWithoutExt(const ov::String &path);

	bool DeleteFile(const ov::String &path);

	bool CopyFile(const ov::String &src_path, const ov::String &dest_path);
}