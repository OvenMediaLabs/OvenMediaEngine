//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./json_builder.h"

#include "./json.h"

namespace ov
{
	JsonBuilder::JsonBuilder(const PrivateToken &token)
	{
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::Builder()
	{
		return std::make_shared<JsonBuilder>(PrivateToken());
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::Builder(JsonBuilderModifier modifier)
	{
		auto builder = Builder();

		return (modifier != nullptr) ? modifier(builder) : builder;
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBackInternal(const char *key, JsonValueType value)
	{
		if (_value_type == ::Json::ValueType::nullValue)
		{
			_value_type = ::Json::ValueType::objectValue;
		}

		if (_value_type != ::Json::ValueType::objectValue)
		{
			OV_ASSERT(false, "The current JSON value is not an object");
		}
		else
		{
			_value_map.PushBack(key, std::move(value));
		}

		return GetSharedPtr();
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBackInternal(JsonValueType item)
	{
		if (_value_type == ::Json::ValueType::nullValue)
		{
			_value_type = ::Json::ValueType::arrayValue;
		}

		if (_value_type != ::Json::ValueType::arrayValue)
		{
			OV_ASSERT(false, "The current JSON value is not an array");
		}
		else
		{
			_value_list.push_back(std::move(item));
		}

		return GetSharedPtr();
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(const char *key, ::Json::Value value)
	{
		return PushBackInternal(key, std::move(value));
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(const char *key, std::shared_ptr<JsonBuilder> builder)
	{
		return (builder == nullptr)
				   ? PushBackInternal(key, ::Json::Value::null)
				   : PushBackInternal(key, builder);
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(const char *key, JsonBuilderModifier modifier)
	{
		return (modifier == nullptr)
				   ? PushBackInternal(key, ::Json::Value::null)
				   : PushBackInternal(key, Builder(modifier));
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(::Json::Value value)
	{
		return PushBackInternal(std::move(value));
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(std::shared_ptr<JsonBuilder> builder)
	{
		return (builder == nullptr)
				   ? PushBackInternal(::Json::Value::null)
				   : PushBackInternal(builder);
	}

	std::shared_ptr<JsonBuilder> JsonBuilder::PushBack(JsonBuilderModifier modifier)
	{
		return (modifier == nullptr)
				   ? PushBackInternal(::Json::Value::null)
				   : PushBackInternal(Builder(modifier));
	}

	ov::String JsonBuilder::Stringify() const
	{
		ov::String output;
		return Stringify(output);
	}

	ov::String &JsonBuilder::Stringify(ov::String &output) const
	{
		switch (_value_type)
		{
			case ::Json::ValueType::nullValue:
				output = "";
				break;

			case ::Json::ValueType::objectValue:
				StringifyObject(output);
				break;

			case ::Json::ValueType::arrayValue:
				StringifyArray(output);
				break;

			default:
				OV_ASSERT(false, "Invalid JSON value type");
				break;
		}

		return output;
	}

	static std::string EscapeString(ov::String &output, const ov::String &value)
	{
		::Json::StreamWriterBuilder builder;

		return ::Json::writeString(builder, value.CStr()).c_str();
	}

	ov::String &JsonBuilder::StringifyObject(ov::String &output) const
	{
		auto end = _value_map.end();

		output.Append('{');

		for (auto it = _value_map.begin(); it != end; ++it)
		{
			if (it != _value_map.begin())
			{
				output.Append(',');
			}

			auto &key	= it->first;
			auto &value = it->second;

			// Print key
			output.AppendFormat("%s:", EscapeString(output, key).c_str());

			// Print value
			if (std::holds_alternative<::Json::Value>(value))
			{
				auto json = std::get<::Json::Value>(value);

				output.Append(Json::Stringify(json, false));
			}
			else if (std::holds_alternative<std::shared_ptr<JsonBuilder>>(value))
			{
				auto builder = std::get<std::shared_ptr<JsonBuilder>>(value);

				builder->Stringify(output);
			}
			else
			{
				OV_ASSERT(false, "Invalid JSON value type: %zu", value.index());
			}
		}

		output.Append('}');

		return output;
	}

	ov::String &JsonBuilder::StringifyArray(ov::String &output) const
	{
		auto end = _value_list.end();

		output.Append('[');

		for (auto it = _value_list.begin(); it != end; ++it)
		{
			if (it != _value_list.begin())
			{
				output.Append(',');
			}

			auto &value = *it;

			// Print value
			if (std::holds_alternative<::Json::Value>(value))
			{
				auto json = std::get<::Json::Value>(value);

				output.Append(Json::Stringify(json, false));
			}
			else if (std::holds_alternative<std::shared_ptr<JsonBuilder>>(value))
			{
				auto builder = std::get<std::shared_ptr<JsonBuilder>>(value);

				builder->Stringify(output);
			}
			else
			{
				OV_ASSERT(false, "Invalid JSON value type: %zu", value.index());
			}
		}

		output.Append(']');

		return output;
	}

	::Json::Value JsonBuilder::Build() const
	{
		switch (_value_type)
		{
			case ::Json::ValueType::nullValue:
				return ::Json::Value::null;

			case ::Json::ValueType::objectValue: {
				::Json::Value result(::Json::objectValue);

				for (auto &[key, value] : _value_map)
				{
					if (std::holds_alternative<::Json::Value>(value))
					{
						result[key.CStr()] = std::get<::Json::Value>(value);
					}
					else if (std::holds_alternative<std::shared_ptr<JsonBuilder>>(value))
					{
						auto builder	   = std::get<std::shared_ptr<JsonBuilder>>(value);
						result[key.CStr()] = builder->Build();
					}
					else
					{
						OV_ASSERT(false, "Invalid JSON value type: %zu", value.index());
					}
				}

				return result;
			}

			case ::Json::ValueType::arrayValue: {
				::Json::Value result(::Json::arrayValue);

				for (const auto &value : _value_list)
				{
					if (std::holds_alternative<::Json::Value>(value))
					{
						result.append(std::get<::Json::Value>(value));
					}
					else if (std::holds_alternative<std::shared_ptr<JsonBuilder>>(value))
					{
						auto builder = std::get<std::shared_ptr<JsonBuilder>>(value);
						result.append(builder->Build());
					}
					else
					{
						OV_ASSERT(false, "Invalid JSON value type: %zu", value.index());
					}
				}

				return result;
			}

			default:
				OV_ASSERT(false, "Invalid JSON value type");
				return ::Json::Value::null;
		}
	}
}  // namespace ov
