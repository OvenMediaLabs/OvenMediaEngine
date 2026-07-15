//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <zlib.h>
#include "ovlibrary.h"

namespace ov
{
	class Zip
	{
	public:
		static std::shared_ptr<ov::Data> CompressGzip(const std::shared_ptr<ov::Data> &input)
		{
			auto output = std::make_shared<ov::Data>(input->GetLength());
			output->SetLength(input->GetLength());

			z_stream zs;
			zs.zalloc = Z_NULL;
			zs.zfree = Z_NULL;
			zs.opaque = Z_NULL;
			zs.avail_in = (uInt)input->GetLength();
			zs.next_in = (Bytef *)input->GetDataAs<Bytef>();
			zs.avail_out = (uInt)output->GetLength();
			zs.next_out = (Bytef *)output->GetWritableDataAs<Bytef>();;

			deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
			deflate(&zs, Z_FINISH);
			deflateEnd(&zs);

			output->SetLength(zs.total_out);
			return output;
		}

		static std::shared_ptr<ov::Data> DecompressGzip(const std::shared_ptr<ov::Data> &input)
		{
			return nullptr;
		}

	private:

	};
}