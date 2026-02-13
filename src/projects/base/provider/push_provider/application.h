//==============================================================================
//
//  PushProvider Application Base Class 
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/provider/application.h"
#include "stream.h"

namespace pvd
{
	class PushProvider;
	class PushApplication : public Application
	{
	public:
		virtual bool JoinStream(const std::shared_ptr<PushStream> &stream);

	protected:
		PushApplication(const std::shared_ptr<PushProvider> &provider, const info::Application &application_info);
		virtual bool DeleteAllStreams() override;		

	private:

	};
}