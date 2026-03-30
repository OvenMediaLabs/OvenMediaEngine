//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
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
				struct SttRendition : public Item
				{
				protected:
					// STT engine to use (e.g. "whisper")
					ov::String _engine;
					// Label of the subtitle rendition (must match a <Subtitle><Rendition><Label>)
					ov::String _output_subtitle_label;
					// Absolute or config-relative path to the model file
					ov::String _model;
					// Index of the input audio track to transcribe (0 = first audio track)
					int _input_audio_index = 0;
					// Input language for transcription ("auto" enables auto-detection)
					ov::String _source_language = "auto";
					// Translate output to English (whisper only)
					bool _translation = false;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEngine, _engine)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputSubtitleLabel, _output_subtitle_label)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModel, _model)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetInputAudioIndex, _input_audio_index)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSourceLanguage, _source_language)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTranslation, _translation)

				protected:
					void MakeList() override
					{
						Register("Engine", &_engine);
						Register("OutputSubtitleLabel", &_output_subtitle_label);
						Register("Model", &_model);
						Register<Optional>("InputAudioIndex", &_input_audio_index);
						Register<Optional>("SourceLanguage", &_source_language);
						Register<Optional>("Translation", &_translation);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
