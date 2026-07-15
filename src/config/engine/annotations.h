//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <type_traits>

namespace cfg
{
	// #region ========== Annotations ==========

	// [Optional]
	// - The child item is optional. If there's no value, it's just skip.
	enum class Optional
	{
		Optional,
		NotOptional
	};

	// [ResolvePath]
	// - Determine whether to obtain a relative/absolute path from the location of the file where the configuration was declared.
	// - If this annotation is used, its child value is automatically converted to a relative/absolute path.
	enum class ResolvePath
	{
		Resolve,
		DontResolve
	};

	// [OmitJsonName]
	// - Indicates whether to omit the name when the child item is array
	// - For example,
	//   - XML Source
	//     <IceCandidates>
	//       <IceCandidate>1.1.1.1/udp</IceCandidate>
	//       <IceCandidate>1.2.2.2/udp</IceCandidate>
	//     </IceCandidates>
	//   - Omit
	//     {
	//         "iceCandidates": [
	//             "1.1.1.1/udp",
	//             "1.2.2.2/udp"
	//         ]
	//     }
	//   - Don't omit
	//     {
	//         "iceCandidates": [
	//             { "iceCandidate": "1.1.1.1/udp" },
	//             { "iceCandidate": "1.2.2.2/udp" }
	//         ]
	//     }
	// OmitName (from OmitJsonName annotation)
	enum class OmitJsonName
	{
		Omit,
		DontOmit
	};

	// #endregion

	// #region ========== Annotation Utilities ==========
	template <typename... Args>
	struct CheckAnnotations;

	template <typename Texpected, typename Tfirst_type, typename... Ttypes>
	struct CheckAnnotations<Texpected, Tfirst_type, Ttypes...>
	{
		static constexpr bool value = std::is_same<Texpected, Tfirst_type>::value || CheckAnnotations<Texpected, Ttypes...>::value;
	};

	template <typename Texpected>
	struct CheckAnnotations<Texpected>
	{
		static constexpr bool value = false;
	};
	// #endregion
}  // namespace cfg
