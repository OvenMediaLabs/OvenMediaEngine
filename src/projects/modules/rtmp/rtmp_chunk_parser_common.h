//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>
#include <base/ovlibrary/queue.h>

template <typename Theader, typename Tmessage>
class RtmpChunkParserCommon
{
public:
	enum class ParseResult
	{
		Error,
		NeedMoreData,
		Parsed,
	};

public:
	info::NamePath GetNamePath() const
	{
		std::lock_guard lock_guard(_name_path_mutex);
		return _name_path;
	}

	virtual void UpdateNamePath(const info::NamePath &stream_name_path)
	{
		std::lock_guard lock_guard(_name_path_mutex);
		_name_path = stream_name_path;
		_message_queue.SetAlias(ov::String::FormatString("RTMP queue for %s", _name_path.CStr()));
	}

protected:
	bool _need_to_parse_new_header = true;
	std::shared_ptr<Tmessage> _current_message;
	std::map<uint32_t, std::shared_ptr<Tmessage>> _pending_message_map;

#if DEBUG
	uint64_t _chunk_index	   = 0ULL;
	uint64_t _total_read_bytes = 0ULL;
#endif	// DEBUG

	std::map<uint32_t, std::shared_ptr<const Theader>> _preceding_chunk_header_map;
	ov::Queue<std::shared_ptr<const Tmessage>> _message_queue{nullptr, 500};
	size_t _chunk_size = 0;

private:
	mutable std::mutex _name_path_mutex;
	info::NamePath _name_path;
};
