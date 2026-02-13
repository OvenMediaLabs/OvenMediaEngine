#include "opus_parser.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/log.h>
#include <base/ovlibrary/ovlibrary.h>

#include <memory>

#define OV_LOG_TAG "OPUSParser"

bool OPUSParser::IsValid(const uint8_t *data, size_t data_length)
{
	if (data == nullptr || data_length < 1)
	{
		return false;
	}

	return true;
}

/*
 Reference : https://tools.ietf.org/html/rfc6716#section-2.1

3.1.  The TOC Byte

   A well-formed Opus packet MUST contain at least one byte [R1].  This
   byte forms a table-of-contents (TOC) header that signals which of the
   various modes and configurations a given packet uses.  It is composed
   of a configuration number, "config", a stereo flag, "s", and a frame
   count code, "c", arranged as illustrated in Figure 1.  A description
   of each of these fields follows.

                              0
                              0 1 2 3 4 5 6 7
                             +-+-+-+-+-+-+-+-+
                             | config  |s| c |
                             +-+-+-+-+-+-+-+-+

                          Figure 1: The TOC Byte

   The top five bits of the TOC byte, labeled "config", encode one of 32
   possible configurations of operating mode, audio bandwidth, and frame
   size.  As described, the LP (SILK) layer and MDCT (CELT) layer can be
   combined in three possible operating modes:

   1.  A SILK-only mode for use in low bitrate connections with an audio
       bandwidth of WB or less,

   2.  A Hybrid (SILK+CELT) mode for SWB or FB speech at medium
       bitrates, and

   3.  A CELT-only mode for very low delay speech transmission as well
       as music transmission (NB to FB).

   The 32 possible configurations each identify which one of these
   operating modes the packet uses, as well as the audio bandwidth and
   the frame size.  Table 2 lists the parameters for each configuration.

   +-----------------------+-----------+-----------+-------------------+
   | Configuration         | Mode      | Bandwidth | Frame Sizes       |
   | Number(s)             |           |           |                   |
   +-----------------------+-----------+-----------+-------------------+
   | 0...3                 | SILK-only | NB        | 10, 20, 40, 60 ms |
   |                       |           |           |                   |
   | 4...7                 | SILK-only | MB        | 10, 20, 40, 60 ms |
   |                       |           |           |                   |
   | 8...11                | SILK-only | WB        | 10, 20, 40, 60 ms |
   |                       |           |           |                   |
   | 12...13               | Hybrid    | SWB       | 10, 20 ms         |
   |                       |           |           |                   |
   | 14...15               | Hybrid    | FB        | 10, 20 ms         |
   |                       |           |           |                   |
   | 16...19               | CELT-only | NB        | 2.5, 5, 10, 20 ms |
   |                       |           |           |                   |
   | 20...23               | CELT-only | WB        | 2.5, 5, 10, 20 ms |
   |                       |           |           |                   |
   | 24...27               | CELT-only | SWB       | 2.5, 5, 10, 20 ms |
   |                       |           |           |                   |
   | 28...31               | CELT-only | FB        | 2.5, 5, 10, 20 ms |
   +-----------------------+-----------+-----------+-------------------+

                Table 2: TOC Byte Configuration Parameters

   The configuration numbers in each range (e.g., 0...3 for NB SILK-
   only) correspond to the various choices of frame size, in the same
   order.  For example, configuration 0 has a 10 ms frame size and
   configuration 3 has a 60 ms frame size.

   One additional bit, labeled "s", signals mono vs. stereo, with 0
   indicating mono and 1 indicating stereo.

   The remaining two bits of the TOC byte, labeled "c", code the number
   of frames per packet (codes 0 to 3) as follows:

   o  0: 1 frame in the packet

   o  1: 2 frames in the packet, each with equal compressed size

   o  2: 2 frames in the packet, with different compressed sizes

   o  3: an arbitrary number of frames in the packet

   This document refers to a packet as a code 0 packet, code 1 packet,
   etc., based on the value of "c".
*/

bool OPUSParser::Parse(const uint8_t *data, size_t data_length, OPUSParser &parser)
{
	BitReader reader(data, data_length);

	uint8_t toc = reader.ReadBits<uint8_t>(8);

	parser._configuration_number = (toc >> 3) & 0x1F;
	parser._stereo_flag = (toc >> 2) & 0x01;
	parser._frame_count_code = (toc >> 0) & 0x01;

	logtt("configuration_number: %d, streo_flag: %d, frame_count_code: %d", parser._configuration_number, parser._stereo_flag, parser._frame_count_code);

	return true;
}

uint8_t OPUSParser::GetStereoFlag()
{
	return _stereo_flag;
}

ov::String OPUSParser::GetInfoString()
{
	ov::String out_str = ov::String::FormatString("\n[OPUSParser]\n");

	// out_str.AppendFormat("\tId(%d)\n", Id());
	// out_str.AppendFormat("\tLayer(%d)\n", Layer());
	// out_str.AppendFormat("\tProtectionAbsent(%s)\n", ProtectionAbsent()?"true":"false");
	// out_str.AppendFormat("\tProfile(%d/%s)\n", Profile(), ProfileString().CStr());
	// out_str.AppendFormat("\tSamplerate(%d/%d)\n", Samplerate(), SamplerateNum());
	// out_str.AppendFormat("\tChannelConfiguration(%d)\n", ChannelConfiguration());
	// out_str.AppendFormat("\tHome(%s)\n", Home()?"true":"false");
	// out_str.AppendFormat("\tAacFrameLength(%d)\n", AacFrameLength());

	return out_str;
}
