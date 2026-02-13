//==============================================================================
//
//  TranscodeApplication
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "transcoder_application.h"
#include "transcoder_private.h"

#include <unistd.h>
#include <iostream>

#define MIN_APPLICATION_WORKER_COUNT 1
#define MAX_APPLICATION_WORKER_COUNT 72
#define MAX_QUEUE_SIZE 100

std::shared_ptr<TranscodeApplication> TranscodeApplication::Create(const info::Application &application_info)
{
	auto instance = std::make_shared<TranscodeApplication>(application_info);
	if(instance->Start() == false)
	{
		return nullptr;
	}
	
	return instance;
}

TranscodeApplication::TranscodeApplication(const info::Application &application_info)
	: _application_info(application_info)
{
	logti("Created transcoder application. [%s]", application_info.GetVHostAppName().CStr());
}

TranscodeApplication::~TranscodeApplication()
{
	logti("Transcoder application has been destroyed. [%s]", _application_info.GetVHostAppName().CStr());
}

bool TranscodeApplication::Start()
{
	if(ValidateAppConfiguration() == false)
	{
		return false;
	}

	return true;
}

bool TranscodeApplication::Stop()
{
	for (const auto &it : _streams)
	{
		auto stream = it.second;
		stream->Stop();
	}
	_streams.clear();

	logtt("Transcoder application has been stopped. [%s]", _application_info.GetVHostAppName().CStr());

	return true;
}

bool TranscodeApplication::OnStreamCreated(const std::shared_ptr<info::Stream> &stream_info)
{
	std::unique_lock<std::mutex> lock(_mutex);

	auto stream = TranscoderStream::Create(_application_info, stream_info, this);
	if (stream == nullptr)
	{
		return false;
	}

	if (stream->Start() == false)
	{
		return false;
	}

	_streams.insert(std::make_pair(stream_info->GetId(), stream));

	return true;
}

bool TranscodeApplication::OnStreamDeleted(const std::shared_ptr<info::Stream> &stream_info)
{
	std::unique_lock<std::mutex> lock(_mutex);

	auto stream_bucket = _streams.find(stream_info->GetId());

	if (stream_bucket == _streams.end())
	{
		return false;
	}

	auto stream = stream_bucket->second;

	stream->Stop();

	_streams.erase(stream_info->GetId());

	return true;
}

bool TranscodeApplication::OnStreamPrepared(const std::shared_ptr<info::Stream> &stream_info)
{
	std::unique_lock<std::mutex> lock(_mutex);

	auto stream_bucket = _streams.find(stream_info->GetId());
	if (stream_bucket == _streams.end())
	{
		return false;
	}

	auto stream = stream_bucket->second;

	if (stream->Prepare(stream_info) == false)
	{
		return false;
	}

	return true;
}

bool TranscodeApplication::OnStreamUpdated(const std::shared_ptr<info::Stream> &stream_info)
{
	std::unique_lock<std::mutex> lock(_mutex);

	auto stream_bucket = _streams.find(stream_info->GetId());
	if (stream_bucket == _streams.end())
	{
		return false;
	}

	auto stream = stream_bucket->second;
	if (stream->Update(stream_info) == false)
	{
		return false;
	}

	return true;
}

bool TranscodeApplication::OnSendFrame(const std::shared_ptr<info::Stream> &stream_info, const std::shared_ptr<MediaPacket> &packet)
{
	std::unique_lock<std::mutex> lock(_mutex);

	auto stream_bucket = _streams.find(stream_info->GetId());

	if (stream_bucket == _streams.end())
	{
		return false;
	}

	auto stream = stream_bucket->second;

	return stream->Push(packet);
}

bool TranscodeApplication::ValidateAppConfiguration()
{
	auto &cfg_output_profile_list = _application_info.GetConfig().GetOutputProfileList();

	std::vector<ov::String> profile_name_list;
	std::vector<ov::String> output_stream_name_list;

	// Check profile name and output stream name
	for (const auto &cfg_output_profile : cfg_output_profile_list)
	{
		if (cfg_output_profile.GetName().IsEmpty() == true)
		{
			logte("Output profile name is empty. [%s]", _application_info.GetVHostAppName().CStr());
			return false;
		}

		if (cfg_output_profile.GetOutputStreamName().IsEmpty() == true)
		{
			logte("Output stream name is empty. [%s]", _application_info.GetVHostAppName().CStr());
			return false;
		}

		if (std::find(profile_name_list.begin(), profile_name_list.end(), cfg_output_profile.GetName()) != profile_name_list.end())
		{
			logte("Output profile name is duplicated. [%s]. name(%s)", _application_info.GetVHostAppName().CStr(), cfg_output_profile.GetName().CStr());
			return false;
		}

		if (std::find(output_stream_name_list.begin(), output_stream_name_list.end(), cfg_output_profile.GetOutputStreamName()) != output_stream_name_list.end())
		{
			logte("Output stream name is duplicated. [%s]. name(%s)", _application_info.GetVHostAppName().CStr(), cfg_output_profile.GetOutputStreamName().CStr());

			return false;
		}

		profile_name_list.push_back(cfg_output_profile.GetName());
		output_stream_name_list.push_back(cfg_output_profile.GetOutputStreamName());
	}

	return true;
}