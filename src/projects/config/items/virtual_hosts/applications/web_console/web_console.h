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
	namespace vhost
	{
		namespace app
		{
			namespace wc
			{
				struct WebConsole : public Item
				{
					CFG_DECLARE_CONST_REF_GETTER_OF(GetListenPort, _listen_port)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLoginId, _login_id)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLoginPw, _login_pw)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDocumentPath, _document_path)

				protected:
					void MakeList() override
					{
						Register<Optional>("ListenPort", &_listen_port);
						Register<Optional>("LoginID", &_login_id);
						Register("LoginPW", &_login_pw);
						Register("DocumentPath", &_document_path);
					}

					int _listen_port	 = 8888;
					ov::String _login_id = "admin";
					ov::String _login_pw;
					ov::String _document_path;
				};
			}  // namespace wc
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg