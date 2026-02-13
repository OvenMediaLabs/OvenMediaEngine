//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace bind
	{
		namespace pub
		{
			template <typename Tport>
			struct Publisher : public Item
			{
			protected:
				Tport _port;
				Tport _tls_port;

				int _worker_count{};

			public:
				explicit Publisher(const char *port)
					: _port(port)
				{
				}

				Publisher(const char *port, const char *tls_port)
					: _port(port),
					  _tls_port(port)
				{
				}

				CFG_DECLARE_CONST_REF_GETTER_OF(GetPort, _port);
				CFG_DECLARE_CONST_REF_GETTER_OF(GetTlsPort, _tls_port);

				CFG_DECLARE_CONST_REF_GETTER_OF(GetWorkerCount, _worker_count);

			protected:
				void MakeList() override
				{
					Register<Optional>("Port", &_port);
					Register<Optional>({"TLSPort", "tlsPort"}, &_tls_port);

					Register<Optional>("WorkerCount", &_worker_count);
				};
			};
		}  // namespace pub
	}  // namespace bind
}  // namespace cfg