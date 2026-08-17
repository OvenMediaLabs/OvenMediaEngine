//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace http
{
	// https://www.rfc-editor.org/rfc/rfc7541.html
	namespace hpack
	{
		class HuffmanCodec : public ov::Singleton<HuffmanCodec>
		{
		public:
			HuffmanCodec();
			std::shared_ptr<ov::Data> Encode(const ov::String &str);
			bool Decode(const std::shared_ptr<const ov::Data> &data, ov::String &str);
			
		private:
			// Build Tree and Map
			void Build(uint32_t code, uint8_t length, uint16_t symbol);
			// Build Map for encoding from symbol to code
			void BuildMap(uint32_t code, uint8_t length, uint16_t symbol);
			// Build Tree for decoding from code to symbol
			void BuildTree(uint32_t code, uint8_t length, uint16_t symbol);

			class Node
			{
			public:
				Node* GetLeft(bool create = false)
				{
					if (_left == nullptr && create == true)
					{
						_left = new Node();
					}

					return _left;
				}

				Node* GetRight(bool create = false)
				{
					if (_right == nullptr && create == true)
					{
						_right = new Node();
					}

					return _right;
				}

				void SetValue(uint16_t value)
				{
					_value = value;
					_is_leaf = true;
				}
				
				uint16_t GetValue()
				{
					return _value;
				}

				bool IsLeaf()
				{
					return _is_leaf;
				}
				
			private:
				Node* _left = nullptr;
				Node* _right = nullptr;
				uint16_t _value = 0;
				bool _is_leaf = false;
			};

			// 
			Node* _tree = new Node();
			std::unordered_map<uint8_t, std::pair<uint32_t, uint8_t>> _map;
		};
	}
}