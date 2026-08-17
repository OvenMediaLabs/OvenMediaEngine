#pragma once

#define OV_LOG_TAG "MPEG-2 TS"

// Just one program supported 
constexpr uint16_t PROGRAM_NUMBER = 1;
constexpr uint16_t PMT_PID = 0x100; // 256
constexpr uint16_t MIN_ELEMENTARY_PID = 0x101; // 257
constexpr uint64_t MPEGTS_MAX_PES_PACKET_SIZE = 0xFFFFFF; // 16,777,215, 16MB