#pragma once

#include <base/ovlibrary/bit_reader.h>
#include <stdint.h>
#include <vector>

class NalUnitSplitter;
class NalUnitList
{
public:
    uint32_t GetCount()
    {
        return _nal_list.size();
    }
    std::shared_ptr<ov::Data>   GetNalUnit(uint32_t index)
    {
        if(index > GetCount() - 1)
        {
            return nullptr;
        }

        return _nal_list[index];
    }

private:
    std::vector<std::shared_ptr<ov::Data>>   _nal_list;
    uint8_t _bitstream;
    size_t _bitstream_length;

    friend class NalUnitSplitter;
};

class NalUnitSplitter
{
public:
    static std::shared_ptr<NalUnitList> Parse(const uint8_t* bitstream, size_t bitstream_length);
private:
};