//
// Created by getroot on 20. 11. 25.
//

#pragma once

#include <base/ovlibrary/converter.h>

#include "base/common_types.h"
#include "base/info/info.h"
#include "base/info/stream.h"
#include "common_metrics.h"

class ReservedStreamMetrics
{
public:
	ReservedStreamMetrics(ProviderType who, const ov::Url &stream_uri, const ov::String &stream_name)
	{
		_who_reserved = who;
		_stream_uri	  = stream_uri;
		_stream_name  = stream_name;
	}

	~ReservedStreamMetrics()
	{
	}

	ProviderType WhoReserved()
	{
		return _who_reserved;
	}
	const ov::Url &GetStreamUri() const
	{
		return _stream_uri;
	}
	const ov::String &GetReservedStreamName() const
	{
		return _stream_name;
	}

private:
	ProviderType _who_reserved;
	ov::Url _stream_uri;
	ov::String _stream_name;
};