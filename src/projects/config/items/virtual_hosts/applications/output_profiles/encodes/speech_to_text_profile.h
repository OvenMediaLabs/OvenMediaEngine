//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				// It will not be mady by Server.xml but will be made by a transcoder stream
				struct SpeechToTextProfile
				{
				protected:
					ov::String _name;
					ov::String _engine;
					ov::String _model;

					ov::String _source_language = "auto"; // input language
					uint32_t _input_track_id; // audio track id for transcription

					bool _translation = false; // whisper only supports english translation
					uint32_t _output_track_id; // subtitle track id
					ov::String _output_track_label; // subtitle track label

				public:
					SpeechToTextProfile(const ov::String &name, const ov::String &engine, const ov::String &model, uint32_t input_track_id, uint32_t output_track_id)
						: _name(name), _engine(engine), _model(model), _input_track_id(input_track_id), _output_track_id(output_track_id)
					{
					}

					ov::String GetName() const
					{
						return _name;
					}

					ov::String GetEngine() const
					{
						return _engine;
					}

					ov::String GetModel() const
					{
						return _model;
					}

					uint32_t GetInputTrackId() const
					{
						return _input_track_id;
					}

					uint32_t GetOutputTrackId() const
					{
						return _output_track_id;
					}

					ov::String GetSourceLanguage() const
					{
						return _source_language;
					}

					bool ShouldTranslate() const
					{
						return _translation;
					}

					void SetSourceLanguage(const ov::String &source_language)
					{
						_source_language = source_language;
					}

					void SetTranslation(bool translation)
					{
						_translation = translation;
					}	

					void SetOutputTrackLabel(const ov::String &label)
					{
						_output_track_label = label;
					}
					ov::String GetOutputTrackLabel() const
					{
						return _output_track_label;
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg