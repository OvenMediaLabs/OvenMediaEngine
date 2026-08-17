//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once
#include <base/ovlibrary/ovlibrary.h>
#include "rtsp_header_field.h"

class RtspHeaderWWWAuthenticateField : public RtspHeaderField
{
public:
	enum class Scheme : uint8_t
	{
		Unknown = 0,
		Basic,
		Digest
	};

	RtspHeaderWWWAuthenticateField()
		: RtspHeaderField(RtspHeaderFieldType::WWWAuthenticate)
	{

	}

	RtspHeaderWWWAuthenticateField(Scheme scheme, ov::String realm)
	{
		_scheme = scheme;
		_realm = realm;

		ov::String value = ov::String::FormatString("%s realm=\"%s\"", 
												GetSchemeString(scheme).CStr(), _realm.CStr());

		SetContent(RtspHeaderFieldType::WWWAuthenticate, value);
	}

	RtspHeaderWWWAuthenticateField(Scheme scheme, ov::String realm, ov::String nonce)
	{
		_scheme = scheme;
		_realm = realm;
		_nonce = nonce;

		ov::String value = ov::String::FormatString("%s realm=\"%s\", nonce=\"%s\"", 
												GetSchemeString(scheme).CStr(),
												_realm.CStr(), _nonce.CStr());

		SetContent(RtspHeaderFieldType::WWWAuthenticate, value);
	}

	// Get string from Scheme
	static ov::String GetSchemeString(Scheme scheme)
	{
		switch (scheme)
		{
			case Scheme::Basic:
				return "Basic";
			case Scheme::Digest:
				return "Digest";
			default:
				return "";
		}
	}

	// Get Scheme from string
	static Scheme GetScheme(const ov::String &scheme_string)
	{
		if (scheme_string == "Basic")
		{
			return Scheme::Basic;
		}
		else if (scheme_string == "Digest")
		{
			return Scheme::Digest;
		}
		else
		{
			return Scheme::Unknown;
		}
	}

	// Get scheme
	Scheme GetScheme() const
	{
		return _scheme;
	}

	// Get Realm
	ov::String GetRealm() const
	{
		return _realm;
	}

	// Get Nonce
	ov::String GetNonce() const
	{
		return _nonce;
	}

	// Parse WWW-Authenticate header field
	//
	// WWW-Authenticate = *( "," OWS ) challenge *( OWS "," [ OWS challenge] )
	// challenge = auth-scheme [ 1*SP ( token68 / [ ( "," / auth-param ) *(OWS "," [ OWS auth-param ] ) ] ) ]
	// auth-scheme = token
	// auth-param = token "=" ( token / quoted-string )
	// token68 = 1*( ALPHA / DIGIT / "-" / "." / "_" / "~" / "+" / "/" ) *( "=" 1*( ALPHA / DIGIT / "-" / "." / "_" / "~" / "+" / "/" ) )
	// Example : WWW-Authenticate: Basic [realm=<realm>] [, nonce=<nonce>]
	bool Parse(const ov::String &message) override
	{
		if(RtspHeaderField::Parse(message) == false)
		{
			return false;
		}

		auto challenge = GetValue();
		// Find first white space in the value
		auto index = challenge.IndexOf(' ');
		if(index == -1)
		{
			// there is no auth-param
			_scheme = GetScheme(challenge.Trim());
			return true;
		}

		// Get auth-scheme
		ov::String scheme_string = challenge.Substring(0, index).Trim();
		_scheme = GetScheme(scheme_string);

		// Get auth-param
		ov::String auth_param = challenge.Substring(index + 1).Trim();

		// Parsing auth-param
		auto auth_param_list = auth_param.Split(",");
		for(auto &param : auth_param_list)
		{
			auto param_list = param.Split("=");
			if(param_list.size() != 2)
			{
				// auth-param is invalid
				return false;
			}

			ov::String key = param_list[0].Trim();
			ov::String value = param_list[1].Trim();

			// Remove double quotes or quote from value
			if((value.HasPrefix("\"") && value.HasSuffix("\"")) || (value.HasPrefix("'") && value.HasSuffix("'")))
			{
				value = value.Substring(1, value.GetLength() - 2);
			}

			if(key.LowerCaseString() == "realm")
			{
				_realm = value;
			}
			else if(key.LowerCaseString() == "nonce")
			{
				_nonce = value;
			}
		}

		return true;
	}

private:
	Scheme _scheme;
	ov::String _realm;
	ov::String _nonce;
};