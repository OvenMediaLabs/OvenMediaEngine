#pragma once

#include "socket.h"

#include <limits>

namespace ov
{
    /*
        A trivial class for managing port ranges. The class does not care about released ports and will always traverse
        the whole range from the first known free element, so a possible improvement in the future is to add a bitmask which actually
        holds the free entries.
    */
    template<SocketType socket_type>
    class PortRange
    {
    public:
        class Iterator
        {
        public:
            Iterator(uint16_t port, uint16_t lower_bound, uint16_t upper_bound, uint16_t count) : port_(port),
                lower_bound_(upper_bound),
                upper_bound_(upper_bound),
                count_(count)
            {
            }

            bool operator!=(const Iterator &iterator)
            {
                return port_ != iterator.port_;
            } 

            Iterator &operator++()
            {
                if (count_)
                {
                    port_ = port_ == upper_bound_ ? lower_bound_ : port_ + 1;
                    --count_;
                }
                return *this;
            }

            const uint16_t &operator*() const
            {
                return port_;
            }

        private:
            uint16_t port_;
            uint16_t lower_bound_;
            uint16_t upper_bound_;
            uint16_t count_;
        };

        PortRange(uint16_t lower_bound, uint16_t upper_bound) : range_(std::make_pair(lower_bound, upper_bound)),
            size_ (range_.second - range_.first),
            next_available_port_(lower_bound)
        {
        }

        void SetNextAvailablePort(uint16_t next_available_port)
        {
            next_available_port_ = next_available_port;
        }

        Iterator begin()
        {
            return Iterator(next_available_port_, range_.first, range_.second, size_);
        }

        Iterator end()
        {
            return Iterator(0, 0, 0, 0);
        }

        static constexpr SocketType type = socket_type;

        PortRange Create(uint16_t lower_bound, uint16_t upper_bound)
        {
            if (lower_bound < upper_bound)
            {
                return PortRange(lower_bound, upper_bound);
            }
            return PortRange(0, 0);
        }

    private:
        std::pair<uint16_t, uint16_t> range_;
        uint16_t size_;
        uint16_t next_available_port_;
    };
}

