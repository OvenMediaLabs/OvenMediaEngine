//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>

namespace ov
{
	class InternalEnableSharedFromThis : public std::enable_shared_from_this<InternalEnableSharedFromThis>
	{
	public:
		virtual ~InternalEnableSharedFromThis() = default;
	};

	template<typename T>
	class EnableSharedFromThis : virtual public InternalEnableSharedFromThis
	{
	public:
		std::shared_ptr<T> GetSharedPtr()
		{
			return std::dynamic_pointer_cast<T>(InternalEnableSharedFromThis::shared_from_this());
		}

		template<typename DownT>
		std::shared_ptr<DownT> GetSharedPtrAs()
		{
			return std::dynamic_pointer_cast<DownT>(InternalEnableSharedFromThis::shared_from_this());
		}

		bool IsSharedPtr()
		{
			try
			{
				GetSharedPtr();

				return true;
			}
			catch(std::bad_weak_ptr &exception)
			{
				return false;
			}
		}

		~EnableSharedFromThis() override = default;
	};
};