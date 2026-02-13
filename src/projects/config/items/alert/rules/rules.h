//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "egress/egress.h"
#include "ingress/ingress.h"

namespace cfg
{
	namespace alrt
	{
		namespace rule
		{
			struct Rules : public Item
			{
			protected:
				Ingress _ingress;
				Egress _egress;

				bool _internal_queue_congestion = false;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetIngress, _ingress)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetEgress, _egress)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsInternalQueueCongestion, _internal_queue_congestion)

			protected:
				void MakeList() override
				{
					Register<Optional>("Ingress", &_ingress);
					Register<Optional>("Egress", &_egress);
					Register<Optional>("InternalQueueCongestion", &_internal_queue_congestion, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_internal_queue_congestion = true;
						return nullptr;
					});
				}
			};
		}  // namespace rule
	}  // namespace alrt
}  // namespace cfg
