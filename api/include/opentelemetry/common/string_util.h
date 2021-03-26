#pragma once

#include "opentelemetry/nostd/string_view.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace common
{

class StringUtil
{
public:
  static nostd::string_view Trim(nostd::string_view str, size_t left, size_t right)
  {
    while (str[static_cast<std::size_t>(right)] == ' ' && left < right)
    {
      right--;
    }
    while (str[static_cast<std::size_t>(left)] == ' ' && left < right)
    {
      left++;
    }
    return str.substr(left, right - left + 1);
  }
};

}  // namespace common

OPENTELEMETRY_END_NAMESPACE
