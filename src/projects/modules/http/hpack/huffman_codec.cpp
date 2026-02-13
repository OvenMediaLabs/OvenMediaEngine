//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include <base/ovlibrary/bit_reader.h>
#include "huffman_codec.h"

namespace http
{
	namespace hpack
	{
		HuffmanCodec::HuffmanCodec()
		{
			// https://www.rfc-editor.org/rfc/rfc7541.html#appendix-B
			Build(0x1ff8, 13, 0);
			Build(0x7fffd8, 23, 1);
			Build(0xfffffe2, 28, 2);
			Build(0xfffffe3, 28, 3);
			Build(0xfffffe4, 28, 4);
			Build(0xfffffe5, 28, 5);
			Build(0xfffffe6, 28, 6);
			Build(0xfffffe7, 28, 7);
			Build(0xfffffe8, 28, 8);
			Build(0xffffea, 24, 9);
			Build(0x3ffffffc, 30, 10);
			Build(0xfffffe9, 28, 11);
			Build(0xfffffea, 28, 12);
			Build(0x3ffffffd, 30, 13);
			Build(0xfffffeb, 28, 14);
			Build(0xfffffec, 28, 15);
			Build(0xfffffed, 28, 16);
			Build(0xfffffee, 28, 17);
			Build(0xfffffef, 28, 18);
			Build(0xffffff0, 28, 19);
			Build(0xffffff1, 28, 20);
			Build(0xffffff2, 28, 21);
			Build(0x3ffffffe, 30, 22);
			Build(0xffffff3, 28, 23);
			Build(0xffffff4, 28, 24);
			Build(0xffffff5, 28, 25);
			Build(0xffffff6, 28, 26);
			Build(0xffffff7, 28, 27);
			Build(0xffffff8, 28, 28);
			Build(0xffffff9, 28, 29);
			Build(0xffffffa, 28, 30);
			Build(0xffffffb, 28, 31);
			Build(0x14, 6, 32);
			Build(0x3f8, 10, 33);
			Build(0x3f9, 10, 34);
			Build(0xffa, 12, 35);
			Build(0x1ff9, 13, 36);
			Build(0x15, 6, 37);
			Build(0xf8, 8, 38);
			Build(0x7fa, 11, 39);
			Build(0x3fa, 10, 40);
			Build(0x3fb, 10, 41);
			Build(0xf9, 8, 42);
			Build(0x7fb, 11, 43);
			Build(0xfa, 8, 44);
			Build(0x16, 6, 45);
			Build(0x17, 6, 46);
			Build(0x18, 6, 47);
			Build(0x0, 5, 48);
			Build(0x1, 5, 49);
			Build(0x2, 5, 50);
			Build(0x19, 6, 51);
			Build(0x1a, 6, 52);
			Build(0x1b, 6, 53);
			Build(0x1c, 6, 54);
			Build(0x1d, 6, 55);
			Build(0x1e, 6, 56);
			Build(0x1f, 6, 57);
			Build(0x5c, 7, 58);
			Build(0xfb, 8, 59);
			Build(0x7ffc, 15, 60);
			Build(0x20, 6, 61);
			Build(0xffb, 12, 62);
			Build(0x3fc, 10, 63);
			Build(0x1ffa, 13, 64);
			Build(0x21, 6, 65);
			Build(0x5d, 7, 66);
			Build(0x5e, 7, 67);
			Build(0x5f, 7, 68);
			Build(0x60, 7, 69);
			Build(0x61, 7, 70);
			Build(0x62, 7, 71);
			Build(0x63, 7, 72);
			Build(0x64, 7, 73);
			Build(0x65, 7, 74);
			Build(0x66, 7, 75);
			Build(0x67, 7, 76);
			Build(0x68, 7, 77);
			Build(0x69, 7, 78);
			Build(0x6a, 7, 79);
			Build(0x6b, 7, 80);
			Build(0x6c, 7, 81);
			Build(0x6d, 7, 82);
			Build(0x6e, 7, 83);
			Build(0x6f, 7, 84);
			Build(0x70, 7, 85);
			Build(0x71, 7, 86);
			Build(0x72, 7, 87);
			Build(0xfc, 8, 88);
			Build(0x73, 7, 89);
			Build(0xfd, 8, 90);
			Build(0x1ffb, 13, 91);
			Build(0x7fff0, 19, 92);
			Build(0x1ffc, 13, 93);
			Build(0x3ffc, 14, 94);
			Build(0x22, 6, 95);
			Build(0x7ffd, 15, 96);
			Build(0x3, 5, 97);
			Build(0x23, 6, 98);
			Build(0x4, 5, 99);
			Build(0x24, 6, 100);
			Build(0x5, 5, 101);
			Build(0x25, 6, 102);
			Build(0x26, 6, 103);
			Build(0x27, 6, 104);
			Build(0x6, 5, 105);
			Build(0x74, 7, 106);
			Build(0x75, 7, 107);
			Build(0x28, 6, 108);
			Build(0x29, 6, 109);
			Build(0x2a, 6, 110);
			Build(0x7, 5, 111);
			Build(0x2b, 6, 112);
			Build(0x76, 7, 113);
			Build(0x2c, 6, 114);
			Build(0x8, 5, 115);
			Build(0x9, 5, 116);
			Build(0x2d, 6, 117);
			Build(0x77, 7, 118);
			Build(0x78, 7, 119);
			Build(0x79, 7, 120);
			Build(0x7a, 7, 121);
			Build(0x7b, 7, 122);
			Build(0x7ffe, 15, 123);
			Build(0x7fc, 11, 124);
			Build(0x3ffd, 14, 125);
			Build(0x1ffd, 13, 126);
			Build(0xffffffc, 28, 127);
			Build(0xfffe6, 20, 128);
			Build(0x3fffd2, 22, 129);
			Build(0xfffe7, 20, 130);
			Build(0xfffe8, 20, 131);
			Build(0x3fffd3, 22, 132);
			Build(0x3fffd4, 22, 133);
			Build(0x3fffd5, 22, 134);
			Build(0x7fffd9, 23, 135);
			Build(0x3fffd6, 22, 136);
			Build(0x7fffda, 23, 137);
			Build(0x7fffdb, 23, 138);
			Build(0x7fffdc, 23, 139);
			Build(0x7fffdd, 23, 140);
			Build(0x7fffde, 23, 141);
			Build(0xffffeb, 24, 142);
			Build(0x7fffdf, 23, 143);
			Build(0xffffec, 24, 144);
			Build(0xffffed, 24, 145);
			Build(0x3fffd7, 22, 146);
			Build(0x7fffe0, 23, 147);
			Build(0xffffee, 24, 148);
			Build(0x7fffe1, 23, 149);
			Build(0x7fffe2, 23, 150);
			Build(0x7fffe3, 23, 151);
			Build(0x7fffe4, 23, 152);
			Build(0x1fffdc, 21, 153);
			Build(0x3fffd8, 22, 154);
			Build(0x7fffe5, 23, 155);
			Build(0x3fffd9, 22, 156);
			Build(0x7fffe6, 23, 157);
			Build(0x7fffe7, 23, 158);
			Build(0xffffef, 24, 159);
			Build(0x3fffda, 22, 160);
			Build(0x1fffdd, 21, 161);
			Build(0xfffe9, 20, 162);
			Build(0x3fffdb, 22, 163);
			Build(0x3fffdc, 22, 164);
			Build(0x7fffe8, 23, 165);
			Build(0x7fffe9, 23, 166);
			Build(0x1fffde, 21, 167);
			Build(0x7fffea, 23, 168);
			Build(0x3fffdd, 22, 169);
			Build(0x3fffde, 22, 170);
			Build(0xfffff0, 24, 171);
			Build(0x1fffdf, 21, 172);
			Build(0x3fffdf, 22, 173);
			Build(0x7fffeb, 23, 174);
			Build(0x7fffec, 23, 175);
			Build(0x1fffe0, 21, 176);
			Build(0x1fffe1, 21, 177);
			Build(0x3fffe0, 22, 178);
			Build(0x1fffe2, 21, 179);
			Build(0x7fffed, 23, 180);
			Build(0x3fffe1, 22, 181);
			Build(0x7fffee, 23, 182);
			Build(0x7fffef, 23, 183);
			Build(0xfffea, 20, 184);
			Build(0x3fffe2, 22, 185);
			Build(0x3fffe3, 22, 186);
			Build(0x3fffe4, 22, 187);
			Build(0x7ffff0, 23, 188);
			Build(0x3fffe5, 22, 189);
			Build(0x3fffe6, 22, 190);
			Build(0x7ffff1, 23, 191);
			Build(0x3ffffe0, 26, 192);
			Build(0x3ffffe1, 26, 193);
			Build(0xfffeb, 20, 194);
			Build(0x7fff1, 19, 195);
			Build(0x3fffe7, 22, 196);
			Build(0x7ffff2, 23, 197);
			Build(0x3fffe8, 22, 198);
			Build(0x1ffffec, 25, 199);
			Build(0x3ffffe2, 26, 200);
			Build(0x3ffffe3, 26, 201);
			Build(0x3ffffe4, 26, 202);
			Build(0x7ffffde, 27, 203);
			Build(0x7ffffdf, 27, 204);
			Build(0x3ffffe5, 26, 205);
			Build(0xfffff1, 24, 206);
			Build(0x1ffffed, 25, 207);
			Build(0x7fff2, 19, 208);
			Build(0x1fffe3, 21, 209);
			Build(0x3ffffe6, 26, 210);
			Build(0x7ffffe0, 27, 211);
			Build(0x7ffffe1, 27, 212);
			Build(0x3ffffe7, 26, 213);
			Build(0x7ffffe2, 27, 214);
			Build(0xfffff2, 24, 215);
			Build(0x1fffe4, 21, 216);
			Build(0x1fffe5, 21, 217);
			Build(0x3ffffe8, 26, 218);
			Build(0x3ffffe9, 26, 219);
			Build(0xffffffd, 28, 220);
			Build(0x7ffffe3, 27, 221);
			Build(0x7ffffe4, 27, 222);
			Build(0x7ffffe5, 27, 223);
			Build(0xfffec, 20, 224);
			Build(0xfffff3, 24, 225);
			Build(0xfffed, 20, 226);
			Build(0x1fffe6, 21, 227);
			Build(0x3fffe9, 22, 228);
			Build(0x1fffe7, 21, 229);
			Build(0x1fffe8, 21, 230);
			Build(0x7ffff3, 23, 231);
			Build(0x3fffea, 22, 232);
			Build(0x3fffeb, 22, 233);
			Build(0x1ffffee, 25, 234);
			Build(0x1ffffef, 25, 235);
			Build(0xfffff4, 24, 236);
			Build(0xfffff5, 24, 237);
			Build(0x3ffffea, 26, 238);
			Build(0x7ffff4, 23, 239);
			Build(0x3ffffeb, 26, 240);
			Build(0x7ffffe6, 27, 241);
			Build(0x3ffffec, 26, 242);
			Build(0x3ffffed, 26, 243);
			Build(0x7ffffe7, 27, 244);
			Build(0x7ffffe8, 27, 245);
			Build(0x7ffffe9, 27, 246);
			Build(0x7ffffea, 27, 247);
			Build(0x7ffffeb, 27, 248);
			Build(0xffffffe, 28, 249);
			Build(0x7ffffec, 27, 250);
			Build(0x7ffffed, 27, 251);
			Build(0x7ffffee, 27, 252);
			Build(0x7ffffef, 27, 253);
			Build(0x7fffff0, 27, 254);
			Build(0x3ffffee, 26, 255);
			Build(0x3fffffff, 30, 256); //EOS
		}

		std::shared_ptr<ov::Data> HuffmanCodec::Encode(const ov::String &str)
		{
			uint8_t out_data[str.GetLength() * 2];
			size_t out_data_size = 0;

			uint64_t bit_buffer = 0;
			size_t bit_buffer_length = 0;
			size_t length = str.GetLength();
			for (size_t i = 0; i < length; i++)
			{
				uint8_t c = str[i];
				auto [code, code_bit_length] = _map[c];
				
				// Append the code to the bit buffer
				bit_buffer <<= code_bit_length;
				bit_buffer |= code;
				bit_buffer_length += code_bit_length;

				// If the bit buffer is over 8 bits, flush it to the output buffer
				while (bit_buffer_length >= 8)
				{
					uint8_t byte = static_cast<uint8_t>(bit_buffer >> (bit_buffer_length - 8));
					bit_buffer_length -= 8;
					out_data[out_data_size] = byte;
					out_data_size ++;
				}
			}
			
			// https://www.rfc-editor.org/rfc/rfc7541.html#section-5.2
			// As the Huffman-encoded data doesn't always end at an octet boundary,
			// some padding is inserted after it, up to the next octet boundary.  To
			// prevent this padding from being misinterpreted as part of the string
			// literal, the most significant bits of the code corresponding to the
			// EOS (end-of-string) symbol are used.

			// Append EOS
			if (bit_buffer_length > 0)
			{
				auto byte = bit_buffer << (8 - bit_buffer_length);
				// Append EOS(0xFF) to the end of the bit buffer
				// bit_buffer is always less than 8 bits
				byte |= 0xFF >> bit_buffer_length;

				out_data[out_data_size] = byte;
				out_data_size ++;
			}
			
			return std::make_shared<ov::Data>(&out_data[0], out_data_size);
		}

		bool HuffmanCodec::Decode(const std::shared_ptr<const ov::Data> &data, ov::String &str)
		{
			auto reader = std::make_shared<BitReader>(data->GetDataAs<uint8_t>(), data->GetLength());
			auto node = _tree;

			while (reader->BytesRemained() > 0)
			{
				auto b = reader->ReadBit();
				if (b == 1)
				{
					node = node->GetRight();
					if (node == nullptr)
					{
						return false;
					}
				}
				else
				{
					node = node->GetLeft();
					if (node == nullptr)
					{
						return false;
					}
				}

				if (node->IsLeaf())
				{
					if (node->GetValue() == 256)
					{
						// EOS
						return false;
					}

					str.Append(static_cast<uint8_t>(node->GetValue()));
					node = _tree;

					// https://www.rfc-editor.org/rfc/rfc7541.html#section-5.2
					// As the Huffman-encoded data doesn't always end at an octet boundary,
					// some padding is inserted after it, up to the next octet boundary.  To
					// prevent this padding from being misinterpreted as part of the string
					// literal, the most significant bits of the code corresponding to the
					// EOS (end-of-string) symbol are used.

					// So, the symbol (1111111) corresponding to EOS may be included in the 
					// last 7 bits or less. In this case, no processing is done because it will 
					// terminate without reaching the leaf naturally.
				}
			}

			return true;
		}

		void HuffmanCodec::BuildTree(uint32_t code, uint8_t length, uint16_t symbol)
		{
			//TODO(Getroot): If needed, apply faster algorithm
			auto node = _tree;

			for (uint16_t i = 0; i < length; i++)
			{
				auto bit = (code >> (length - i - 1)) & 0x1;

				if (bit == 0)
				{
					node = node->GetLeft(true);
				}
				else
				{
					node = node->GetRight(true);
				}
			}

			node->SetValue(symbol);
		}

		void HuffmanCodec::BuildMap(uint32_t code, uint8_t length, uint16_t symbol)
		{
			_map[symbol] = std::make_pair(code, length);
		}

		// Build Tree and Map
		void HuffmanCodec::Build(uint32_t code, uint8_t length, uint16_t symbol)
		{
			BuildMap(code, length, symbol);
			BuildTree(code, length, symbol);
		}
	} // namespace hpack
} // namespace http