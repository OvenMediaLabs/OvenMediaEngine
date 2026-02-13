//=============================================================================
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
				struct Transcription : public Item
				{
				protected:
					ov::String _engine = "whisper"; // whisper
					ov::String _model;    // model path, whisper/gml-small.bin
					int _audio_index_hint = 0;  // default : 0

					ov::String _source_language = "auto"; // input language
					bool _translation = false; // whisper only supports english translation

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetEngine, _engine)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModel, _model)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioIndexHint, _audio_index_hint)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSourceLanguage, _source_language)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTranslation, _translation)

				protected:
					void MakeList() override
					{
						Register("Engine", &_engine);
						Register("Model", &_model);
						Register<Optional>("AudioIndexHint", &_audio_index_hint);
						Register<Optional>("SourceLanguage", &_source_language);
						Register<Optional>("Translation", &_translation);

					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg