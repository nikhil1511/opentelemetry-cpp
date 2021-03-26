#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/common/string_util.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/version.h"

#include <string>
#include <type_traits>

OPENTELEMETRY_BEGIN_NAMESPACE
namespace common
{

// Iterator for key-value headers
class KeyValueStringIterator
{
public:
  enum class Status
  {
    VALID = 0,
    INVALID,
    END,
  };

  struct KeyValuePair
  {
    Status status;
    nostd::string_view key;
    nostd::string_view value;
  };

  KeyValueStringIterator(nostd::string_view str,
                         char member_separator    = ',',
                         char key_value_separator = '=')
      : str_(str),
        member_separator_(member_separator),
        key_value_separator_(key_value_separator),
        index_(0)
  {}

  KeyValuePair next()
  {
    KeyValuePair ret;
    ret.status = Status::VALID;
    while (index_ < str_.size())
    {
      size_t end = str_.find(member_separator_, index_);
      if (end == std::string::npos)
      {
        end = str_.size() - 1;
      }
      else
      {
        end--;
      }

      auto list_member = StringUtil::Trim(str_, index_, end);
      if (list_member.size() == 0)
      {
        // empty list member. Move to next entry. For both baggage and trace_state this is valid
        // behaviour.
        index_ = end + 2;
        continue;
      }

      auto key_end_pos = list_member.find(key_value_separator_);
      if (key_end_pos == std::string::npos)
      {
        // invalid member
        ret.status = Status::INVALID;
        return ret;
      }

      ret.key   = list_member.substr(0, key_end_pos);
      ret.value = list_member.substr(key_end_pos + 1);

      index_ = end + 2;
      return ret;
    }

    ret.status = Status::END;
    return ret;
  }

  void reset() { index_ = 0; }

private:
  nostd::string_view str_;
  const char member_separator_;
  const char key_value_separator_;
  size_t index_;
};

// Class to store fixed size array of key-value pairs of string type
class KeyValueProperties
{
  // Class to store key-value pairs of string types
public:
  class Entry
  {
  public:
    Entry() : key_(nullptr), value_(nullptr){};

    // Copy constructor
    Entry(const Entry &copy)
    {
      key_   = CopyStringToPointer(copy.key_.get());
      value_ = CopyStringToPointer(copy.value_.get());
    }

    // Copy assignment operator
    Entry &operator=(Entry &other)
    {
      key_   = CopyStringToPointer(other.key_.get());
      value_ = CopyStringToPointer(other.value_.get());
      return *this;
    }

    // Move contructor and assignment operator
    Entry(Entry &&other) = default;
    Entry &operator=(Entry &&other) = default;

    // Creates an Entry for a given key-value pair.
    Entry(nostd::string_view key, nostd::string_view value) noexcept
    {
      key_   = CopyStringToPointer(key);
      value_ = CopyStringToPointer(value);
    }

    // Gets the key associated with this entry.
    nostd::string_view GetKey() const { return key_.get(); }

    // Gets the value associated with this entry.
    nostd::string_view GetValue() const { return value_.get(); }

    // Sets the value for this entry. This overrides the previous value.
    void SetValue(nostd::string_view value) { value_ = CopyStringToPointer(value); }

  private:
    // Store key and value as raw char pointers to avoid using std::string.
    nostd::unique_ptr<const char[]> key_;
    nostd::unique_ptr<const char[]> value_;

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

  // Maintain the number of entries in entries_.
  size_t num_entries_;

  // Max size of allocated array
  size_t max_num_entries_;

  // Store entries in a C-style array to avoid using std::array or std::vector.
  nostd::unique_ptr<Entry[]> entries_;

public:
  // Create Key-value list of given size
  // @param size : Size of list.
  KeyValueProperties(size_t size)
      : num_entries_(0), max_num_entries_(size), entries_(new Entry[size])
  {}

  // Create Empty Key-Value list
  KeyValueProperties() : num_entries_(0), max_num_entries_(0), entries_(nullptr) {}

  template <class T, typename = std::enable_if<detail::is_key_value_iterable<T>::value>>
  KeyValueProperties(const T &keys_and_values)
      : max_num_entries_(keys_and_values.size()), entries_(new Entry[max_num_entries_])
  {
    for (auto &e : keys_and_values)
    {
      Entry entry(keys_and_values.first, keys_and_values.second);
      (entries_.get())[num_entries_++] = entry;
    }
  }

  void AddEntry(const nostd::string_view &key, const nostd::string_view &value)
  {
    if (num_entries_ < max_num_entries_)
    {
      Entry entry(key, value);
      (entries_.get())[num_entries_++] = entry;
    }
  }

  bool GetAllEntries(
      nostd::function_ref<bool(nostd::string_view, nostd::string_view)> callback) const noexcept
  {
    for (size_t i = 0; i < num_entries_; i++)
    {
      auto entry = (entries_.get())[i];
      if (!callback(entry.GetKey(), entry.GetValue()))
      {
        return false;
      }
    }
    return true;
  }

  bool GetValue(const nostd::string_view key, std::string &value)
  {
    for (size_t i = 0; i < num_entries_; i++)
    {
      auto entry = (entries_.get())[i];
      if (entry.GetKey() == key)
      {
        value = std::string(entry.GetValue());
        return true;
      }
    }
    return false;
  }

  size_t Size() { return num_entries_; }
};
}  // namespace common
OPENTELEMETRY_END_NAMESPACE