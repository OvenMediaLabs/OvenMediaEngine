#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/ovlibrary.h>

class NalStreamConverter 
{
public:
	static std::shared_ptr<ov::Data> ConvertXvccToAnnexb(const std::shared_ptr<const ov::Data> &data);
	static std::shared_ptr<ov::Data> ConvertAnnexbToXvcc(const std::shared_ptr<const ov::Data> &data);
	static std::shared_ptr<ov::Data> ConvertAnnexbToXvcc(const std::shared_ptr<const ov::Data> &data, const FragmentationHeader *frag_header);
};