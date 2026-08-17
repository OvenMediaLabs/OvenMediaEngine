//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace cmn
	{
		struct SrtStream : public Item
		{
		protected:
			int _port;
			ov::String _stream_path;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetPort, _port)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetStreamPath, _stream_path)

		protected:
			void MakeList() override
			{
				Register("Port", &_port);
				Register("StreamPath", &_stream_path);
			}
		};
	}  // namespace cmn
}  // namespace cfg
