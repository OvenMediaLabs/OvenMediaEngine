//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./socket_address.h"

namespace ov
{
	class SocketAddressPair
	{
	public:
		SocketAddressPair()
		{
		}

		SocketAddressPair(
			const SocketAddress &local_address,
			const SocketAddress &remote_address)
			: _local_address(local_address),
			  _remote_address(remote_address)
		{
		}

		const SocketAddress &GetLocalAddress() const
		{
			return _local_address;
		}

		const SocketAddress &GetRemoteAddress() const
		{
			return _remote_address;
		}

		void SetLocalAddress(const SocketAddress &address)
		{
			_local_address = address;
		}

		void SetRemoteAddress(const SocketAddress &address)
		{
			_remote_address = address;
		}

		bool operator==(const SocketAddressPair &pair) const
		{
			return (_remote_address == pair._remote_address) && (_local_address == pair._local_address);
		}

		bool operator!=(const SocketAddressPair &pair) const
		{
			return (operator==(pair)) == false;
		}

		bool operator<(const SocketAddressPair &pair) const
		{
			if (_remote_address < pair._remote_address)
			{
				return true;
			}
			else if (_remote_address == pair._remote_address)
			{
				return _local_address < pair._local_address;
			}

			return false;
		}

		bool operator>(const SocketAddressPair &pair) const
		{
			if (_remote_address > pair._remote_address)
			{
				return true;
			}
			else if (_remote_address == pair._remote_address)
			{
				return _local_address > pair._local_address;
			}

			return false;
		}

		String ToString() const
		{
			return String::FormatString(
				"<SocketAddressPair: %p, local: %s, remote: %s>",
				this,
				_local_address.ToString(false).CStr(),
				_remote_address.ToString(false).CStr());
		}

	private:
		SocketAddress _local_address;
		SocketAddress _remote_address;
	};
}  // namespace ov
