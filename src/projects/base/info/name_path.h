#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace info
{
	constexpr const char *UNKNOWN_NAME_PATH = "?unknown?";

	class NamePath
	{
	public:
		NamePath() = default;

		NamePath(const ov::String &name)
		{
			_name_path = name;
			_hash	   = _name_path.Hash();
		}

		static const NamePath &UnknownNamePath()
		{
			static NamePath unknown_name_path(UNKNOWN_NAME_PATH);
			
			return unknown_name_path;
		}

		void Update(const char *format, ...)
		{
			va_list args;
			va_start(args, format);

			_name_path.VFormat(format, args);
			_hash = _name_path.Hash();

			va_end(args);
		}

		NamePath &Append(const char *format, ...)
		{
			va_list args;
			va_start(args, format);

			if (_name_path.IsEmpty() == false)
			{
				_name_path.Append("/");
			}

			_name_path.AppendVFormat(format, args);
			_hash = _name_path.Hash();

			va_end(args);

			return *this;
		}

		NamePath Append(const char *format, ...) const
		{
			auto path = _name_path;

			va_list args;
			va_start(args, format);

			if (path.IsEmpty() == false)
			{
				path.Append("/");
			}

			path.AppendVFormat(format, args);

			va_end(args);

			return NamePath(path);
		}

		const ov::String &ToString() const
		{
			return _name_path;
		}

		std::size_t Hash() const noexcept
		{
			return _hash;
		}

		bool operator==(const NamePath &other) const
		{
			return _name_path == other._name_path;
		}

		bool operator<(const NamePath &other) const
		{
			return _name_path < other._name_path;
		}

		const char *CStr() const
		{
			return _name_path.CStr();
		}

	private:
		ov::String _name_path;
		std::size_t _hash{0};
	};

}  // namespace info

namespace std
{
	template <>
	struct hash<info::NamePath>
	{
		std::size_t operator()(info::NamePath const &name_path) const noexcept
		{
			return name_path.Hash();
		}
	};
}  // namespace std
