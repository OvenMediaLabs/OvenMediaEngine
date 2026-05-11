//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg::vhost::app::oprf
{
	struct Track : public Item
	{
	protected:
		ov::String _name;
		int _index_hint = -1;

	public:
		CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
		CFG_DECLARE_CONST_REF_GETTER_OF(GetIndexHint, _index_hint);

	protected:
		void MakeList() override
		{
			Register("Name", &_name);
			Register<Optional>("IndexHint", &_index_hint);
		}
	};
}  // namespace cfg::vhost::app::oprf
