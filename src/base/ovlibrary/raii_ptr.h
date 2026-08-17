//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

// The simple version of std::unique_ptr
// This can be used for OpenSSL's STACK_OF() and other C-style APIs
namespace ov
{
	template <typename T>
	class RaiiPtr
	{
	public:
		RaiiPtr(T *instance, std::function<void(T *instance)> deleter)
			: _instance(instance),
			  _deleter(deleter)
		{
		}

		RaiiPtr(const RaiiPtr &other) = delete;
		RaiiPtr &operator=(const RaiiPtr &other) = delete;

		RaiiPtr(RaiiPtr &&other)
			: _instance(nullptr),
			  _deleter(nullptr)
		{
			std::swap(_instance, other._instance);
			std::swap(_deleter, other._deleter);
		}

		~RaiiPtr()
		{
			Set(nullptr);
		}

		RaiiPtr &operator=(RaiiPtr &&other)
		{
			if (this != &other)
			{
				std::swap(_instance, other._instance);
				std::swap(_deleter, other._deleter);
			}

			return *this;
		}

		RaiiPtr &operator=(T *instance)
		{
			Set(instance);

			return *this;
		}

		void Set(T *instance)
		{
			if (_instance != nullptr)
			{
				if (_deleter != nullptr)
				{
					_deleter(_instance);
				}
			}

			_instance = instance;
		}

		T *Get() const
		{
			return _instance;
		}

		T *Get()
		{
			return _instance;
		}

		template <typename Tcast>
		Tcast *GetAs()
		{
			return reinterpret_cast<Tcast *>(_instance);
		}

		T **GetPointer()
		{
			return &_instance;
		}

		T *Detach()
		{
			T *instance = _instance;

			_instance = nullptr;

			return instance;
		}

		operator T *() const
		{
			return Get();
		}

		operator T *()
		{
			return Get();
		}

	private:
		T *_instance;
		std::function<void(T *instance)> _deleter;
	};
}  // namespace ov