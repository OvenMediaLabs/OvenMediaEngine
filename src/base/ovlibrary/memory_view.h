#pragma once

#include <stdint.h>
#include <cstddef>
#include <vector>
#include <type_traits>

class MemoryView
{
public:
    MemoryView(uint8_t *buffer, size_t capacity, size_t write_offset = 0, size_t read_offset = 0) : buffer_(buffer),
        capacity_(capacity),
        write_offset_(write_offset),
        read_offset_(read_offset),
        good_(true),
        eof_(false)
    {
    }

    template<typename T, typename = typename std::enable_if_t<std::is_fundamental<T>::value>>
    MemoryView &operator<<(const T &value)
    {
        if (write_offset_ + sizeof(value) <= capacity_)
        {
            memcpy(buffer_ + write_offset_, &value, sizeof(value));
            write_offset_ += sizeof(value);
        }
        else
        {
            good_ = false;
        }
        return *this;
    }

    template<typename T, typename = typename std::enable_if_t<std::is_fundamental<T>::value>>
    MemoryView &operator<<(const std::vector<T> &values)
    {
        (*this) << values.size();
        const size_t data_size = values.size() * sizeof(T);
        if (good_ && write_offset_ + data_size <= capacity_)
        {
            memcpy(buffer_ + write_offset_, values.data(), data_size);
            write_offset_ += data_size;
        }
        else
        {
            good_ = false;
        }
        return *this;       
    }

    template<typename T>
    MemoryView &operator<<(const std::vector<std::vector<T>> &values)
    {
        (*this) << values.size();
        auto iterator = values.begin();
        while (good_ && iterator != values.end())
        {
            (*this) << *iterator;
            ++iterator;
        }
        return *this;       
    }

    template<typename T>
    MemoryView &operator>>(T &value)
    {
        if (read_offset_ + sizeof(value) <= write_offset_)
        {
            memcpy(&value, buffer_ + read_offset_, sizeof(value));
            read_offset_ += sizeof(value);
            eof_ = read_offset_ == write_offset_;
        }
        else
        {
            good_ = false;
        }
        return *this;
    }

    template<typename T, typename = typename std::enable_if_t<std::is_fundamental<T>::value>>
    MemoryView &operator>>(std::vector<T> &values)
    {
        size_t size = 0;
        (*this) >> size;
        values.resize(size);
        if (good_ && size)
        {
            const size_t data_size = size * sizeof(T);
            if (read_offset_ + data_size <= write_offset_)
            {
                memcpy(values.data(), buffer_ + read_offset_, data_size);
                read_offset_ += data_size;
                eof_ = read_offset_ == write_offset_;
            }
            else
            {
                good_ = false;
            }
        }
        return *this;
    }

    template<typename T>
    MemoryView &operator>>(std::vector<std::vector<T>> &values)
    {
        size_t size = 0;
        (*this) >> size;
        if (good_ && size)
        {
            while (good_ && size)
            {
                std::vector<T> value;
                (*this) >> value;
                if (good_)
                {
                    values.emplace_back(value);
                }
                else
                {
                    values.clear();
                    break;
                }
                --size;
            }
        }
        return *this;
    }

    bool good() const
    {
        return good_;
    }

    bool eof() const
    {
        return eof_;
    }

private:
    uint8_t *const buffer_;
    const size_t capacity_;
    size_t write_offset_;
    size_t read_offset_;
    bool good_;
    bool eof_;
};