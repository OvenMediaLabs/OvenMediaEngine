//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace avif
{
	// Wraps one AV1 coded still into a one-image AVIF file.
	//
	// AVIF (ISO/IEC 23000-22) is an AV1 still inside a HEIF/MIAF container, so this only
	// writes boxes around the bitstream - nothing is re-encoded. Every value the boxes need
	// comes from the still's own in-band sequence header, so the container cannot disagree
	// with what was encoded.
	class Packager
	{
	public:
		// `av1_temporal_unit` is one AV1 temporal unit (raw OBUs) coding a key frame plus its
		// sequence header - what an allintra libaom encoder emits per frame. Returns nullptr
		// if it is not a self-contained still.
		static std::shared_ptr<ov::Data> Pack(const std::shared_ptr<const ov::Data> &av1_temporal_unit);
	};
}  // namespace avif
