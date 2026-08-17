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
#include <base/ovcrypto/base_64.h>
#include <base/ovcrypto/message_digest.h>

#include "rtsp_header_field.h"
#include "rtsp_header_authenticate.h"

class RtspHeaderAuthorizationField : public RtspHeaderField
{
public:

    // Create basic authorization field
    static std::shared_ptr<RtspHeaderAuthorizationField> CreateRtspBasicAuthorizationField(const ov::String &username, const ov::String &password)
    {
        auto field = std::make_shared<RtspHeaderAuthorizationField>();
        field->SetBasicAuth(username, password);
        return field;
    }

    // Create digest authorization field
    static std::shared_ptr<RtspHeaderAuthorizationField> CreateRtspDigestAuthorizationField(const ov::String &username, const ov::String &password, const ov::String &method, const ov::String &uri, const ov::String &realm, const ov::String &nonce)
    {
        auto field = std::make_shared<RtspHeaderAuthorizationField>();
        field->SetDigestAuth(username, password, method, realm, uri, nonce);
        return field;
    }

    RtspHeaderAuthorizationField()
        : RtspHeaderField(RtspHeaderFieldType::Authorization)
    {
    }

    // Set username and password for basic auth
    void SetBasicAuth(ov::String username, ov::String password)
    {
        _scheme = RtspHeaderWWWAuthenticateField::Scheme::Basic;
        _username = username;
        _password = password;

        // Generate basic auth value from username and password
        _response = ov::Base64::Encode(ov::String::FormatString("%s:%s", _username.CStr(), _password.CStr()).ToData(false));
        ov::String value = ov::String::FormatString("%s %s",
                                                    RtspHeaderWWWAuthenticateField::GetSchemeString(_scheme).CStr(),
                                                    _response.CStr());

        SetContent(RtspHeaderFieldType::Authorization, value);
    }

    void UpdateDigestAuth(ov::String method, ov::String uri)
    {
        SetDigestAuth(_username, _password, method, _realm, uri, _nonce);
    }

    // Set username, password, method, realm, uri and nonce for digest auth
    void SetDigestAuth(ov::String username, ov::String password, ov::String method, ov::String realm, ov::String uri, ov::String nonce)
    {
        _scheme = RtspHeaderWWWAuthenticateField::Scheme::Digest;
        _username = username;
        _password = password;
        _method = method;
        _realm = realm;
        _uri = uri;
        _nonce = nonce;

        // MD5 hash of username:realm:password
        auto ha1 = ov::String::FormatString("%s:%s:%s", _username.CStr(), _realm.CStr(), _password.CStr());
        auto ha1_md5 = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Md5, ha1.ToData(false));
        
        // MD5 hash of method:uri
        auto ha2 = ov::String::FormatString("%s:%s", _method.CStr(), _uri.CStr());
        auto ha2_md5 = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Md5, ha2.ToData(false));

        // MD5 hash of ha1:nonce:ha2
        auto response = ov::String::FormatString("%s:%s:%s", ha1_md5->ToHexString().LowerCaseString().CStr(), _nonce.CStr(), ha2_md5->ToHexString().LowerCaseString().CStr());
        _response = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Md5, response.ToData(false))->ToHexString().LowerCaseString();

        // Generate digest auth value from username, password, method, realm, uri, nonce and response
        ov::String value = ov::String::FormatString("%s username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
                                                    RtspHeaderWWWAuthenticateField::GetSchemeString(_scheme).CStr(),
                                                    _username.CStr(),
                                                    _realm.CStr(),
                                                    _nonce.CStr(),
                                                    _uri.CStr(),
                                                    _response.CStr());

        SetContent(RtspHeaderFieldType::Authorization, value);
    }

    // Get scheme
    RtspHeaderWWWAuthenticateField::Scheme GetScheme() const
    {
        return _scheme;
    }

    // Get username
    ov::String GetUsername() const
    {
        return _username;
    }

    // Get password
    ov::String GetPassword() const
    {
        return _password;
    }

    // Get method
    ov::String GetMethod() const
    {
        return _method;
    }

    // Get realm
    ov::String GetRealm() const
    {
        return _realm;
    }

    // Get uri
    ov::String GetUri() const
    {
        return _uri;
    }

    // Get nonce
    ov::String GetNonce() const
    {
        return _nonce;
    }

    // Get response
    ov::String GetResponse() const
    {
        return _response;
    }

    bool Parse(const ov::String &message) override
	{
        return false;
    }

    // Doesn't need to implement serialize method, because SetContent() already serialize it.

private:
    RtspHeaderWWWAuthenticateField::Scheme  _scheme;
    // For basic auth
    ov::String _username;
    ov::String _password;
    // For digest auth
    ov::String _method;
    ov::String _realm;
    ov::String _nonce;
    ov::String _uri;

    ov::String _response;
};