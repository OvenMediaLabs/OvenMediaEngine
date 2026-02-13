#pragma once

#include <vector>
#include <string_view>
#include <type_traits>
#include <string>
#include <stdexcept>
#include <limits>

// Various STL helpers

template<typename T, size_t delimiter_length, typename = std::enable_if_t<std::is_same<T, char>::value || std::is_same<T, unsigned char>::value>>
std::vector<std::string_view> Split(const std::vector<T> &vector, const T (&delimiter)[delimiter_length])
{
    std::vector<std::string_view> components;
    const T *component_start = vector.data(), *component_end = nullptr, *vector_end = vector.data() + vector.size();
    bool is_last_component = false;
    while (is_last_component == false)
    {
        component_end = reinterpret_cast<T*>(memmem(component_start, vector_end - component_start, delimiter, delimiter_length));
        if (component_end == nullptr)
        {
            is_last_component = true;
            component_end = vector_end;
        }
        components.emplace_back(reinterpret_cast<const char*>(component_start), component_end - component_start);
        component_start = component_end + delimiter_length;
    }
    return components;
}

std::vector<std::string_view> Split(const std::string_view &value, char delimiter);
std::string_view Trim(const std::string_view &value);

// A safe noexcept wrapper around std::stoi
template<typename T, typename = std::enable_if<std::is_arithmetic<T>::value>>
bool Stoi(const std::string &string, T& value)
{
    try
    {
        auto int_value = std::stoi(string);
        if (int_value > 0 && int_value > std::numeric_limits<T>::max()) return false;
        if (std::is_signed<T>::value && int_value < 0 && int_value < std::numeric_limits<T>::min()) return false;
        value = static_cast<T>(std::stoi(string));
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::out_of_range&)
    {
        return false;
    }
}

template<typename T, size_t substring_length, typename = std::enable_if_t<std::is_same<T, char>::value || std::is_same<T, unsigned char>::value>>
bool HasSubstring(const std::string_view &value, size_t offset, const T (&substring)[substring_length])
{
    if (offset + substring_length > value.size())
    {
        return false;
    }
    return value.substr(offset, substring_length) == std::string_view(substring, substring_length);
}

std::string_view operator""_str_v(const char *str, size_t length);

bool CaseInsensitiveEqual(const std::string_view &first, const std::string_view  &second);
