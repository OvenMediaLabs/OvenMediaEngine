//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"

namespace info
{

	class AudioMapItem
	{
	public:
		AudioMapItem(int index, const ov::String &name, const ov::String &language, const ov::String &characteristics)
			: _index(index),
			  _name(name),
			  _language(language),
			  _characteristics(characteristics)
		{
		}

		// Setter
		void SetIndex(int index)
		{
			_index = index;
		}

		void SetName(const ov::String &name)
		{
			_name = name;
		}

		void SetLanguage(const ov::String &language)
		{
			_language = language;
		}

		void SetCharacteristics(const ov::String &characteristics)
		{
			_characteristics = characteristics;
		}

		// Getter
		int GetIndex() const
		{
			return _index;
		}

		const ov::String &GetName() const
		{
			return _name;
		}

		const ov::String &GetLanguage() const
		{
			return _language;
		}

		const ov::String &GetCharacteristics() const
		{
			return _characteristics;
		}

	private:
		int _index;
		ov::String _name;
		ov::String _language = "und";
		ov::String _characteristics;
	};
} // namespace info