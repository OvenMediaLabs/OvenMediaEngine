//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once
#include <base/ovlibrary/ovlibrary.h>
#include "rtsp_header_field.h"

class RtspHeaderTransportField : public RtspHeaderField
{
public:
	RtspHeaderTransportField()
		: RtspHeaderField(RtspHeaderFieldType::Transport)
	{

	}

	RtspHeaderTransportField(uint32_t interleaved_channel_start, uint32_t interleaved_channel_end, uint32_t ssrc)
		: RtspHeaderField(RtspHeaderFieldType::Transport, 
			ov::String::FormatString("RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%X", interleaved_channel_start, interleaved_channel_end, ssrc))
	{
		_interleaved_channel_start = interleaved_channel_start;
		_interleaved_channel_end = interleaved_channel_end;
		_ssrc = ssrc;
	}

	// https://datatracker.ietf.org/doc/html/rfc2326#section-12.39
	// transport/profile/lower-transport;parameters

	bool Parse(const ov::String &message) override
	{
		if(RtspHeaderField::Parse(message) == false)
		{
			return false;
		}

		auto items = GetValue().Split(";");
		
		auto transport = items[0];
		auto transport_items = transport.Split("/");
		switch(transport_items.size())
		{
			case 3:
				_lower_transport = transport_items[2];
				[[fallthrough]];
			case 2:
				_profile = transport_items[1];
				[[fallthrough]];
			case 1:
				_protocol = transport_items[0];
			default:
				// error
				break;
		}

		// Parameters
		for(size_t i=1; i<items.size(); i++)
		{
			auto parameter = items[i];
			auto parameter_items = parameter.Split("=");
			auto name = parameter_items[0].Trim();

			if(name.UpperCaseString() == "UNICAST")
			{
				_is_unicast = true;
			}
			else if(name.UpperCaseString() == "MULTICAST")
			{
				_is_unicast = false;
			}
			else if(name.UpperCaseString() == "INTERLEAVED")
			{
				if(parameter_items.size() == 2)
				{
					auto interleaved = parameter_items[1].Trim();
					auto interleaved_items = interleaved.Split("-");
					if(interleaved_items.size() == 2)
					{
						_interleaved_parsed = true;
						_interleaved_channel_start = ov::Converter::ToUInt32(interleaved_items[0].CStr());
						_interleaved_channel_end = ov::Converter::ToUInt32(interleaved_items[1].CStr());
					}
				}
			}
			else if(name.UpperCaseString() == "SSRC")
			{
				if(parameter_items.size() == 2)
				{
					auto ssrc = parameter_items[1].Trim();
					_ssrc = ov::Converter::ToUInt32(ssrc.CStr(), 16);
				}
			}
		}

		return true;
	}

	ov::String Serialize() const override
	{
		return ov::String::FormatString("Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%X", _interleaved_channel_start, _interleaved_channel_end, _ssrc);
	}

	uint32_t		GetSsrc() {return _ssrc;}
	bool			IsInterleavedParsed(){return _interleaved_parsed;}
	uint32_t		GetInterleavedChannelStart(){return _interleaved_channel_start;}
	uint32_t		GetInterleavedChannelEnd(){return _interleaved_channel_end;}
	bool			IsUnicast(){return _is_unicast;}
	ov::String		GetProtocol(){return _protocol;}
	ov::String		GetProfile(){return _profile;}
	ov::String		GetLowerTransport(){return _lower_transport;}

private:
	// OME only supports RTP/AVP/TCP
	ov::String		_protocol = "RTP";
	ov::String		_profile = "AVP";
	ov::String		_lower_transport = "TCP";

	// Parameters
	bool			_is_unicast = true;
	uint32_t		_ssrc = 0;
	bool 			_interleaved_parsed = false;
	uint32_t		_interleaved_channel_start = 0;
	uint32_t 		_interleaved_channel_end = 0;
	// other parameters not yet used
};