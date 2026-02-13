#pragma once

#include <base/common_types.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/h264/h264_common.h>

class NalUnitInsertor
{
public:
	
	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Insert(
		const std::shared_ptr<const ov::Data> src_data,
		const std::shared_ptr<ov::Data> new_nal,
		const cmn::BitstreamFormat format);

	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Insert(
		const std::shared_ptr<const ov::Data> src_nalu,
		const FragmentationHeader* src_fragment,
		const std::shared_ptr<ov::Data> new_nalu,
		const cmn::BitstreamFormat format = cmn::BitstreamFormat::H264_ANNEXB);

	static std::shared_ptr<ov::Data> EmulationPreventionBytes(const std::shared_ptr<ov::Data>& nal);
};
