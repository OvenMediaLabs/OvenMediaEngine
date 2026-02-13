//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <config/config.h>

//
// SRT Options
// (We are using SRT 1.5.1, so we do not support SRTO_CRYPTOMODE)
//
// https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md#list-of-options
//
// +--------------------------+-----------+----------+---------+---------+---------------+----------+-----+--------+
// | Option Name              | Since     | Restrict | Type    | Units   | Default       | Range    | Dir | Entity |
// +--------------------------+-----------+----------+---------+---------+---------------+----------+-----+--------+
// | SRTO_BINDTODEVICE        | 1.4.2     | pre-bind | string  |         |               |          | RW  | GSD+   |
// | SRTO_CONGESTION          | 1.3.0     | pre      | string  |         | "live"        | *        | W   | S      |
// | SRTO_CONNTIMEO           | 1.1.2     | pre      | int32_t | ms      | 3000          | 0..      | W   | GSD+   |
// | SRTO_CRYPTOMODE          | 1.6.0-dev | pre      | int32_t |         | 0 (Auto)      | [0, 2]   | W   | GSD    |
// | SRTO_DRIFTTRACER         | 1.4.2     | post     | bool    |         | true          |          | RW  | GSD    |
// | SRTO_ENFORCEDENCRYPTION  | 1.3.2     | pre      | bool    |         | true          |          | W   | GSD    |
// | SRTO_EVENT               |           |          | int32_t | flags   |               |          | R   | S      |
// | SRTO_FC                  |           | pre      | int32_t | pkts    | 25600         | 32..     | RW  | GSD    |
// | SRTO_GROUPCONNECT        | 1.5.0     | pre      | int32_t |         | 0             | 0...1    | W   | S      |
// | SRTO_GROUPMINSTABLETIMEO | 1.5.0     | pre      | int32_t | ms      | 60            | 60-...   | W   | GDI+   |
// | SRTO_GROUPTYPE           | 1.5.0     |          | int32_t | enum    |               |          | R   | S      |
// | SRTO_INPUTBW             | 1.0.5     | post     | int64_t | B/s     | 0             | 0..      | RW  | GSD    |
// | SRTO_IPTOS               | 1.0.5     | pre-bind | int32_t |         | (system)      | 0..255   | RW  | GSD    |
// | SRTO_IPTTL               | 1.0.5     | pre-bind | int32_t | hops    | (system)      | 1..255   | RW  | GSD    |
// | SRTO_IPV6ONLY            | 1.4.0     | pre-bind | int32_t |         | (system)      | -1..1    | RW  | GSD    |
// | SRTO_ISN                 | 1.3.0     |          | int32_t |         |               |          | R   | S      |
// | SRTO_KMPREANNOUNCE       | 1.3.2     | pre      | int32_t | pkts    | 0: 2^12       | 0.. *    | RW  | GSD    |
// | SRTO_KMREFRESHRATE       | 1.3.2     | pre      | int32_t | pkts    | 0: 2^24       | 0..      | RW  | GSD    |
// | SRTO_KMSTATE             | 1.0.2     |          | int32_t | enum    |               |          | R   | S      |
// | SRTO_LATENCY             | 1.0.2     | pre      | int32_t | ms      | 120 *         | 0..      | RW  | GSD    |
// | SRTO_LINGER              |           | post     | linger  | s       | off *         | 0..      | RW  | GSD    |
// | SRTO_LOSSMAXTTL          | 1.2.0     | post     | int32_t | packets | 0             | 0..      | RW  | GSD+   |
// | SRTO_MAXBW               |           | post     | int64_t | B/s     | -1            | -1..     | RW  | GSD    |
// | SRTO_MESSAGEAPI          | 1.3.0     | pre      | bool    |         | true          |          | W   | GSD    |
// | SRTO_MININPUTBW          | 1.4.3     | post     | int64_t | B/s     | 0             | 0..      | RW  | GSD    |
// | SRTO_MINVERSION          | 1.3.0     | pre      | int32_t | version | 0x010000      | *        | RW  | GSD    |
// | SRTO_MSS                 |           | pre-bind | int32_t | bytes   | 1500          | 76..     | RW  | GSD    |
// | SRTO_NAKREPORT           | 1.1.0     | pre      | bool    |         |  *            |          | RW  | GSD+   |
// | SRTO_OHEADBW             | 1.0.5     | post     | int32_t | %       | 25            | 5..100   | RW  | GSD    |
// | SRTO_PACKETFILTER        | 1.4.0     | pre      | string  |         | ""            | [512]    | RW  | GSD    |
// | SRTO_PASSPHRASE          | 0.0.0     | pre      | string  |         | ""            | [10..80] | W   | GSD    |
// | SRTO_PAYLOADSIZE         | 1.3.0     | pre      | int32_t | bytes   | *             | 0.. *    | W   | GSD    |
// | SRTO_PBKEYLEN            | 0.0.0     | pre      | int32_t | bytes   | 0             | *        | RW  | GSD    |
// | SRTO_PEERIDLETIMEO       | 1.3.3     | pre      | int32_t | ms      | 5000          | 0..      | RW  | GSD+   |
// | SRTO_PEERLATENCY         | 1.3.0     | pre      | int32_t | ms      | 0             | 0..      | RW  | GSD    |
// | SRTO_PEERVERSION         | 1.1.0     |          | int32_t | *       |               |          | R   | GS     |
// | SRTO_RCVBUF              |           | pre-bind | int32_t | bytes   | 8192 payloads | *        | RW  | GSD+   |
// | SRTO_RCVDATA             |           |          | int32_t | pkts    |               |          | R   | S      |
// | SRTO_RCVKMSTATE          | 1.2.0     |          | int32_t | enum    |               |          | R   | S      |
// | SRTO_RCVLATENCY          | 1.3.0     | pre      | int32_t | msec    | *             | 0..      | RW  | GSD    |
// | SRTO_RCVSYN              |           | post     | bool    |         | true          |          | RW  | GSI    |
// | SRTO_RCVTIMEO            |           | post     | int32_t | ms      | -1            | -1, 0..  | RW  | GSI    |
// | SRTO_RENDEZVOUS          |           | pre      | bool    |         | false         |          | RW  | S      |
// | SRTO_RETRANSMITALGO      | 1.4.2     | pre      | int32_t |         | 1             | [0, 1]   | RW  | GSD    |
// | SRTO_REUSEADDR           |           | pre-bind | bool    |         | true          |          | RW  | GSD    |
// | SRTO_SENDER              | 1.0.4     | pre      | bool    |         | false         |          | W   | S      |
// | SRTO_SNDBUF              |           | pre-bind | int32_t | bytes   | 8192 payloads | *        | RW  | GSD+   |
// | SRTO_SNDDATA             |           |          | int32_t | pkts    |               |          | R   | S      |
// | SRTO_SNDDROPDELAY        | 1.3.2     | post     | int32_t | ms      | *             | -1..     | W   | GSD+   |
// | SRTO_SNDKMSTATE          | 1.2.0     |          | int32_t | enum    |               |          | R   | S      |
// | SRTO_SNDSYN              |           | post     | bool    |         | true          |          | RW  | GSI    |
// | SRTO_SNDTIMEO            |           | post     | int32_t | ms      | -1            | -1..     | RW  | GSI    |
// | SRTO_STATE               |           |          | int32_t | enum    |               |          | R   | S      |
// | SRTO_STREAMID            | 1.3.0     | pre      | string  |         | ""            | [512]    | RW  | GSD    |
// | SRTO_TLPKTDROP           | 1.0.6     | pre      | bool    |         | *             |          | RW  | GSD    |
// | SRTO_TRANSTYPE           | 1.3.0     | pre      | int32_t | enum    | SRTT_LIVE     | *        | W   | S      |
// | SRTO_TSBPDMODE           | 0.0.0     | pre      | bool    |         | *             |          | W   | S      |
// | SRTO_UDP_RCVBUF          |           | pre-bind | int32_t | bytes   | 8192 payloads | *        | RW  | GSD+   |
// | SRTO_UDP_SNDBUF          |           | pre-bind | int32_t | bytes   | 65536         | *        | RW  | GSD+   |
// | SRTO_VERSION             | 1.1.0     |          | int32_t |         |               |          | R   | S      |
// +--------------------------+-----------+----------+---------+---------+---------------+----------+-----+--------+
class SrtOptionProcessor
{
public:
	static std::shared_ptr<ov::Error> SetOptions(
		const std::shared_ptr<ov::Socket> &sock,
		const cfg::cmn::Options &options);

protected:
	SrtOptionProcessor() = default;

	static std::shared_ptr<ov::Error> SetOption(
		const std::shared_ptr<ov::Socket> &sock,
		const ov::String &option_name, const SRT_SOCKOPT option,
		const ov::String &value,
		const int32_t &dummy);

	static std::shared_ptr<ov::Error> SetOption(
		const std::shared_ptr<ov::Socket> &sock,
		const ov::String &option_name, const SRT_SOCKOPT option,
		const ov::String &value,
		const int64_t &dummy);

	static std::shared_ptr<ov::Error> SetOption(
		const std::shared_ptr<ov::Socket> &sock,
		const ov::String &option_name, const SRT_SOCKOPT option,
		const ov::String &value,
		const bool &dummy);

	static std::shared_ptr<ov::Error> SetOption(
		const std::shared_ptr<ov::Socket> &sock,
		const ov::String &option_name, const SRT_SOCKOPT option,
		const ov::String &value,
		const linger &dummy);

	static std::shared_ptr<ov::Error> SetOption(
		const std::shared_ptr<ov::Socket> &sock,
		const ov::String &option_name, const SRT_SOCKOPT option,
		const ov::String &value,
		const ov::String &dummy);
};