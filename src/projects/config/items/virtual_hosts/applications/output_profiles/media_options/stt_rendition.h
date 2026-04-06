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
					// Whisper sliding-window tuning parameters.
					// StepMs: how many ms of new audio to collect before running inference (default 2000).
					// LengthMs: total audio window passed to Whisper per inference call (default 10000).
					// KeepMs: audio overlap carried over from the previous window after a context reset (default 1500).
					int32_t _step_ms = 2000;
					int32_t _length_ms = 10000;
					int32_t _keep_ms = 1500;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEngine, _engine)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputSubtitleLabel, _output_subtitle_label)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModel, _model)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetInputAudioIndex, _input_audio_index)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSourceLanguage, _source_language)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTranslation, _translation)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetStepMs, _step_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLengthMs, _length_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetKeepMs, _keep_ms)

				protected:
					void MakeList() override
					{
						Register("Engine", &_engine);
						Register("OutputSubtitleLabel", &_output_subtitle_label);
						Register("Model", &_model);
						Register<Optional>("InputAudioIndex", &_input_audio_index);
						Register<Optional>("SourceLanguage", &_source_language);
						Register<Optional>("Translation", &_translation);
						Register<Optional>("StepMs", &_step_ms);
						Register<Optional>("LengthMs", &_length_ms);
						Register<Optional>("KeepMs", &_keep_ms);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
