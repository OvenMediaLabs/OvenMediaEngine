#pragma once
#include "base/ovlibrary/ovlibrary.h"

#define RTCP_VERSION         		2
#define RTCP_HEADER_SIZE            4
#define RTCP_MAX_BLOCK_COUNT        0x1F

// packet type
enum class RtcpPacketType : uint8_t
{
    SR = 200,   // Sender Report
    RR = 201,   // Receiver Report
    SDES = 202, // Source Description message
    BYE = 203,  // Bye message
    APP = 204,  // Application specfic RTCP
	RTPFB = 205, // Transport layer feedback message : RFC 4585
	PSFB = 206,	// Payload-specific feedback message : RFC 4585
	XR = 207,	// Extended Reports	: RFC 3611 (RTCP XR)
};

enum class RTPFBFMT : uint8_t
{
	NACK = 1,	// General Negative acknowledgements
	TRANSPORT_CC = 15, // Transport-CC (https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01)
	EXT = 31, 	// Reserved for future extensions
};

enum class PSFBFMT : uint8_t
{
	PLI = 1,	// Picture Loss Indication
	SLI = 2,	// Slice Loss Indication
	RPSI = 3,	// Reference Picture Selection Indication
	FIR = 4,	// Full Intra Request (FIR) Command
	AFB = 15,	// Application Layer Feedback
	EXT = 31,	// Reserved for future extensions.
};