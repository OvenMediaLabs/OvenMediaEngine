//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: SEIEvent validation, parsing and the JSON round trip its packet rides
//
//==============================================================================
#include <base/modules/data_format/sei_event/sei_event.h>
#include <gtest/gtest.h>

namespace
{
	Json::Value ApiEvent(const char *body)
	{
		return ov::Json::Parse(body).GetJsonValue();
	}

	std::shared_ptr<pugi::xml_document> XmlValues(const char *body)
	{
		auto document = std::make_shared<pugi::xml_document>();
		if (document->load_string(body).status != pugi::status_ok)
		{
			return nullptr;
		}

		return document;
	}
}  // namespace

// ---------------------------------------------------------------------------
// The sendEvent API
// ---------------------------------------------------------------------------

TEST(SeiEvent, ApiAcceptsUserDataUnregistered)
{
	auto event = ApiEvent(R"({"seiType": "UserDataUnregistered", "data": "hello"})");

	ASSERT_TRUE(SEIEvent::IsValid(event));

	auto parsed = SEIEvent::Parse(event);
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::USER_DATA_UNREGISTERED);
	EXPECT_EQ(parsed->GetData(), "hello");
	// An API event is aimed at the next picture, so there is nothing to wait for
	EXPECT_FALSE(parsed->IsKeyframeOnly());
}

// PictureTiming keeps stamping and rewrites the SPS, so it is a stream setting rather than a
// one-off event and only the EventGenerator XML may ask for it
TEST(SeiEvent, ApiRejectsPictureTiming)
{
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": "PictureTiming"})")));
}

TEST(SeiEvent, ApiRequiresASeiTypeString)
{
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"data": "hello"})")))
		<< "an omitted seiType used to default to UserDataUnregistered";
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": ""})")));

	// asString() throws on a non string, which would surface as a 500 instead of a 400
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": 1})")));
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": {}})")));
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": null})")));
}

TEST(SeiEvent, ApiRejectsAnUnknownSeiType)
{
	EXPECT_FALSE(SEIEvent::IsValid(ApiEvent(R"({"seiType": "SomethingElse"})")));
}

// ---------------------------------------------------------------------------
// The EventGenerator XML
// ---------------------------------------------------------------------------

TEST(SeiEvent, XmlAcceptsBothSupportedTypes)
{
	auto user_data = XmlValues("<Values><SeiType>UserDataUnregistered</SeiType></Values>");
	ASSERT_NE(user_data, nullptr);
	EXPECT_TRUE(SEIEvent::IsValid(user_data->child("Values")));

	auto pic_timing = XmlValues("<Values><SeiType>PictureTiming</SeiType></Values>");
	ASSERT_NE(pic_timing, nullptr);
	EXPECT_TRUE(SEIEvent::IsValid(pic_timing->child("Values")));

	auto unknown = XmlValues("<Values><SeiType>SomethingElse</SeiType></Values>");
	ASSERT_NE(unknown, nullptr);
	EXPECT_FALSE(SEIEvent::IsValid(unknown->child("Values")));
}

TEST(SeiEvent, XmlDefaultsToUserDataUnregistered)
{
	auto document = XmlValues("<Values><Data>hello</Data></Values>");
	ASSERT_NE(document, nullptr);

	auto values = document->child("Values");
	ASSERT_TRUE(SEIEvent::IsValid(values));

	auto parsed = SEIEvent::Parse(values);
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::USER_DATA_UNREGISTERED);
	EXPECT_FALSE(parsed->IsKeyframeOnly());
}

TEST(SeiEvent, XmlDefaultsToUtc)
{
	auto document = XmlValues("<Values><SeiType>PictureTiming</SeiType></Values>");
	ASSERT_NE(document, nullptr);

	auto parsed = SEIEvent::Parse(document->child("Values"));
	ASSERT_NE(parsed, nullptr);
	EXPECT_FALSE(parsed->GetTimezone().local) << "an absent <Timezone> is UTC, not the server's zone";
	EXPECT_EQ(parsed->GetTimezone().offset_seconds, 0);
}

TEST(SeiEvent, XmlReadsTheTimezone)
{
	struct
	{
		const char *text;
		bool local;
		int32_t offset_seconds;
	} const cases[] = {
		{"UTC", false, 0},
		{"Local", true, 0},
		{"+09:00", false, 9 * 3600},
		{"-05:00", false, -5 * 3600},
	};

	for (const auto &entry : cases)
	{
		auto document = XmlValues(ov::String::FormatString(
									  "<Values><SeiType>PictureTiming</SeiType><Timezone>%s</Timezone></Values>",
									  entry.text)
									  .CStr());
		ASSERT_NE(document, nullptr) << entry.text;

		auto values = document->child("Values");
		ASSERT_TRUE(SEIEvent::IsValid(values)) << entry.text;

		auto parsed = SEIEvent::Parse(values);
		ASSERT_NE(parsed, nullptr) << entry.text;
		EXPECT_EQ(parsed->GetTimezone().local, entry.local) << entry.text;
		EXPECT_EQ(parsed->GetTimezone().offset_seconds, entry.offset_seconds) << entry.text;
	}
}

// Caught at config load, where the XML is printed, rather than at insertion time
TEST(SeiEvent, XmlRejectsATimezoneItCannotRead)
{
	auto document = XmlValues("<Values><SeiType>PictureTiming</SeiType><Timezone>Asia/Seoul</Timezone></Values>");
	ASSERT_NE(document, nullptr);
	EXPECT_FALSE(SEIEvent::IsValid(document->child("Values")));
}

TEST(SeiEvent, XmlReadsKeyframeOnly)
{
	auto document = XmlValues("<Values><SeiType>PictureTiming</SeiType><KeyframeOnly>true</KeyframeOnly></Values>");
	ASSERT_NE(document, nullptr);

	auto parsed = SEIEvent::Parse(document->child("Values"));
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::PICTURE_TIMING);
	EXPECT_TRUE(parsed->IsKeyframeOnly());

	// Anything other than "true" is false, and an absent element reads as ""
	auto off = XmlValues("<Values><KeyframeOnly>yes</KeyframeOnly></Values>");
	ASSERT_NE(off, nullptr);
	EXPECT_FALSE(SEIEvent::Parse(off->child("Values"))->IsKeyframeOnly());
}

// ---------------------------------------------------------------------------
// The packet the event rides on
// ---------------------------------------------------------------------------

TEST(SeiEvent, SurvivesTheJsonRoundTrip)
{
	H264SeiTimecodeZone timezone;
	ASSERT_TRUE(H264SeiTimecodeZone::Parse("+09:00", timezone));

	auto source = std::make_shared<SEIEvent>();
	source->SetSeiType("PictureTiming");
	source->SetData("CurrentTime:${EpochTime}");
	source->SetKeyframeOnly(true);
	source->SetTimezone(timezone);

	auto bytes = source->Serialize();
	ASSERT_NE(bytes, nullptr);

	auto parsed = SEIEvent::Deserialize(bytes);
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::PICTURE_TIMING);
	EXPECT_TRUE(parsed->IsKeyframeOnly()) << "KeyframeOnly has to reach the consumer";
	// ${EpochTime} is substituted at insertion time, not here
	EXPECT_EQ(parsed->GetData(), "CurrentTime:${EpochTime}");

	EXPECT_FALSE(parsed->GetTimezone().local) << "the timezone has to reach the consumer";
	EXPECT_EQ(parsed->GetTimezone().offset_seconds, 9 * 3600);
}

// Every way of asking for no particular zone: the default, an event from a build that had no
// timezone field, and one naming a zone this build cannot resolve
TEST(SeiEvent, DeserializeFallsBackToUtc)
{
	auto as_data = [](const char *text) {
		return std::make_shared<ov::Data>(text, ::strlen(text));
	};

	auto source = std::make_shared<SEIEvent>();
	source->SetSeiType("PictureTiming");

	// Serialize() always writes a timezone, so this is the default coming back
	auto round_tripped = SEIEvent::Deserialize(source->Serialize());
	ASSERT_NE(round_tripped, nullptr);
	EXPECT_FALSE(round_tripped->GetTimezone().local);

	auto without_field = SEIEvent::Deserialize(as_data(R"({"seiType": "PictureTiming"})"));
	ASSERT_NE(without_field, nullptr);
	EXPECT_FALSE(without_field->GetTimezone().local);

	// An unreadable zone is not worth dropping the event over
	auto unreadable = SEIEvent::Deserialize(as_data(R"({"seiType": "PictureTiming", "timezone": "Asia/Seoul"})"));
	ASSERT_NE(unreadable, nullptr);
	EXPECT_FALSE(unreadable->GetTimezone().local);
}

TEST(SeiEvent, SerializeFallsBackToUserDataUnregistered)
{
	auto source = std::make_shared<SEIEvent>();
	source->SetData("hello");

	auto parsed = SEIEvent::Deserialize(source->Serialize());
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::USER_DATA_UNREGISTERED);
}

TEST(SeiEvent, DeserializeRejectsAnythingItDidNotWrite)
{
	EXPECT_EQ(SEIEvent::Deserialize(nullptr), nullptr);
	EXPECT_EQ(SEIEvent::Deserialize(std::make_shared<ov::Data>()), nullptr);

	auto as_data = [](const char *text) {
		return std::make_shared<ov::Data>(text, ::strlen(text));
	};

	EXPECT_EQ(SEIEvent::Deserialize(as_data("not json at all")), nullptr);
	EXPECT_EQ(SEIEvent::Deserialize(as_data("[1, 2, 3]")), nullptr) << "an array is not an event";

	// seiType is what makes this an SEI event
	EXPECT_EQ(SEIEvent::Deserialize(as_data("{}")), nullptr);
	EXPECT_EQ(SEIEvent::Deserialize(as_data(R"({"data": "hello"})")), nullptr);
	EXPECT_EQ(SEIEvent::Deserialize(as_data(R"({"seiType": ""})")), nullptr);
	EXPECT_EQ(SEIEvent::Deserialize(as_data(R"({"seiType": 5})")), nullptr);
}

// An unknown name is let through so the consumer can name it in the line it logs when it discards
// the event
TEST(SeiEvent, DeserializeKeepsAnUnknownSeiType)
{
	auto as_data = R"({"seiType": "SomethingElse", "data": "hello"})";
	auto parsed	 = SEIEvent::Deserialize(std::make_shared<ov::Data>(as_data, ::strlen(as_data)));

	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->GetSeiPayloadType(), H264SEI::PayloadType::UNKNOWN);
}
