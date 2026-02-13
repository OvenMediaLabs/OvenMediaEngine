//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include "codec/codec_base.h"
#include "transcoder_private.h"

class TranscodeFaultInjector : public ov::Singleton<TranscodeFaultInjector>
{
public:
	enum IssueType
	{
		None,
		InitFailed,
		ProcessFailed,
		Lagging
	};

	enum ComponentType
	{
		UnknownComponent,
		DecoderComponent,
		EncoderComponent,
		FilterComponent
	};

	static IssueType GetIssueTypeFromString(const ov::String &issue_str)
	{
		if (issue_str.UpperCaseString() == "INITFAILED")
		{
			return IssueType::InitFailed;
		}
		else if (issue_str.UpperCaseString() == "PROCESSFAILED")
		{
			return IssueType::ProcessFailed;
		}
		else if (issue_str.UpperCaseString() == "LAGGING")
		{
			return IssueType::Lagging;
		}

		return IssueType::None;
	}

	static const char* GetIssueTypeString(IssueType issue_type)
	{
		switch (issue_type)
		{
		case IssueType::InitFailed:
			return "InitFailed";
		case IssueType::ProcessFailed:
			return "ProcessFailed";
		case IssueType::Lagging:
			return "Lagging";
		default:
			return "None";
		}
	}

	static ComponentType GetComponentTypeFromString(const ov::String &component_str)
	{
		if (component_str.UpperCaseString() == "DECODER")
		{
			return ComponentType::DecoderComponent;
		}
		else if (component_str.UpperCaseString() == "ENCODER")
		{
			return ComponentType::EncoderComponent;
		}
		else if (component_str.UpperCaseString() == "FILTER")
		{
			return ComponentType::FilterComponent;
		}

		return ComponentType::UnknownComponent;
	}

	static const char* GetComponentTypeString(ComponentType component_type)
	{
		switch (component_type)
		{
		case ComponentType::DecoderComponent:
			return "Decoder";
		case ComponentType::EncoderComponent:
			return "Encoder";
		case ComponentType::FilterComponent:
			return "Filter";
		default:
			return "Unknown";
		}
	}

public:
	TranscodeFaultInjector();
	~TranscodeFaultInjector() = default;

public:
	bool IsEnabled()
	{
		return _enabled;
	}

	bool IsTriggered(ComponentType component_type, IssueType issue_type, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id)
	{
		if (!_enabled)
		{
			return false;
		}

		std::shared_lock lock(_mutex);

		auto component_it = _fault_triggers.find(std::make_pair(component_type, issue_type));
		if (component_it == _fault_triggers.end())
		{
			return false;
		}

		auto &module_map = component_it->second;
		auto module_it	 = module_map.find(std::make_pair(module_id, device_id));
		if (module_it == module_map.end())
		{
			return false;
		}

		double rate = module_it->second;

		// rate 값이 0.01% ~ 100% 사이여야 함
		if (rate < 0.01 || rate > 100.0)
		{
			return false;
		}

		static thread_local std::mt19937 rng{std::random_device{}()};
		static thread_local std::uniform_real_distribution<double> dist(0.0, 100.0);
		double random_value = dist(rng);
		if (random_value < rate)
		{
			logtw("Fault is triggered. ComponentType(%s), IssueType(%s), ModuleId(%s), DeviceId(%d), Rate(%.2f%%), RandomValue(%.2f%%)",
				  GetComponentTypeString(component_type), GetIssueTypeString(issue_type), cmn::GetCodecModuleIdString(module_id), device_id, rate, random_value);

			return true;
		}

		return false;
	}

	void SetFaultRate(ComponentType component_type, IssueType issue_type, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id, double fault_rate)
	{
		std::unique_lock lock(_mutex);

		logti("Setting fault injection. ComponentType(%s), IssueType(%s), ModuleId(%s), DeviceId(%d), Rate(%.2f%%)",
			  GetComponentTypeString(component_type), GetIssueTypeString(issue_type), cmn::GetCodecModuleIdString(module_id), device_id, fault_rate);

		_fault_triggers[std::make_pair(component_type, issue_type)][std::make_pair(module_id, device_id)] = fault_rate;
		_enabled = true;
	}

	void Clear()
	{
		std::unique_lock lock(_mutex);
		_fault_triggers.clear();
		_enabled = false;
	}

private:
	bool _enabled = false;

	std::shared_mutex _mutex;
	std::map<std::pair<ComponentType, IssueType>, std::map<std::pair<cmn::MediaCodecModuleId, cmn::DeviceId>, double>> _fault_triggers;
};