#include "file_export.h"

#include <base/info/stream.h>
#include <base/publisher/stream.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include "file_private.h"

namespace pub
{
	FileExport::FileExport()
	{
	}

	FileExport::~FileExport()
	{
	}

	bool FileExport::ExportRecordToXml(const ov::String path, const std::shared_ptr<info::Record> &record)
	{
		std::shared_lock<std::shared_mutex> lock(_mutex);

		pugi::xml_document doc;

		pugi::xml_parse_result result = doc.load_file(path.CStr(), pugi::parse_default | pugi::parse_declaration);
		if (!result)
		{
			auto declarationNode = doc.append_child(pugi::node_declaration);
			declarationNode.append_attribute("version") = "1.0";
			declarationNode.append_attribute("encoding") = "utf-8";

			doc.append_child("files");
		}

		// A valid XML document must have a single root node
		pugi::xml_node root = doc.document_element();

		auto item = root.append_child("file");

		item.append_child("transactionId").append_child(pugi::node_pcdata).set_value(record->GetTransactionId().CStr());
		item.append_child("id").append_child(pugi::node_pcdata).set_value(record->GetId().CStr());
		item.append_child("metadata").append_child(pugi::node_pcdata).set_value(record->GetMetadata().CStr());
		item.append_child("vhost").append_child(pugi::node_pcdata).set_value(record->GetVhost().CStr());
		item.append_child("app").append_child(pugi::node_pcdata).set_value(record->GetApplication().CStr());
		item.append_child("stream").append_child(pugi::node_pcdata).set_value(record->GetStreamName().CStr());

		item.append_child("filePath").append_child(pugi::node_cdata).set_value(record->GetOutputFilePath().CStr());

		item.append_child("recordBytes").append_child(pugi::node_pcdata).set_value(std::to_string(record->GetRecordBytes()).c_str());
		item.append_child("recordTime").append_child(pugi::node_pcdata).set_value(std::to_string(record->GetRecordTime()).c_str());

		item.append_child("sequence").append_child(pugi::node_pcdata).set_value(std::to_string(record->GetSequence()).c_str());
		item.append_child("lastSequence").append_child(pugi::node_pcdata).set_value((record->GetEnable() == false) ? "true" : "false");

		item.append_child("createdTime").append_child(pugi::node_pcdata).set_value(ov::Converter::ToISO8601String(record->GetCreatedTime()).CStr());
		item.append_child("startTime").append_child(pugi::node_pcdata).set_value(ov::Converter::ToISO8601String(record->GetRecordStartTime()).CStr());
		item.append_child("finishTime").append_child(pugi::node_pcdata).set_value(ov::Converter::ToISO8601String(record->GetRecordStopTime()).CStr());

		bool is_success = doc.save_file(path.CStr(), PUGIXML_TEXT("  "));

		if (chmod(path.CStr(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0)
		{
			logtw("Could not change permission. path(%s)", path.CStr());
		}

		return is_success;
	}
}  // namespace pub