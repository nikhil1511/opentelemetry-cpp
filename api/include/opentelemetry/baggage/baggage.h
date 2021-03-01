#pragma once

#include <iomanip>
#include <iostream>
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/unique_ptr.h"

OPENTELEMETRY_BEGIN_NAMESPACE

namespace baggage
{

/**
 * Baggage is used to annotate telemetry, adding context and information to metrics, traces, and
 * logs. It is a set of name/value pairs describing user-defined properties
 *
 * For more information refer to:
 * https://www.w3.org/TR/baggage/
 */
class Baggage
{
public:
  static constexpr size_t kMaxKeyValuePairs     = 180;
  static constexpr size_t kMaxKeyValueSize      = 4096;
  static constexpr size_t kMaxSize              = 8192;
  static constexpr char kKeyValueSeparator      = '=';
  static constexpr char kMembersSeparator       = ',';
  static constexpr char kValueMetadataSeparator = ';';

  // Class to store key-value pairs.
  class Entry
  {
  public:
    Entry() : key_(nullptr), value_(nullptr), metadata_(nullptr){};

    // Copy constructor
    Entry(const Entry &other)
    {
      key_      = CopyStringToPointer(other.key_.get());
      value_    = CopyStringToPointer(other.value_.get());
      metadata_ = CopyStringToPointer(other.metadata_.get());
    }

    // Copy assignment operator
    Entry &operator=(Entry &other)
    {
      key_      = CopyStringToPointer(other.key_.get());
      value_    = CopyStringToPointer(other.value_.get());
      metadata_ = CopyStringToPointer(other.metadata_.get());

      return *this;
    }
    // Move contructor and assignment operator
    Entry(Entry &&other) = default;
    Entry &operator=(Entry &&other) = default;
    // Creates an Entry for a given key-value pair.
    Entry(nostd::string_view key,
          nostd::string_view value,
          nostd::string_view metadata = "") noexcept
    {
      key_      = CopyStringToPointer(key);
      value_    = CopyStringToPointer(value);
      metadata_ = CopyStringToPointer(metadata);
    }
    // Gets the key associated with this entry.
    nostd::string_view GetKey() const { return key_.get(); }
    // Gets the value associated with this entry.
    nostd::string_view GetValue() const { return value_.get(); }
    // Gets the metdata associated with the entry
    nostd::string_view GetMetadata() const { return metadata_.get(); }

  private:
    // Store key and value as raw char pointers to avoid using std::string.
    nostd::unique_ptr<const char[]> key_;
    nostd::unique_ptr<const char[]> value_;
    nostd::unique_ptr<const char[]> metadata_;
    // Copies string into a buffer and returns a unique_ptr to the buffer.
    // This is a workaround for the fact that memcpy doesn't accept a const destination.
    nostd::unique_ptr<const char[]> CopyStringToPointer(nostd::string_view str)
    {
      char *temp = new char[str.size() + 1];
      memcpy(temp, str.data(), str.size());
      temp[str.size()] = '\0';
      return nostd::unique_ptr<const char[]>(temp);
    }
  };

  Baggage() : entries_(new Entry[kMaxKeyValuePairs]), num_entries_{0} {}

  // Creates baggage object by extracting baggage info from header
  static nostd::shared_ptr<Baggage> FromHeader(nostd::string_view header)
  {
    nostd::shared_ptr<Baggage> baggage{new Baggage()};

    if (header.size() > kMaxSize)
    {
      return baggage;
    }

    size_t begin{0};
    size_t end{0};
    while (begin < header.size() && baggage->num_entries_ < kMaxKeyValuePairs)
    {
      // find list-member
      end = header.find(kMembersSeparator, begin);
      if (end == begin)
      {
        // character is ,
        begin++;
        continue;
      }
      if (end == std::string::npos)
      {
        // last list member. `end` points to end of it.
        end = header.size() - 1;
      }
      else
      {
        // `end` points to end of current list member
        end--;
      }

      auto list_member = header.substr(begin, end - begin + 1);

      auto key_end_pos = list_member.find(kKeyValueSeparator);
      if (key_end_pos == std::string::npos)
      {
        // Invalid list member, key end not found. Ignore this entry
        begin = end + 2;
        continue;
      }

      nostd::string_view metadata;
      auto value_end_pos   = list_member.find(kValueMetadataSeparator);
      size_t value_end_len = std::string::npos;
      if (value_end_pos != std::string::npos)
      {
        value_end_len = value_end_pos - key_end_pos - 1;

        // metadata is trimmed and kept as it is
        metadata = TrimString(list_member.substr(value_end_pos + 1));
      }

      int err    = 0;
      auto key   = Decode(TrimString(list_member.substr(0, key_end_pos)), err);
      auto value = Decode(TrimString(list_member.substr(key_end_pos + 1, value_end_len)), err);
      if (IsValidKey(key) && IsValidValue(value) && err == 0)
      {
        Entry entry(key, value, metadata);
        if (end - begin + 1 < kMaxKeyValueSize)
        {
          (baggage->entries_.get())[baggage->num_entries_] = entry;
          baggage->num_entries_++;
        }
      }

      begin = end + 2;
    }

    return baggage;
  }

  // Creates string from baggage object.
  std::string ToHeader()
  {
    std::string header_s;
    for (size_t count = 0; count < num_entries_; count++)
    {
      if (count != 0)
      {
        header_s.append(",");
      }
      auto entry = (entries_.get())[count];
      header_s.append(Encode(entry.GetKey()));
      header_s.push_back(kKeyValueSeparator);
      header_s.append(Encode(entry.GetValue()));
      if (entry.GetMetadata().size())
      {
        header_s.push_back(kValueMetadataSeparator);
        header_s.append(std::string{entry.GetMetadata()});
      }
    }
    return header_s;
  }

  // Access the value for a name/value pair set by a prior event. Returns a value associated with
  // the given name or null if the given name is not present.
  std::string Get(nostd::string_view key) const noexcept
  {
    if (!IsValidKey(key))
    {
      return std::string();
    }
    for (size_t i = 0; i < num_entries_; i++)
    {
      auto entry = (entries_.get())[i];
      if (key == entry.GetKey())
      {
        return std::string(entry.GetValue());
      }
    }
    return std::string();
  }

  // Record the value for a name/value pair. Returns a new Baggage that contains the new value.
  nostd::shared_ptr<Baggage> Set(nostd::string_view key,
                                 nostd::string_view value,
                                 nostd::string_view metadata = "")
  {
    nostd::shared_ptr<Baggage> baggage{new Baggage()};
    if (!IsValidKey(key) || !IsValidValue(value))
    {
      return baggage;
    }

    Entry new_e(key, value, metadata);
    (baggage->entries_.get())[baggage->num_entries_++] = new_e;
    for (size_t i = 0; i < num_entries_; i++)
    {
      // Each name in the baggage must be associated with only one value.
      // We do not add old value for the key.
      if (key == (entries_.get())[i].GetKey())
      {
        continue;
      }

      Entry e((entries_.get())[i]);
      (baggage->entries_.get())[baggage->num_entries_++] = e;
    }

    return baggage;
  }

  // Delete a name/value pair. Returns a new Baggage which does not contain the selected key.
  nostd::shared_ptr<Baggage> Remove(nostd::string_view key)
  {
    nostd::shared_ptr<Baggage> baggage(new Baggage{});
    for (size_t i = 0; i < num_entries_; i++)
    {
      if (key == (entries_.get())[i].GetKey())
      {
        continue;
      }
      Entry e((entries_.get())[i]);
      (baggage->entries_.get())[baggage->num_entries_++] = e;
    }

    return baggage;
  }

  // Get all key-value pairs in baggage
  nostd::span<Entry> GetAll() const noexcept
  {
    return nostd::span<Entry>(entries_.get(), num_entries_);
  }

private:
  nostd::unique_ptr<Entry[]> entries_;
  size_t num_entries_;

  static bool IsPrintableString(nostd::string_view str)
  {
    for (const auto &ch : str)
    {
      if (ch < ' ' || ch > '~')
      {
        return false;
      }
    }

    return true;
  }

  static bool IsValidKey(nostd::string_view key) { return key.size() && IsPrintableString(key); }

  static bool IsValidValue(nostd::string_view value) { return IsPrintableString(value); }

  // Remove trailing spaces from str [left, right]
  static nostd::string_view TrimString(nostd::string_view str)
  {
    if (str.size() == 0)
    {
      return str;
    }

    size_t left = 0, right = str.size() - 1;
    while (str[right] == ' ' && left < right)
    {
      right--;
    }
    while (str[left] == ' ' && left < right)
    {
      left++;
    }
    return str.substr(left, right - left + 1);
  }

  // Uri encode key value pairs before injecting into header
  static std::string Encode(nostd::string_view str)
  {
    auto to_hex = [](char c) -> char {
      static const char *hex = "0123456789ABCDEF";
      return hex[c & 15];
    };

    std::string ret;

    for (auto c : str)
    {
      if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      {
        ret.push_back(c);
      }
      else if (c == ' ')
      {
        ret.push_back('+');
      }
      else
      {
        ret.push_back('%');
        ret.push_back(to_hex(c >> 4));
        ret.push_back(to_hex(c & 15));
      }
    }

    return ret;
  }

  // Uri decode key value pairs after extracting from header
  static std::string Decode(nostd::string_view str, int &err)
  {
    auto IsHex = [](char c) {
      return std::isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    };

    auto from_hex = [](char c) -> char {
      return std::isdigit(c) ? c - '0' : std::toupper(c) - 'A' + 10;
    };

    std::string ret;

    for (int i = 0; i < str.size(); i++)
    {
      if (str[i] == '%')
      {
        if (i + 2 >= str.size() || !IsHex(str[i + 1]) || !IsHex(str[i + 2]))
        {
          err = 1;
          return "";
        }
        ret.push_back(from_hex(str[i + 1]) << 4 | from_hex(str[i + 2]));
        i += 2;
      }
      else if (str[i] == '+')
      {
        ret.push_back(' ');
      }
      else if (std::isalnum(str[i]) || str[i] == '-' || str[i] == '_' || str[i] == '.' ||
               str[i] == '~')
      {
        ret.push_back(str[i]);
      }
      else
      {
        err = 1;
        return "";
      }
    }

    return ret;
  }
};

}  // namespace baggage
OPENTELEMETRY_END_NAMESPACE
