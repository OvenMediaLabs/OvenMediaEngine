//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "ice_candidate.h"

#include <base/ovlibrary/converter.h>

#include <algorithm>

#include "ice_private.h"

#define ICE_CANDIDATE_PREFIX "candidate:"

IceCandidate::IceCandidate()
	: IceCandidate("UDP", ov::SocketAddress())
{
	_foundation = ov::Random::GenerateNumberString(10);
}

IceCandidate::IceCandidate(const ov::String &transport, const ov::SocketAddress &address)
	: _component_id(1),
	  _transport(std::move(transport)),
	  _priority(50),
	  _address(address),
	  // candidate type은 host만 지원
	  _candidate_types("host"),
	  _rel_port(0)
{
	_foundation = ov::Random::GenerateNumberString(10);
}

// without resolving address
IceCandidate::IceCandidate(const ov::String &transport, const ov::String &address, int port)
	: _component_id(1),
	  _transport(std::move(transport)),
	  _priority(50),
	  _address_str(std::move(address)),
	  _port(port),
	  _candidate_types("host"),
	  _rel_port(0)
{
	_foundation = ov::Random::GenerateNumberString(10);
}

IceCandidate::IceCandidate(IceCandidate &&candidate) noexcept
	: IceCandidate()
{
	Swap(candidate);
}

IceCandidate::~IceCandidate()
{
}

void IceCandidate::Swap(IceCandidate &from) noexcept
{
	std::swap(_foundation, from._foundation);
	std::swap(_component_id, from._component_id);
	std::swap(_transport, from._transport);
	std::swap(_priority, from._priority);
	std::swap(_address, from._address);
	std::swap(_address_str, from._address_str);
	std::swap(_port, from._port);
	std::swap(_candidate_types, from._candidate_types);
	std::swap(_rel_addr, from._rel_addr);
	std::swap(_rel_port, from._rel_port);
	std::swap(_extension_attributes, from._extension_attributes);
}

bool IceCandidate::ParseFromString(const ov::String &candidate_string)
{
	IceCandidate temp_candidate;

	// RFC5245 - 15.1.  "candidate" Attribute
	// candidate-attribute   = "candidate" ":" foundation SP component-id SP
	//                         transport SP
	//                         priority SP
	//                         connection-address SP     ;from RFC 4566
	//                         port         ;port from RFC 4566
	//                         SP cand-type
	//                         [SP rel-addr]
	//                         [SP rel-port]
	//                         *(SP extension-att-name SP
	//                              extension-att-value)
	//

	// 0                    1   2               3               4       5   6       7           8   9       10      11              12
	// candidate:501616445  1   udp 2113937151  192.168.0.152   52739   typ host    generation  0   ufrag   Qy/4    network-cost    50
	// candidate:0          1   UDP 50          192.168.0.183   10000   typ host    generation  0

	if (candidate_string.HasPrefix("candidate:") == false)
	{
		// 반드시 candidate로 시작해야 함
		logtw("Candidate string does not starts with 'candidate:' string: %s", candidate_string.CStr());
		return false;
	}

	// 공백을 기준으로 나눔
	auto tokens = candidate_string.Split(" ");

	if (tokens.size() < 7)
	{
		// 다음을 위해, 최소 7개의 토큰이 있어야 함:
		// foundation, component-id, transport, priority, connection-address, port, cand-type

		// 잘못된 candidate
		logtw("Candidate string must have at least 7 tokens: %s", candidate_string.CStr());
		return false;
	}

	auto iterator = tokens.begin();

	temp_candidate._foundation = *iterator++;
	if (temp_candidate._foundation.HasPrefix(ICE_CANDIDATE_PREFIX) == false)
	{
		// 잘못된 foundation
		logtw("Invalid foundation: %s", temp_candidate._foundation.CStr());
		return false;
	}

	// 앞에 candidate:는 제외
	temp_candidate._foundation = temp_candidate._foundation.Substring(OV_COUNTOF(ICE_CANDIDATE_PREFIX));

	temp_candidate._component_id = ov::Converter::ToUInt32(*iterator++);
	temp_candidate._transport = *iterator++;
	temp_candidate._priority = ov::Converter::ToUInt32(*iterator++);

	{
		auto connection_address = *iterator++;
		auto port = ov::Converter::ToUInt16(*iterator++);

		temp_candidate._address_str = connection_address;
		temp_candidate._port = port;

		// Check if this is a necessary procedure
		// try
		// {
		// 	auto address_list = ov::SocketAddress::Create(connection_address, port);

		// 	if (address_list.empty())
		// 	{
		// 		logtw("Could not resolve an address from %s:%d", connection_address.CStr(), port);
		// 		return false;
		// 	}

		// 	if (address_list.size() >= 2)
		// 	{
		// 		logtw("Multiple addresses are found from %s:%d. OME will use the first address", connection_address.CStr(), port);
		// 	}

		// 	temp_candidate._address = address_list[0];
		// }
		// catch (const ov::Error &e)
		// {
		// 	logtw("Invalid address: %s, port: %d - %s", connection_address.CStr(), port, e.What());
		// 	return false;
		// }
	}

	ov::String cand_type = *iterator++;

	if (cand_type != "typ")
	{
		// 잘못된 cand_type
		return false;
	}
	temp_candidate._candidate_types = *iterator++;

	if ((iterator != tokens.end()) && ((*iterator) == "raddr"))
	{
		*iterator++;

		if (iterator != tokens.end())
		{
			// "raddr" <connection-address>
			temp_candidate._rel_addr = *iterator++;
		}
		else
		{
			logtw("Invalid rel-addr");
			return false;
		}
	}

	if ((iterator != tokens.end()) && ((*iterator) == "rport"))
	{
		*iterator++;

		if (iterator != tokens.end())
		{
			// "rport" <port>
			temp_candidate._rel_port = ov::Converter::ToUInt16(*iterator++);
		}
		else
		{
			logtw("Invalid rel-port");
			return false;
		}
	}

	// extension attributes
	while (iterator != tokens.end())
	{
		ov::String &key = *iterator++;

		if (iterator == tokens.end())
		{
			logtw("Invalid extension value for key: '%s'", key.CStr());
			return false;
		}

		ov::String &value = *iterator++;

		temp_candidate._extension_attributes[key] = value;
	}

	Swap(temp_candidate);

	return true;
}

IceCandidate &IceCandidate::operator=(IceCandidate candidate) noexcept
{
	Swap(candidate);

	return *this;
}

bool IceCandidate::operator<(const IceCandidate &candidate) const noexcept
{
	if (_address.IsValid())
	{
		return _address < candidate._address;
	}

	if (_address_str == candidate._address_str)
	{
		return _port < candidate._port;
	}

	return _address_str < candidate._address_str;
}

const ov::String &IceCandidate::GetFoundation() const noexcept
{
	return _foundation;
}

void IceCandidate::SetFoundation(ov::String foundation)
{
	_foundation = foundation;
}

uint32_t IceCandidate::GetComponentId() const
{
	return _component_id;
}

void IceCandidate::SetComponentId(uint32_t component_id)
{
	_component_id = component_id;
}

const ov::String &IceCandidate::GetTransport() const
{
	return _transport;
}

void IceCandidate::SetTransport(const ov::String &transport)
{
	_transport = transport;
}

uint32_t IceCandidate::GetPriority() const
{
	return _priority;
}

void IceCandidate::SetPriority(uint32_t priority)
{
	_priority = priority;
}

ov::SocketAddress IceCandidate::GetAddress() const
{
	return _address;
}

ov::String IceCandidate::GetConnectionAddress() const
{
	if (_address.IsValid())
	{
		return _address.GetIpAddress();
	}
	
	return _address_str;
}

int IceCandidate::GetPort() const
{
	if (_address.IsValid())
	{
		return _address.Port();
	}

	return _port;
}

const ov::String &IceCandidate::GetCandidateTypes() const
{
	return _candidate_types;
}

void IceCandidate::SetCandidateTypes(const ov::String &candidate_types)
{
	_candidate_types = candidate_types;
}

const ov::String &IceCandidate::GetRelAddr() const
{
	return _rel_addr;
}

void IceCandidate::SetRelAddr(const ov::String &rel_addr)
{
	_rel_addr = rel_addr;
}

uint16_t IceCandidate::GetRelPort() const
{
	return _rel_port;
}

void IceCandidate::SetRelPort(uint16_t rel_port)
{
	_rel_port = rel_port;
}

const std::map<ov::String, ov::String> &IceCandidate::GetExtensionAttributes() const
{
	return _extension_attributes;
}

void IceCandidate::AddExtensionAttributes(const ov::String &key, const ov::String &value)
{
	_extension_attributes[key] = value;
}

bool IceCandidate::RemoveExtensionAttributes(const ov::String &key)
{
	auto item = _extension_attributes.find(key);

	if (item == _extension_attributes.end())
	{
		return false;
	}

	_extension_attributes.erase(item);

	return true;
}

void IceCandidate::RemoveAllExtensionAttributes()
{
	_extension_attributes.clear();
}

ov::String IceCandidate::GetCandidateString() const noexcept
{
	// candidate:0 1 UDP 50 192.168.0.183 10000 typ host generation 0
	ov::String result;

	result.Format(
		// "candidate" ":" foundation SP component-id SP
		"candidate:%s %d "
		// transport SP
		"%s "
		// priority SP
		"%d "
		// connection-address SP     ;from RFC 4566
		"%s "
		// port         ;port from RFC 4566
		"%d"
		// SP cand-type
		" typ %s",
		_foundation.CStr(), _component_id,
		_transport.UpperCaseString().CStr(),
		_priority,
		GetConnectionAddress().CStr(),
		GetPort(),
		_candidate_types.CStr());

	if (_rel_addr.IsEmpty() == false)
	{
		// [SP rel-addr]
		// rel-addr              = "raddr" SP connection-address
		result.AppendFormat(" raddr %s", _rel_addr.CStr());
	}

	if (_rel_port > 0)
	{
		// [SP rel-port]
		// rel-port              = "rport" SP port
		result.AppendFormat(" rport %d", _rel_port);
	}

	for (auto const &value : _extension_attributes)
	{
		// *(SP extension-att-name SP
		//   extension-att-value)
		result.AppendFormat(" %s %s", value.first.CStr(), value.second.CStr());
	}

	return result;
}

ov::String IceCandidate::ToString() const noexcept
{
	return GetCandidateString();
}
