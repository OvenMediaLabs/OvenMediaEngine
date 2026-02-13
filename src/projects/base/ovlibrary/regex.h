//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "./error.h"

// These classes are a simple PCRE2 wrapper
//
// Usage:
//
//    auto regex = ov::Regex("(?<token1>[^/]+)/.+");
//    auto error = regex.Compile();
//
//    if (error == nullptr)
//    {
//        auto matches = regex.Matches("Dim/iden");
//
//        auto group_list = matches.GetGroupList();
//
//        printf("=== Group List ===\n");
//        int index = 0;
//        for (auto &group : group_list)
//        {
//            printf("%d: %.*s\n", index, static_cast<int>(group.length()), group.data());
//            index++;
//        }
//
//        printf("=== Named Group List ===\n");
//        auto named_list = matches.GetNamedGroupList();
//
//        for (auto &item : named_list)
//        {
//            auto &key = item.first;
//            auto &value = item.second;
//
//            printf("%s = %.*s\n", key.CStr(), static_cast<int>(value.length()), value.data());
//        }
//    }
//    else
//    {
//        printf("%s\n", error->What());
//    }

namespace ov
{
	class MatchGroup
	{
	public:
		MatchGroup() = default;
		MatchGroup(const char *base_address, size_t start_offset, size_t end_offset);

		bool IsValid() const
		{
			return (_base_address != nullptr);
		}

		size_t GetStartOffset() const
		{
			return _start_offset;
		}

		size_t GetEndOffset() const
		{
			return _end_offset;
		}

		size_t GetLength() const
		{
			return _end_offset - _start_offset;
		}

		ov::String GetValue() const
		{
			if (_base_address != nullptr)
			{
				return ov::String(_base_address + _start_offset, _length);
			}

			return {};
		}

	protected:
		// Because _base_address refers to MatchResult, MatchGroup is also unavailable when MatchResult is released.
		const char *_base_address = nullptr;

		size_t _start_offset = 0;
		size_t _end_offset = 0;
		size_t _length = 0;
	};

	// Match result must be a
	class MatchResult
	{
	public:
		MatchResult();

		// MatchResult with error
		MatchResult(const std::shared_ptr<ov::Error> &error);

		// MatchResult with results
		MatchResult(std::shared_ptr<ov::String> subject,
					std::vector<MatchGroup> group_list,
					std::unordered_map<ov::String, MatchGroup> named_group);

		std::shared_ptr<const ov::Error> GetError() const;

		bool IsMatched() const
		{
			return ((_error == nullptr) && (GetGroupCount() > 0));
		}

		const ov::String &GetSubject() const;

		size_t GetGroupCount() const;
		MatchGroup GetGroupAt(size_t index) const;
		const std::vector<MatchGroup> &GetGroupList() const;

		size_t GetNamedGroupCount() const;
		MatchGroup GetNamedGroup(const char *name) const;
		const std::unordered_map<ov::String, MatchGroup> &GetNamedGroupList() const;

	protected:
		std::shared_ptr<Error> _error;

		// Stored as std::shared_ptr for ov::String to work well even if MatchResult is copied
		std::shared_ptr<ov::String> _subject;

		std::vector<MatchGroup> _group_list;
		std::unordered_map<ov::String, MatchGroup> _named_group;
	};

	class Regex
	{
	public:
		enum class Option
		{
			None,

			// PCRE2_CASELESS
			CaseInsensitive,
			// PCRE2_MULTILINE
			Multiline,
			// PCRE2_DOTALL
			DotAll,
			// PCRE2_LITERAL
			Literal
		};

		Regex();
		// TODO(dimiden): Implement a feature to support multiple options
		Regex(const char *pattern, Option options);
		Regex(const char *pattern);
		Regex(const Regex &regex);
		Regex(Regex &&regex);
		~Regex();

		static Regex CompiledRegex(const char *pattern, Option options);
		static Regex CompiledRegex(const char *pattern);

		// Transform the wildcard-type string to be used in regex
		//
		// 1) Escape special characters: '[', '\', '.', '/', '+', '{', '}', '$', '^', '|' to \<char>
		// 2) Replace '*', '?' to '.*', '.?'
		// 3) Prepend '^' and append '$' if exact_match == true
		//
		// For example: *.[a/irensoft.com => .*\.\[a\/irensoft\.com
		static ov::String WildCardRegex(const ov::String &wildcard_string, bool exact_match = true);

		const ov::String &GetPattern() const;

		std::shared_ptr<Error> Compile();
		bool IsCompiled() const
		{
			return (_code != nullptr);
		}

		MatchResult Matches(const char *subject) const;

		// Replace all occurrences of the pattern in the subject string with replace_to
		// For example:
		//     ov::Regex regex("[el]+");
		//     regex.Compile();
		//     regex.Replace("Hello", "!"); // It returns "H!o"
		ov::String Replace(const ov::String &subject, const ov::String &replace_with, bool replace_all = false, std::shared_ptr<const ov::Error> *error = nullptr) const;

		Regex &operator=(const Regex &regex);

		void Release();

	protected:
		std::unordered_map<ov::String, MatchGroup> CreateNamedGroupMap(const char *base_address, const size_t *output_vectors) const;

		ov::String _pattern;
		Option _options;

		// To hide pcre2_code
		void *_code = nullptr;
	};
}  // namespace ov
