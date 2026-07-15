//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./regex.h"

#define OV_LOG_TAG "Regex"

#include "./assert.h"
#include "./memory_utilities.h"

// This is to set up PCRE2 to treat strings as UTF8
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define GET_CODE() static_cast<pcre2_code *>(_code)

static inline PCRE2_SPTR ToPcreStr(const char *c_string)
{
	return reinterpret_cast<PCRE2_SPTR>(c_string);
}

static inline const char *FromPcreStr(PCRE2_SPTR pcre_string)
{
	return reinterpret_cast<const char *>(pcre_string);
}

static inline PCRE2_SIZE ToPcreSize(const ov::String &string)
{
	return static_cast<PCRE2_SIZE>(string.GetLength());
}

static inline PCRE2_SIZE ToPcreSize(size_t size)
{
	return static_cast<PCRE2_SIZE>(size);
}

static inline size_t FromPcreSize(PCRE2_SIZE pcre_size)
{
	return static_cast<size_t>(pcre_size);
}

static inline ov::String GetPcreErrorMessage(int error_code)
{
	PCRE2_UCHAR buffer[512];
	::pcre2_get_error_message(error_code, buffer, OV_COUNTOF(buffer));

	return FromPcreStr(buffer);
}

namespace ov
{
	MatchGroup::MatchGroup(const char *base_address, size_t start_offset, size_t end_offset)
	{
		if (start_offset <= end_offset)
		{
			_base_address = base_address;
			_start_offset = start_offset;
			_end_offset = end_offset;

			_length = end_offset - start_offset;
		}
		else
		{
			// Note in pcre2demo:
			//    We must guard against patterns such as /(?=.\K)/ that use \K in an assertion
			//    to set the start of a match later than its end. In this demonstration program,
			//    we just detect this case and give up.
		}
	}

	MatchResult::MatchResult()
		: MatchResult(Error::CreateError("Regex", "Not initialized"))
	{
	}

	static std::vector<MatchGroup> CreateMatchGroupList(const char *base_address, const pcre2_match_data *match_data, int group_count, const size_t *output_vectors)
	{
		std::vector<MatchGroup> result;

		for (int i = 0; i < group_count; i++)
		{
			auto start_offset = output_vectors[2 * i];
			auto end_offset = output_vectors[2 * i + 1];

			result.emplace_back(base_address, start_offset, end_offset);
		}

		return result;
	}

	MatchResult::MatchResult(const std::shared_ptr<ov::Error> &error)
		: _error(error)
	{
	}

	// MatchResult with results
	MatchResult::MatchResult(std::shared_ptr<ov::String> subject,
							 std::vector<MatchGroup> group_list,
							 std::unordered_map<ov::String, MatchGroup> named_group)
		: _subject(std::move(subject)),
		  _group_list(std::move(group_list)),
		  _named_group(std::move(named_group))

	{
	}

	std::shared_ptr<const ov::Error> MatchResult::GetError() const
	{
		return _error;
	}

	const ov::String &MatchResult::GetSubject() const
	{
		return *_subject;
	}

	size_t MatchResult::GetGroupCount() const
	{
		return _group_list.size();
	}

	MatchGroup MatchResult::GetGroupAt(size_t index) const
	{
		if (index >= _group_list.size())
		{
			return {};
		}

		return _group_list[index];
	}

	const std::vector<MatchGroup> &MatchResult::GetGroupList() const
	{
		return _group_list;
	}

	size_t MatchResult::GetNamedGroupCount() const
	{
		return _named_group.size();
	}

	const std::unordered_map<ov::String, MatchGroup> &MatchResult::GetNamedGroupList() const
	{
		return _named_group;
	}

	MatchGroup MatchResult::GetNamedGroup(const char *name) const
	{
		auto item = _named_group.find(name);

		if (item == _named_group.end())
		{
			return {};
		}

		return item->second;
	}

	Regex::Regex()
		: Regex("")
	{
	}

	Regex::Regex(const char *pattern, Option options)
		: _pattern(pattern),
		  _options(options)
	{
	}

	Regex::Regex(const char *pattern)
		: Regex(pattern, Option::None)
	{
	}

	Regex::Regex(const Regex &regex)
	{
		operator=(regex);
	}

	Regex::Regex(Regex &&regex)
	{
		std::swap(_pattern, regex._pattern);
		std::swap(_options, regex._options);
		std::swap(_code, regex._code);
	}

	Regex::~Regex()
	{
		Release();
	}

	Regex Regex::CompiledRegex(const char *pattern, Option options)
	{
		Regex regex(pattern, options);
		auto error = regex.Compile();

		if (error != nullptr)
		{
			logte("Could not compile regex: %s, options: %d, reason: %s", pattern, static_cast<int>(options), error->What());
		}

		return regex;
	}

	Regex Regex::CompiledRegex(const char *pattern)
	{
		Regex regex(pattern);
		auto error = regex.Compile();

		if (error != nullptr)
		{
			logte("Could not compile regex: %s, reason: %s", pattern, error->What());
		}

		return regex;
	}

	ov::String Regex::WildCardRegex(const ov::String &wildcard_string, bool exact_match)
	{
		// Escape special characters: '[', '\', '.', '/', '+', '{', '}', '$', '^', '|' to \<char>
		auto regex = ov::Regex::CompiledRegex(R"(([[\\.\/+{}$^|]))")
						 // Change <special char> to '.<special char>'
						 .Replace(wildcard_string, R"(\\$1)", true)
						 // Change '*'/'?' to .<char>
						 .Replace(R"(*)", R"(.*)")
						 .Replace(R"(?)", R"(.?)");

		if (exact_match)
		{
			regex.Prepend("^");
			regex.Append("$");
		}

		return regex;
	}

	const ov::String &Regex::GetPattern() const
	{
		return _pattern;
	}

	std::shared_ptr<Error> Regex::Compile()
	{
		if (_code != nullptr)
		{
			OV_ASSERT2("Compile() is called twice");
			return Error::CreateError("Regex", "Regex is already compiled");
		}

		if (_pattern.IsEmpty())
		{
			return Error::CreateError("Regex", "Pattern is empty");
		}

		int error_code;
		PCRE2_SIZE error_offset;

		uint32_t options = PCRE2_UTF;

		if (_options == Option::CaseInsensitive)
		{
			options |= PCRE2_CASELESS;
		}
		if (_options == Option::Multiline)
		{
			options |= PCRE2_MULTILINE;
		}
		if (_options == Option::DotAll)
		{
			options |= PCRE2_DOTALL;
		}
		if (_options == Option::Literal)
		{
			options |= PCRE2_LITERAL;
		}

		// https://www.pcre.org/current/doc/html/pcre2_compile.html
		_code = ::pcre2_compile(
			ToPcreStr(_pattern.CStr()), ToPcreSize(_pattern),
			options, &error_code, &error_offset,
			nullptr);

		if (_code == nullptr)
		{
			return Error::CreateError("Regex", "Compilation failed at offset %zu: %s", FromPcreSize(error_offset), GetPcreErrorMessage(error_code).CStr());
		}

		// https://www.pcre.org/current/doc/html/pcre2_jit_compile.html
		// If the regex pattern is large/complicated, then we need to control the jit stack (https://www.pcre.org/current/doc/html/pcre2jit.html#stackcontrol)

		// IMPORTANT!: I disabled pcre2_jit_compile() code, because I need to study more about PCRE2 JIT
		//
		// http://pcre.org/current/doc/html/pcre2api.html - The compiled pattern
		//
		//     ...
		//     However, if the just-in-time (JIT) optimization feature is being used,
		//     it needs separate memory stack areas for each thread. See the pcre2jit documentation for more details.
		//     ...

		// TODO(dimiden): A lock is needed for the _code
		// ::pcre2_jit_compile(GET_CODE(), PCRE2_JIT_COMPLETE);

		return nullptr;
	}

	MatchResult Regex::Matches(const char *subject) const
	{
		if (_code == nullptr)
		{
			return {ov::Error::CreateError("Regex", "Not compiled")};
		}

		std::shared_ptr<Error> error;

		// Allocate group information
		auto match_data = ::pcre2_match_data_create_from_pattern(GET_CODE(), nullptr);

		if (match_data != nullptr)
		{
			// https://www.pcre.org/current/doc/html/pcre2_match.html
			auto group_count = ::pcre2_match(GET_CODE(),
											 ToPcreStr(subject), PCRE2_ZERO_TERMINATED,
											 // start offset, options, ...
											 0, 0, match_data, nullptr);

			if (group_count > 0)
			{
				// group_count == 1: There is no numbered captures (output_vector[0]/[1] contain the start/end offset of matched text)

				auto allocated_subject = std::make_shared<ov::String>(subject);
				auto base_address = allocated_subject->CStr();

				auto output_vectors = ::pcre2_get_ovector_pointer(match_data);

				auto match_group_list = CreateMatchGroupList(base_address, match_data, group_count, output_vectors);
				auto named_group_map = CreateNamedGroupMap(base_address, output_vectors);

				::pcre2_match_data_free(match_data);

				return {allocated_subject, match_group_list, named_group_map};
			}
			else if (group_count == 0)
			{
				// (group_count == 0) means vector of offsets is too small
			}
			else
			{
				// An error occurred
				switch (group_count)
				{
					case PCRE2_ERROR_NOMATCH:
						error = Error::CreateError("Regex", "No match");
						break;

					default:
						error = Error::CreateError("Regex", "Unknown error: %d", group_count);
						break;
				}
			}

			::pcre2_match_data_free(match_data);
		}
		else
		{
			error = Error::CreateError("Regex", "Failed to create match data");
		}

		return {error};
	}

	ov::String Regex::Replace(const ov::String &subject, const ov::String &replace_with, bool replace_all, std::shared_ptr<const ov::Error> *error) const
	{
		if (IsCompiled() == false)
		{
			if (error != nullptr)
			{
				*error = Error::CreateError("Regex", "Regex is not compiled");
			}
			return {};
		}

		auto code = GET_CODE();
		auto match_data = ::pcre2_match_data_create_from_pattern(code, nullptr);

		auto subject_str = ToPcreStr(subject);
		auto subject_length = ToPcreSize(subject);
		auto replace_str = ToPcreStr(replace_with);
		auto replace_length = ToPcreSize(replace_with);

		// https://www.pcre.org/current/doc/html/pcre2_substitute.html
		auto options =
			// If overflow, compute needed length
			PCRE2_SUBSTITUTE_OVERFLOW_LENGTH |
			// Replace all occurrences in the subject
			(replace_all ? PCRE2_SUBSTITUTE_GLOBAL : 0) |
			// Do extended replacement processing
			PCRE2_SUBSTITUTE_EXTENDED;

		PCRE2_SIZE buffer_length = subject_length;
		PCRE2_SIZE string_length = 0;
		PCRE2_UCHAR *buffer = static_cast<PCRE2_UCHAR *>(::malloc(sizeof(PCRE2_UCHAR) * buffer_length));

		while (true)
		{
			auto output_length = buffer_length;
			auto result = ::pcre2_substitute(
				code,
				subject_str, subject_length,
				// startoffset
				0,
				options,
				// match_data, mcontext
				nullptr, nullptr,
				replace_str, replace_length,
				buffer, &output_length);

			string_length = output_length;

			if (result >= 0)
			{
				break;
			}
			else
			{
				if (result == PCRE2_ERROR_NOMEMORY)
				{
					buffer_length = output_length;
					buffer = static_cast<PCRE2_UCHAR *>(::realloc(buffer, sizeof(PCRE2_UCHAR) * buffer_length));
					continue;
				}

				// An error occurred
				if (error != nullptr)
				{
					*error = Error::CreateError("Regex", "Regex is not compiled");
				}

				break;
			}
		}

		::pcre2_match_data_free(match_data);

		ov::String result(FromPcreStr(buffer), string_length);

		OV_SAFE_FREE(buffer);

		return result;
	}

	Regex &Regex::operator=(const Regex &regex)
	{
		_pattern = regex._pattern;
		_options = regex._options;
		_code = ::pcre2_code_copy_with_tables(static_cast<pcre2_code_8 *>(regex._code));

		return *this;
	}

	void Regex::Release()
	{
		_pattern = {};

		if (_code != nullptr)
		{
			::pcre2_code_free(GET_CODE());
			_code = nullptr;
		}
	}

	std::unordered_map<ov::String, MatchGroup> Regex::CreateNamedGroupMap(const char *base_address, const size_t *output_vectors) const
	{
		std::unordered_map<ov::String, MatchGroup> result;

		// Named groups
		uint32_t name_count;
		::pcre2_pattern_info(GET_CODE(), PCRE2_INFO_NAMECOUNT, &name_count);

		if (name_count > 0)
		{
			// I think name_entry_size would be (<the longest name in pattern> + 3)
			// So, max_group_length == (name_entry_size - 3);
			uint32_t name_entry_size;
			::pcre2_pattern_info(GET_CODE(), PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

			// And, I think the name_table looks like the following:
			//
			// "[group index (2 Bytes)][Name]\0[group index (2 Bytes)][Name]\0"
			PCRE2_SPTR name_table;
			::pcre2_pattern_info(GET_CODE(), PCRE2_INFO_NAMETABLE, &name_table);

			for (uint32_t i = 0; i < name_count; i++)
			{
				// A group index corresponding to the name
				int group_index = (name_table[0] << 8) | name_table[1];

				// group name
				auto group_name = FromPcreStr(name_table + 2);

				// value
				auto start_offset = output_vectors[2 * group_index];
				auto end_offset = output_vectors[2 * group_index + 1];

				result.emplace(group_name, MatchGroup(base_address, start_offset, end_offset));

				name_table += name_entry_size;
			}
		}
		else
		{
			// No named substrings
		}

		return result;
	}
}  // namespace ov