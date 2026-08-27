//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>

// Rebuilds an access unit with one NAL unit added or swapped out. Codec agnostic: it deals in
// fragment offsets and takes the target index from the caller, which is what knows NAL types.
class NalUnitInsertor
{
public:
	// Appends after the last NAL unit
	static constexpr size_t APPEND_TO_END = SIZE_MAX;

	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Insert(
		const std::shared_ptr<const ov::Data> src_data,
		const std::shared_ptr<ov::Data> new_nal,
		const cmn::BitstreamFormat format,
		const size_t nal_index = APPEND_TO_END);

	// Inserts new_nalu so that it ends up at nal_index, shifting the NAL units at and after that
	// index. APPEND_TO_END puts it after every existing NAL unit.
	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Insert(
		const std::shared_ptr<const ov::Data> src_nalu,
		const FragmentationHeader* src_fragment,
		const std::shared_ptr<ov::Data> new_nalu,
		const cmn::BitstreamFormat format,
		const size_t nal_index = APPEND_TO_END);

	// Replaces the NAL unit at nal_index with new_nalu. The new NAL may have a different length.
	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Replace(
		const std::shared_ptr<const ov::Data> src_nalu,
		const FragmentationHeader* src_fragment,
		const std::shared_ptr<ov::Data> new_nalu,
		const cmn::BitstreamFormat format,
		const size_t nal_index);

	// Whole NAL units, header included: inserts emulation_prevention_three_byte(0x03)
	static std::shared_ptr<ov::Data> EmulationPreventionBytes(const std::shared_ptr<ov::Data>& nal);

	// Whole NAL units, header included: removes emulation_prevention_three_byte(0x03)
	static std::shared_ptr<ov::Data> RemoveEmulationPreventionBytes(const std::shared_ptr<const ov::Data>& nal);

private:
	enum class Operation
	{
		Insert,
		Replace
	};

	static std::optional<std::tuple<std::shared_ptr<ov::Data>, std::shared_ptr<FragmentationHeader>>> Rebuild(
		const std::shared_ptr<const ov::Data> src_data,
		const FragmentationHeader* src_fragment,
		const std::shared_ptr<ov::Data> new_nal,
		const cmn::BitstreamFormat format,
		const size_t nal_index,
		const Operation operation);
};
