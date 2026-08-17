#include "stl.h"

#include <strings.h>

std::vector<std::string_view> Split(const std::string_view &value, char delimiter)
{
    std::vector<std::string_view> components;
    size_t component_start = 0, component_end = std::string::npos;
    bool is_last_component = false;
    while (is_last_component == false)
    {
        component_end = value.find(delimiter, component_start);
        if (component_end == std::string::npos)
        {
            is_last_component = true;
            component_end = value.size();
        }
        components.emplace_back(value.substr(component_start, component_end - component_start));
        component_start = component_end + 1;
    }
    return components;
}

std::string_view Trim(const std::string_view &value)
{
    auto front = value.begin();
    while ( (front != value.end() && *front == ' '))
    {
        ++front;
    }
    if (front == value.end())
    {
        return std::string_view();
    }
    auto back = value.rbegin();
    while (back.base() != front && *back == ' ')
    {
        ++back;
    }
    const size_t length = back.base() == front ? 1 : back.base() - front + 1;
    return std::string_view(front, length);
}

std::string_view operator""_str_v(const char *str, size_t length)
{
    return std::string_view(str, length);
}

bool CaseInsensitiveEqual(const std::string_view &first, const std::string_view &second)
{
    if (first.size() == second.size())
    {
        return strncasecmp(first.data(), second.data(), std::min(first.size(), second.size())) == 0;
    }
    return false;
}
