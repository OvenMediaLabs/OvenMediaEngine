//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once
#include "modules/ice/ice_candidate.h"
#include "sdp_base.h"

// It is used for session description and media description 
class CommonAttr
{
protected:
	CommonAttr();
	~CommonAttr();

	bool SerializeCommonAttr(ov::String& sdp);
	bool ParsingCommonAttrLine(char type, std::string content);

public:
	enum class SetupType
	{
		Unknown,
		Active,
		Passive,
		ActPass
	};

	// a=extmap-allow-mixed
	void SetExtmapAllowMixed(bool allow);
	bool GetExtmapAllowMixed() const;

	// a=fingerprint:sha-256 D7:81:CF:01:46:FB:2D
	void SetFingerprint(const ov::String& algorithm, const ov::String& value);
	virtual ov::String GetFingerprintAlgorithm() const;
	virtual ov::String GetFingerprintValue() const;

	// a=ice-options:trickle
	void SetIceOption(const ov::String& option);
	virtual ov::String GetIceOption() const;

	// a=ice-pwd:c32d4070c67e9782bea90a9ab46ea838
	void SetIceUfrag(const ov::String& ufrag);
	virtual ov::String GetIceUfrag() const;

	// a=ice-ufrag:0dfa46c9
	void SetIcePwd(const ov::String& pwd);
	virtual ov::String GetIcePwd() const;

	// a=control:trackID=1
	void SetControl(const ov::String& control);
	ov::String GetControl() const;

	// a=setup:actpass
	void SetSetup(SetupType type);
	SetupType GetSetup() const;
	ov::String GetSetupStr() const;
	bool SetSetup(const ov::String &type);

	// a=candidate:1387637174 1 udp 2122260223 192.0.2.1 61764 typ host generation 0 ufrag EsAw network-id 1
	std::vector<std::shared_ptr<IceCandidate>> GetIceCandidates() const;
	void AddIceCandidate(std::shared_ptr<IceCandidate> ice_candidate);

private:
	// ice
	ov::String _ice_option;
	ov::String _ice_ufrag;
	ov::String _ice_pwd;
	// dtls
	ov::String _fingerprint_algorithm;
	ov::String _fingerprint_value;

	// extmap-allow-mixed
	bool _extmap_allow_mixed = false;

	// For RTSP
	// a=control:
	ov::String _control;

	SetupType _setup = SetupType::Unknown;
	ov::String _setup_str = "UNKNOWN";

	std::vector<std::shared_ptr<IceCandidate>> _ice_candidates;
};
