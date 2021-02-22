#pragma once

#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/nostd/span.h"

#include <iostream>


OPENTELEMETRY_BEGIN_NAMESPACE

namespace baggage {

/**
 * Baggage is used to annotate telemetry, adding context and information to metrics, traces, and logs. 
 * It is a set of name/value pairs describing user-defined properties
 * 
 * For more information refer to:
 * https://www.w3.org/TR/baggage/ 
 */
class Baggage
{
public:
    static constexpr size_t kMaxKeyValuePairs = 180;
    static constexpr size_t kMaxKeyValueSize = 4096;
    static constexpr size_t kMaxSize = 8192;
    static constexpr char kKeyValueSeparator = '=';
    static constexpr char kMembersSeparator  = ',';

    // Class to store key-value pairs.
    class Entry
    {
    public:
        Entry() : key_(nullptr), value_(nullptr), total_size_(0) {};
        
        // Copy constructor
        Entry(const Entry &other)
        {
        key_   = CopyStringToPointer(other.key_.get());
        value_ = CopyStringToPointer(other.value_.get());
        total_size_ = other.total_size_;
        }

        // Copy assignment operator
        Entry &operator=(Entry &other)
        {
        key_   = CopyStringToPointer(other.key_.get());
        value_ = CopyStringToPointer(other.value_.get());
        total_size_ = other.total_size_;

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
        total_size_ = key.size() + value.size();
        }
        // Gets the key associated with this entry.
        nostd::string_view GetKey() const { return key_.get(); }
        // Gets the value associated with this entry.
        nostd::string_view GetValue() const { return value_.get(); }
        // Gets the size of key value pair
        size_t GetSize() const { return total_size_; }
        // Sets the value for this entry. This overrides the previous value.
        void SetValue(nostd::string_view value) { value_ = CopyStringToPointer(value); }
    private:
        // Store key and value as raw char pointers to avoid using std::string.
        nostd::unique_ptr<const char[]> key_;
        nostd::unique_ptr<const char[]> value_;
        size_t total_size_;
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

    static nostd::shared_ptr<Baggage> FromHeader(nostd::string_view header)
    {
        nostd::shared_ptr<Baggage> baggage{new Baggage()};

        size_t begin{0};
        size_t end{0};
        size_t total_size = 0;
        while (begin < header.size() && baggage->num_entries_ < kMaxKeyValuePairs)
        {
            // find list-member
            end = header.find(kMembersSeparator, begin);
            if (end == 0) {
                // special case where "," is first char, move to next list member
                begin = 1;
                continue;
            }
            if (end == std::string::npos) {
                // last list member. `end` points to end of it.
                end = header.size() - 1;
            }
            else {
                // `end` points to end of current list member
                end--;
            }

            auto list_member = TrimString(header, begin, end);
            if (list_member.size() == 0) {
                // empty list member, move to next in list
                begin = end + 2;  // begin points to start of next member
                continue;
            }

            auto key_end_pos = list_member.find(kKeyValueSeparator);
            if (key_end_pos == std::string::npos) {
                // Error: invalid list member, return empty Baggage
                baggage->entries_.reset(nullptr);
                baggage->num_entries_ = 0;
                break;
            }
            auto key   = list_member.substr(0, key_end_pos);
            auto value = list_member.substr(key_end_pos + 1);
            if (!IsValidKey(key) || !IsValidValue(value)) {
                // invalid header. return empty baggage
                baggage->entries_.reset(nullptr);
                baggage->num_entries_ = 0;
                break;
            }
            Entry entry(key, value);
            if (entry.GetSize() < kMaxKeyValueSize && total_size + entry.GetSize() < kMaxSize) {
                (baggage->entries_.get())[baggage->num_entries_] = entry;
                baggage->num_entries_++;
                total_size += entry.GetSize();
            }
            
            begin = end + 2;
        }

        return baggage;
    }
    std::string ToHeader()
    {
        std::string header_s;
        for (size_t count = 0; count < num_entries_; count++) {
            if (count != 0) {
                header_s.append(",");
            }
            auto entry = (entries_.get())[count];
            header_s.append(std::string(entry.GetKey()));
            header_s.append(1, kKeyValueSeparator);
            header_s.append(std::string(entry.GetValue()));
        }
        return header_s;
    }

    // Access the value for a name/value pair set by a prior event. Returns a value associated with the given
    // name or null if the given name is not present.
    std::string Get(nostd::string_view key) const noexcept
    {
        if (!IsValidKey(key)) {
            return std::string();
        }
        for (size_t i = 0; i < num_entries_; i++)
        {
            auto entry = (entries_.get())[i];
            if (key == entry.GetKey()) {
                return std::string(entry.GetValue());
            }
        }
        return std::string();
    }

    // Record the value for a name/value pair. Returns a new Baggage that contains the new value.
    nostd::shared_ptr<Baggage> Set(nostd::string_view key, nostd::string_view value)
    {
        nostd::shared_ptr<Baggage> baggage{new Baggage()};
        if (!IsValidKey(key) || !IsValidValue(value)) {
            return baggage;
        }

        size_t total_size = 0;
        int key_index_in_baggage = -1;

        for (size_t i = 0; i < num_entries_; i++) {
            // Each name in the baggage must be associated with only one value.
            // We do not add old value for the key, but save it's index to be used
            // in a case when new key value is discarded. 
            if (key == (entries_.get())[i].GetKey()) {
                key_index_in_baggage = i;
                continue;
            }

            Entry e((entries_.get())[i]);

            (baggage->entries_.get())[baggage->num_entries_++] = e;
            total_size += e.GetSize();
        }


        if (baggage->num_entries_ < kMaxKeyValuePairs) {
            Entry e(key, value);
            total_size += e.GetSize();
            if (total_size < kMaxSize && e.GetSize() < kMaxKeyValueSize) {
                (baggage->entries_.get())[baggage->num_entries_++] = e;
            } else if (key_index_in_baggage != -1) {
                // we add original key, value in the baggage as new value exceeds size
                // threshold.
                auto original_entry = Entry((entries_.get())[key_index_in_baggage]);
                (baggage->entries_.get())[baggage->num_entries_++] = original_entry;
            }
        }

        return baggage;
    }

    // Delete a name/value pair. Returns a new Baggage which does not contain the selected key.
    nostd::shared_ptr<Baggage> Remove(nostd::string_view key)
    {
        nostd::shared_ptr<Baggage> baggage(new Baggage{});
        for (size_t i = 0; i < num_entries_; i++) {
            if (key == (entries_.get())[i].GetKey()) {
                continue;
            }
            Entry e((entries_.get())[i]);
            (baggage->entries_.get())[baggage->num_entries_++] = e;
        }

        return baggage;
    }

    nostd::span<Entry> GetAll() const noexcept
    {
        return nostd::span<Entry>(entries_.get(), num_entries_);
    }

private:
    nostd::unique_ptr<Entry[]> entries_;
    size_t num_entries_;

    static bool IsValidKey(nostd::string_view key)
    {
        if (key.empty()) {
            return false;
        }
        return true;
    }

    static bool IsValidValue(nostd::string_view value)
    {
        if (value.empty()) {
            return false;
        }
        return true;
    }

    static nostd::string_view TrimString(nostd::string_view str, size_t left, size_t right)
    {
        while (str[static_cast<std::size_t>(right)] == ' ' && left < right) {
            right--;
        }
        while (str[static_cast<std::size_t>(left)] == ' ' && left < right) {
            left++;
        }
        return str.substr(left, right - left + 1);
    }

};

} // baggage
OPENTELEMETRY_END_NAMESPACE
