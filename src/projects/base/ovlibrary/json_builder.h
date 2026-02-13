//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <functional>
#include <variant>

#include "./enable_shared_from_this.h"
#include "./json_object.h"
#include "./sequencial_map.h"

namespace ov
{
	class JsonBuilder;

	using JsonBuilderModifier = std::function<std::shared_ptr<JsonBuilder>(std::shared_ptr<JsonBuilder> builder)>;

	// According to the specification, JSON works regardless of order, but when visualizing it as a JSON format
	// such as outputting logs, the order may be important, so we provide a JsonBuilder class that guarantees the order.
	//
	// Usage:
	//
	// ov::String json = ov::JsonBuilder::Builder()
	//     ->PushBack("key1", 100)
	//     ->PushBack("key2", "value")
	//     // If you don't need additional operations when set nested values, you can create a new builder and use it
	//     ->PushBack("sub_object",
	//                 ov::JsonBuilder::Builder()
	//                     ->PushBack("sub_key1", "sub_value1")
	//                     ->PushBack("sub_key2", "sub_value2")
	//                     ->PushBack("sub_key3", [](auto builder) -> auto {
	//                         return builder
	//                             ->PushBack("sub_sub_key1", "sub_sub_value1")
	//                             ->PushBack("sub_sub_key2", "sub_sub_value2");
	//                     }))
	//     // If you need additional operations when set nested values, you can use the modifier function
	//     ->PushBack("sub_array", [obj](auto builder) -> auto {
	//         ov::String s = "some complex value";
	//         s += "!";
	//
	//         return builder
	//             ->PushBack(s.CStr())
	//             ->PushBack("sub_array")
	//             ->PushBack([](auto builder) -> auto {
	//                 return builder
	//                     ->PushBack("sub_sub_key1", "sub_array1")
	//                     ->PushBack("sub_sub_key2", "sub_array2");
	//             });
	//     })
	//     ->Stringify();
	//
	// This will output:
	// {"key1":100,"key2":"value","sub_object":true,"sub_array":["some complex value!","sub_array",{"sub_sub_key1":"sub_array1","sub_sub_key2":"sub_array2"}]}
	class JsonBuilder : public ov::EnableSharedFromThis<JsonBuilder>
	{
	private:
		struct PrivateToken
		{
		};

		using JsonValueType = std::variant<
			::Json::Value,
			std::shared_ptr<JsonBuilder>>;

	public:
		// To ensure that the JsonBuilder object always uses shared_ptr
		JsonBuilder() = delete;
		JsonBuilder(const PrivateToken &token);
		~JsonBuilder() = default;

		// Create a new JsonBuilder object
		static std::shared_ptr<JsonBuilder> Builder();
		static std::shared_ptr<JsonBuilder> Builder(JsonBuilderModifier modifier);

		// These APIs can be used when the JSON value currently being created is an object
		std::shared_ptr<JsonBuilder> PushBack(const char *key, ::Json::Value value);
		std::shared_ptr<JsonBuilder> PushBack(const char *key, std::shared_ptr<JsonBuilder> builder);
		std::shared_ptr<JsonBuilder> PushBack(const char *key, JsonBuilderModifier modifier);

		// These APIs can be used when the JSON value currently being created is an array
		std::shared_ptr<JsonBuilder> PushBack(::Json::Value value);
		std::shared_ptr<JsonBuilder> PushBack(std::shared_ptr<JsonBuilder> builder);
		std::shared_ptr<JsonBuilder> PushBack(JsonBuilderModifier modifier);

		// To JSON string
		ov::String Stringify() const;

		// To JSON object/array
		::Json::Value Build() const;

	protected:
		std::shared_ptr<JsonBuilder> PushBackInternal(const char *key, JsonValueType item);
		std::shared_ptr<JsonBuilder> PushBackInternal(JsonValueType item);

		ov::String &Stringify(ov::String &output) const;
		ov::String &StringifyObject(ov::String &output) const;
		ov::String &StringifyArray(ov::String &output) const;

	protected:
		::Json::ValueType _value_type = ::Json::ValueType::nullValue;

		SequencialMap<ov::String, JsonValueType> _value_map;
		std::vector<JsonValueType> _value_list;
	};
}  // namespace ov
