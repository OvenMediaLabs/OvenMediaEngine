#include "file_session.h"

#include <base/info/application.h>
#include <base/info/host.h>
#include <base/info/stream.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/publisher/application.h>
#include <base/publisher/stream.h>
#include <config/config.h>
#include <modules/ffmpeg/compat.h>
#include <modules/ffmpeg/writer.h>

#include <regex>

#include "file_export.h"
#include "file_private.h"
#include "file_macro.h"

namespace pub
{
	std::shared_ptr<FileSession> FileSession::Create(const std::shared_ptr<pub::Application> &application,
													 const std::shared_ptr<pub::Stream> &stream,
													 uint32_t session_id)
	{
		auto session_info = info::Session(*std::static_pointer_cast<info::Stream>(stream), session_id);
		auto session = std::make_shared<FileSession>(session_info, application, stream);

		return session;
	}

	FileSession::FileSession(const info::Session &session_info,
							 const std::shared_ptr<pub::Application> &application,
							 const std::shared_ptr<pub::Stream> &stream)
		: pub::Session(session_info, application, stream),
		  _writer(nullptr)
	{
		MonitorInstance->OnSessionConnected(*stream, PublisherType::File);
	}

	FileSession::~FileSession()
	{
		logtt("FileSession(%d) has been terminated finally", GetId());
		MonitorInstance->OnSessionDisconnected(*GetStream(), PublisherType::File);
	}

	bool FileSession::Start()
	{
		_is_splitting.store(false);

		if (StartRecord() == false)
		{
			logte("Failed to start recording. id(%d)", GetId());

			return false;
		}

		logtt("FileSession(%d) has started.", GetId());

		return Session::Start();
	}

	bool FileSession::Stop()
	{
		if (StopRecord() == false)
		{
			logte("Failed to stop recording. id(%d)", GetId());

			return false;
		}

		logtt("FileSession(%d) has stoped", GetId());

		return Session::Stop();
	}

	bool FileSession::Split()
	{
		if (StopRecord() == false)
		{
			logte("Failed to stop recording. id(%d)", GetId());
			return false;
		}

		if (StartRecord() == false)
		{
			logte("Failed to start recording. id(%d)", GetId());
			return false;
		}

		return true;
	}

	bool FileSession::StartRecord()
	{
		auto record = GetRecord();
		if (record == nullptr)
		{
			logte("Record information is not set. id(%d)", GetId());
			return false;
		}

		record->UpdateRecordStartTime();
		record->SetOutputFilePath(GetOutputFilePath());
		record->SetOutputInfoPath(GetOutputFileInfoPath());
		record->SetTmpPath(GetOutputTempFilePath(record));
		record->SetState(info::Record::RecordState::Recording);

		// Get extension and container format
		ov::String output_extension = ov::PathManager::ExtractExtension(record->GetOutputFilePath());
		ov::String output_format = ffmpeg::compat::GetFormatByExtension(output_extension, "mpegts");

		// Create directory for temporary file
		ov::String tmp_directory = ov::PathManager::ExtractPath(record->GetTmpPath());
		ov::String tmp_real_directory = ov::PathManager::Combine(GetRootPath(), tmp_directory);

		if (ov::PathManager::MakeDirectoryRecursive(tmp_real_directory.CStr()) == false)
		{
			logte("Could not create directory. path(%s)", tmp_real_directory.CStr());

			SetState(SessionState::Error);
			record->SetState(info::Record::RecordState::Error);

			return false;
		}

		logtt("OutputFilePath : %s", record->GetOutputFilePath().CStr());
		logtt("OutputInfoPath : %s", record->GetOutputInfoPath().CStr());
		logtt("TmpPath        : %s", record->GetTmpPath().CStr());
		logtt("TmpDirectory   : %s", tmp_directory.CStr());
		logtt("TmpRealDirectory   : %s", tmp_real_directory.CStr());

		auto writer = CreateWriter();
		if (writer == nullptr)
		{
			SetState(SessionState::Error);
			record->SetState(info::Record::RecordState::Error);
			logte("Failed to create writer. %s", record->GetInfoString().CStr());
			return false;
		}

		if (writer->SetUrl(ov::PathManager::Combine(GetRootPath(), record->GetTmpPath()), output_format) == false)
		{
			SetState(SessionState::Error);
			record->SetState(info::Record::RecordState::Error);
			logte("Failed to set URL. Reason(%s), %s", writer->GetErrorMessage().CStr(), record->GetInfoString().CStr());
			return false;
		}

		// The mode to specify the initial value of the timestamp stored in the file to zero,
		// or keep it at the same value as the source timestamp
		if (record->GetSegmentationRule() == "continuity")
		{
			writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_PASSTHROUGH_MODE);
		}
		else if (record->GetSegmentationRule() == "discontinuity")
		{
			writer->SetTimestampMode(ffmpeg::Writer::TIMESTAMP_STARTZERO_MODE);
		}

		// Add Tracks
		if (record->GetTrackIds().empty() && record->GetVariantNames().empty())
		{
			// If there is no specified track, add all tracks.
			for (auto &[track_id, media_track] : GetStream()->GetTracks())
			{
				if (AddTrack(output_format, media_track) == false)
				{
					continue;
				}
			}
		}
		else
		{
			// Select tracks by VariantNames
			for (const auto &variant_name : record->GetVariantNames())
			{
				// VariantName format: "variantName:index", "variantName" (index is optional). if index is not specified, 0 is used.
				auto vars			  = variant_name.Split(":", 2);
				ov::String variant = vars[0];
				int32_t variant_index	  = (vars.size() >= 2) ? ov::Converter::ToInt32(vars[1], 0) : 0;

				// Find MediaTrack by VariantName and Index
				auto media_track	  = GetStream()->GetTrackByVariant(variant, variant_index);
				if (media_track == nullptr)
				{
					logtw("FileSession(%d) - Could not find track by VariantName: %s : %d", GetId(), variant.CStr(), variant_index);
					continue;
				}

				if (AddTrack(output_format, media_track) == false)
				{
					continue;
				}
			}

			// Select tracks by TrackIds
			for (const auto &track_id : record->GetTrackIds())
			{
				auto media_track = GetStream()->GetTrack(track_id);
				if (media_track == nullptr)
				{
					logtw("FileSession(%d) - Could not find track by TrackId: %d", GetId(), track_id);
					continue;
				}

				if (AddTrack(output_format, media_track) == false)
				{
					continue;
				}
			}
		}

		logtd("Create temporary file(%s) and default track id(%d)", writer->GetUrl().CStr(), _default_track);

		if (writer->Start() == false)
		{
			SetState(SessionState::Error);
			record->SetState(info::Record::RecordState::Error);
			logte("Failed to start writer. Reason(%s), %s", writer->GetErrorMessage().CStr(), record->GetInfoString().CStr());
			return false;
		}

		logtd("Recording Started. %s", record->GetInfoString().CStr());

		return true;
	}

	// Select the first video track as the default track.
	// If there is no video track, the first track of the audio is selected as the default track.
	void FileSession::SelectDefaultTrack(const std::shared_ptr<MediaTrack> &track)
	{
		if (_default_track_by_type.find(track->GetMediaType()) == _default_track_by_type.end())
		{
			_default_track_by_type[track->GetMediaType()] = track->GetId();

			if (_default_track_by_type.find(cmn::MediaType::Video) != _default_track_by_type.end())
			{
				_default_track = _default_track_by_type[cmn::MediaType::Video];
			}
			else if (_default_track_by_type.find(cmn::MediaType::Audio) != _default_track_by_type.end())
			{
				_default_track = _default_track_by_type[cmn::MediaType::Audio];
			}
		}
	}

	bool FileSession::StopRecord()
	{
		auto writer = GetWriter();
		if (writer != nullptr)
		{
			writer->Stop();

			// If current state is in the middle of a split recording, do not change the session state.
			if(_is_splitting.load() == false)
			{
				SetState(SessionState::Stopping);
			}

			auto record = GetRecord();
			if (record)
			{
				record->SetState(info::Record::RecordState::Stopping);
				record->UpdateRecordStopTime();
				record->SetOutputFilePath(GetOutputFilePath());
				record->SetOutputInfoPath(GetOutputFileInfoPath());

				// Create directory for recorded file
				ov::String output_path = ov::PathManager::Combine(GetRootPath(), record->GetOutputFilePath());
				ov::String output_directory = ov::PathManager::ExtractPath(output_path);

				if (ov::PathManager::MakeDirectoryRecursive(output_directory.CStr()) == false)
				{
					logte("Could not create directory. path: %s", output_directory.CStr());

					SetState(SessionState::Error);
					record->SetState(info::Record::RecordState::Error);

					return false;
				}

				// Create directory for information file
				ov::String info_path = ov::PathManager::Combine(GetRootPath(), record->GetOutputInfoPath());
				ov::String info_directory = ov::PathManager::ExtractPath(info_path);

				if (ov::PathManager::MakeDirectoryRecursive(info_directory.CStr()) == false)
				{
					logte("Could not create directory. path: %s", info_directory.CStr());

					SetState(SessionState::Error);
					record->SetState(info::Record::RecordState::Error);

					return false;
				}

				// Moves temporary files to a user-defined path.
				ov::String tmp_output_path = writer->GetUrl();

				if (rename(tmp_output_path.CStr(), output_path.CStr()) != 0)
				{
					logte("Failed to move file. from: %s to: %s", tmp_output_path.CStr(), output_path.CStr());

					SetState(SessionState::Error);
					record->SetState(info::Record::RecordState::Error);

					return false;
				}

				logtd("Replace the temporary file name with the target file name. from: %s, to: %s", tmp_output_path.CStr(), output_path.CStr());

				// Append recorded information to the information file
				if (FileExport::GetInstance()->ExportRecordToXml(info_path, record) == false)
				{
					logte("Failed to export xml file. path: %s", info_path.CStr());
				}

				logtd("Appends the recording result to the information file. path: %s", info_path.CStr());

				record->SetState(info::Record::RecordState::Stopped);
				
				logtd("Recording Completed. %s", record->GetInfoString().CStr());
								
				record->IncreaseSequence();
			}

			DestroyWriter();
		}

		return true;
	}

	void FileSession::SendOutgoingData(const std::any &packet)
	{
		if (GetState() != SessionState::Started)
		{
			return;
		}

		std::shared_ptr<MediaPacket> session_packet;

		try
		{
			session_packet = std::any_cast<std::shared_ptr<MediaPacket>>(packet);
			if (session_packet == nullptr)
			{
				return;
			}
		}
		catch (const std::bad_any_cast &e)
		{
			logtw("An incorrect type of packet was input from the stream. (%s)", e.what());

			return;
		}

		auto record = GetRecord();
		if (!record)
		{
			logte("Record information is not set. id(%d)", GetId());
			return;
		}

		// Drop until the first keyframe of the main track is received.
		if (_found_first_keyframe == false)
		{
			if (_default_track == session_packet->GetTrackId())
			{
				if ((session_packet->GetMediaType() == cmn::MediaType::Audio) ||
					(session_packet->GetMediaType() == cmn::MediaType::Video && session_packet->GetFlag() == MediaPacketFlag::Key))
				{
					_found_first_keyframe = true;
				}
				else
				{
					record->UpdateRecordStartTime();
					return;
				}
			}
		}

		_is_splitting.store(false);

		// When setting interval parameter, perform segmentation recording.
		if (((uint64_t)record->GetInterval() > 0) && (record->GetRecordTime() > (uint64_t)record->GetInterval()) &&
			(session_packet->GetTrackId() == _default_track) &&
			((session_packet->GetMediaType() == cmn::MediaType::Audio) ||
			(session_packet->GetMediaType() == cmn::MediaType::Video && session_packet->GetFlag() == MediaPacketFlag::Key)) )
		{
			_is_splitting.store(true);
		}
		// When setting schedule parameter, perform segmentation recording.
		else if (record->GetSchedule().IsEmpty() != true)
		{
			if (record->IsNextScheduleTimeEmpty() == true)
			{
				if (record->UpdateNextScheduleTime() == false)
				{
					logte("Failed to update next schedule time. request to stop recording.");
				}
			}
			else if (record->GetNextScheduleTime() <= std::chrono::system_clock::now())
			{
				_is_splitting.store(true);

				if (record->UpdateNextScheduleTime() == false)
				{
					logte("Failed to update next schedule time. request to stop recording.");
				}
			}
		}

		if (_is_splitting.load())
		{
			Split();
		}

		auto writer = GetWriter();
		if (writer != nullptr)
		{
			uint64_t sent_bytes = 0;

			bool ret = writer->SendPacket(session_packet, &sent_bytes);
			if (ret == false)
			{
				SetState(SessionState::Error);
				record->SetState(info::Record::RecordState::Error);
				logte("Failed to write packet. Reason(%s), %s", writer->GetErrorMessage().CStr(), record->GetInfoString().CStr());
				DestroyWriter();

				return;
			}

			record->UpdateRecordTime();
			record->IncreaseRecordBytes(sent_bytes);
			
			MonitorInstance->IncreaseBytesOut(*GetStream(), PublisherType::File, sent_bytes);
		}
	}

	void FileSession::SetRecord(std::shared_ptr<info::Record> &record)
	{
		std::lock_guard<std::shared_mutex> mlock(_record_mutex);

		_record = record;
	}

	std::shared_ptr<info::Record> &FileSession::GetRecord()
	{
		std::shared_lock<std::shared_mutex> mlock(_record_mutex);
		return _record;
	}

	ov::String FileSession::GetRootPath()
	{
		auto app_config = std::static_pointer_cast<info::Application>(GetApplication())->GetConfig();
		auto file_config = app_config.GetPublishers().GetFilePublisher();

		return file_config.GetRootPath();
	}

	ov::String FileSession::GetOutputTempFilePath(std::shared_ptr<info::Record> &record)
	{
		ov::String tmp_directory = ov::PathManager::ExtractPath(record->GetOutputFilePath());

		// If the tmp_directory is relative path "./", change it to empty string.
		if (tmp_directory.HasPrefix("./") && tmp_directory.GetLength() == 2)
		{
			tmp_directory = "";
		}

		ov::String tmp_filename = ov::String::FormatString("tmp_%s", ov::Random::GenerateString(32).CStr());

		return ov::PathManager::Combine(tmp_directory, tmp_filename);
	}

	ov::String FileSession::GetOutputFilePath()
	{
		auto app_config = std::static_pointer_cast<info::Application>(GetApplication())->GetConfig();
		auto file_config = app_config.GetPublishers().GetFilePublisher();
		auto record = GetRecord();
		if(!record)
		{
			logte("Record information is not set. id(%d)", GetId());

			return "";
		}

		ov::String template_path = "";
		if (record->IsFilePathSetByUser() == true)
		{
			template_path = record->GetFilePath();
		}
		else
		{
			template_path = file_config.GetFilePath();
		}

		// If FilePath config is not set, save it as the default path.
		if (template_path.GetLength() == 0 || template_path.IsEmpty() == true)
		{
			template_path.Format("${TransactionId}_${VirtualHost}_${Application}_${Stream}_${StartTime:YYYYMMDDhhmmss}.ts");
		}

		auto result = FileMacro::ConvertMacro(std::static_pointer_cast<info::Application>(GetApplication()), std::static_pointer_cast<info::Stream>(GetStream()), record, template_path);

		return result;
	}

	ov::String FileSession::GetOutputFileInfoPath()
	{
		auto app_config = std::static_pointer_cast<info::Application>(GetApplication())->GetConfig();
		auto file_config = app_config.GetPublishers().GetFilePublisher();
		auto record = GetRecord();
		if(!record)
		{
			logte("Record information is not set. id(%d)", GetId());
			
			return "";
		}

		ov::String template_path = "";

		if (record->IsInfoPathSetByUser() == true)
		{
			template_path = record->GetInfoPath();
		}
		else
		{
			template_path = file_config.GetInfoPath();
		}

		// If FileInfoPath config is not set, save it as the default path.
		if (template_path.GetLength() == 0 || template_path.IsEmpty() == true)
		{
			template_path.Format("${TransactionId}_${VirtualHost}_${Application}_${Stream}.xml");
		}

		auto result = FileMacro::ConvertMacro(std::static_pointer_cast<info::Application>(GetApplication()), std::static_pointer_cast<info::Stream>(GetStream()), record, template_path);

		return result;
	}

	std::shared_ptr<ffmpeg::Writer> FileSession::CreateWriter()
	{
		std::lock_guard<std::shared_mutex> lock(_writer_mutex);
		if (_writer != nullptr)
		{
			_writer->Stop();
			_writer = nullptr;
		}

		_writer = ffmpeg::Writer::Create();
		if (_writer == nullptr)
		{
			return nullptr;
		}

		return _writer;
	}

	void FileSession::DestroyWriter()
	{
		std::lock_guard<std::shared_mutex> lock(_writer_mutex);
		if (_writer != nullptr)
		{
			_writer->Stop();
			_writer = nullptr;
		}
	}

	std::shared_ptr<ffmpeg::Writer> FileSession::GetWriter()
	{
		std::shared_lock<std::shared_mutex> lock(_writer_mutex);
		return _writer;
	}

	bool FileSession::IsSupportCodec(const ov::String output_format, const cmn::MediaCodecId codec_id)
	{
		if (output_format == "mp4")
		{
			// For mp4 format, only a limited number of codecs are supported.
			if (codec_id == cmn::MediaCodecId::H264 ||
				codec_id == cmn::MediaCodecId::H265 ||
				codec_id == cmn::MediaCodecId::Aac ||
				codec_id == cmn::MediaCodecId::Mp3 ||
				codec_id == cmn::MediaCodecId::Opus)
			{
				return true;
			}
		}
		else if (output_format == "mpegts")
		{
			if (codec_id == cmn::MediaCodecId::H264 ||
				codec_id == cmn::MediaCodecId::H265 ||
				codec_id == cmn::MediaCodecId::Vp8 ||
				codec_id == cmn::MediaCodecId::Vp9 ||
				codec_id == cmn::MediaCodecId::Aac ||
				codec_id == cmn::MediaCodecId::Mp3 ||
				codec_id == cmn::MediaCodecId::Opus)
			{
				return true;
			}
		}
		// Webm 지원 코덱
		else if (output_format == "webm")
		{
			if (codec_id == cmn::MediaCodecId::Vp8 ||
				codec_id == cmn::MediaCodecId::Vp9 ||
				codec_id == cmn::MediaCodecId::Av1 ||
				codec_id == cmn::MediaCodecId::Opus)
			{
				return true;
			}
		}

		return false;
	}

	bool FileSession::AddTrack(const ov::String output_format, const std::shared_ptr<MediaTrack> &track)
	{
		auto writer = GetWriter();
		if (writer == nullptr)
		{
			logte("Writer is not created.");
			return false;
		}

		// Check already added track
		if (writer->GetTrackByTrackId(track->GetId()) != nullptr)
		{
			logtw("Track already added. trackId: %d, variantName: %s", track->GetId(), track->GetVariantName().CStr());
			return false;
		}

		if (IsSupportCodec(output_format, track->GetCodecId()) == false)
		{
			logtw("Could not supported codec. trackId: %u, container: %s codec: %s, variantName: %s",
				  track->GetId(), output_format.CStr(), GetCodecIdString(track->GetCodecId()), track->GetVariantName().CStr());
			return false;
		}

		logtd("Adding track to writer. trackId: %d, variantName: %s", track->GetId(), track->GetVariantName().CStr());

		SelectDefaultTrack(track);

		return writer->AddTrack(track);
	}

}  // namespace pub
