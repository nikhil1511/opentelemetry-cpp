#include "opentelemetry/nostd/string_view.h"

#include <gtest/gtest.h>
#include <string>

#include "opentelemetry/baggage/baggage.h"

using namespace opentelemetry::baggage;

// ----------------------- Entry Class Tests --------------------------

// Test constructor that takes a key-value pair
TEST(EntryTest, KeyValueConstruction)
{
  opentelemetry::nostd::string_view key = "test_key";
  opentelemetry::nostd::string_view val = "test_value";
  Baggage::Entry e(key, val);
  EXPECT_EQ(key.size(), e.GetKey().size());
  EXPECT_EQ(key, e.GetKey());
  EXPECT_EQ(val.size(), e.GetValue().size());
  EXPECT_EQ(val, e.GetValue());
}

// Test copy constructor
TEST(EntryTest, Copy)
{
  Baggage::Entry e("test_key", "test_value");
  Baggage::Entry copy(e);
  EXPECT_EQ(copy.GetKey(), e.GetKey());
  EXPECT_EQ(copy.GetValue(), e.GetValue());
}
// Test assignment operator
TEST(EntryTest, Assignment)
{
  Baggage::Entry e("test_key", "test_value");
  Baggage::Entry empty;
  empty = e;
  EXPECT_EQ(empty.GetKey(), e.GetKey());
  EXPECT_EQ(empty.GetValue(), e.GetValue());
}
TEST(EntryTest, SetValue)
{
  Baggage::Entry e("test_key", "test_value");
  opentelemetry::nostd::string_view new_val = "new_value";
  e.SetValue(new_val);
  EXPECT_EQ(new_val.size(), e.GetValue().size());
  EXPECT_EQ(new_val, e.GetValue());
}

// ----------------------- Baggage Class Tests --------------------------

std::string header_with_max_members()
{
  std::string header;
  auto max_members = Baggage::kMaxKeyValuePairs;
  for (int i = 0; i < max_members; i++)
  {
    std::string key   = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    header += key + "=" + value;
    if (i != max_members - 1)
    {
      header += ",";
    }
  }
  return header;
}

std::string header_with_custom_size(size_t key_value_size, size_t num_entries)
{
  std::string header = "";
  for (int i = 0; i < num_entries; i++)
  {
    std::string str = std::to_string(i + 1);
    str += "=";
    assert(key_value_size > str.size());
    for (int j = str.size(); j < key_value_size; j++)
    {
      str += "a";
    }

    header += str + ',';
  }

  header.pop_back();
  return header;
}

TEST(BaggageTest, ValidateHeaderParsing)
{
  auto max_pairs_header              = header_with_max_members();
  auto invalid_key_value_size_header = header_with_custom_size(Baggage::kMaxKeyValueSize + 1, 1);
  int num_pairs_with_max_size        = Baggage::kMaxSize / Baggage::kMaxKeyValueSize;
  auto invalid_total_size_header =
      header_with_custom_size(Baggage::kMaxKeyValueSize, num_pairs_with_max_size + 1);
  auto valid_header_with_max_size_pairs =
      header_with_custom_size(Baggage::kMaxKeyValueSize, num_pairs_with_max_size);

  struct
  {
    const char *input;
    const char *expected;
  } testcases[] = {{"k1=v1", "k1=v1"},
                   {"K1=V1", "K1=V1"},
                   {"k1=v1,k2=v2,k3=v3", "k1=v1,k2=v2,k3=v3"},
                   {"k1=v1,k2=v2,,", "k1=v1,k2=v2"},
                   {"k1=v1,k2=v2,invalidmember", ""},
                   {"1a-2f@foo=bar1,a*/foo-_/bar=bar4", "1a-2f@foo=bar1,a*/foo-_/bar=bar4"},
                   {"1a-2f@foo=bar1,*/foo-_/bar=bar4", "1a-2f@foo=bar1,*/foo-_/bar=bar4"},
                   {",k1=v1", "k1=v1"},
                   {",", ""},
                   {",=,", ""},
                   {"", ""},
                   {max_pairs_header.data(), max_pairs_header.data()},
                   {invalid_key_value_size_header.data(), ""},
                   {invalid_total_size_header.data(), valid_header_with_max_size_pairs.data()}};
  for (auto &testcase : testcases)
  {
    EXPECT_EQ(Baggage::FromHeader(testcase.input)->ToHeader(), testcase.expected);
  }
}

TEST(BaggageTest, BaggageGet)
{
  auto header  = header_with_max_members();
  auto baggage = Baggage::FromHeader(header);

  EXPECT_EQ(baggage->Get("key0"), "value0");
  EXPECT_EQ(baggage->Get("key16"), "value16");
  EXPECT_EQ(baggage->Get("key31"), "value31");
  EXPECT_EQ(baggage->Get("key181"), "");
}

TEST(BaggageTest, BaggageSet)
{
  std::string header = "k1=v1,k2=v2";
  auto baggage       = Baggage::FromHeader(header);

  auto baggage_new = baggage->Set("k3", "v3");
  EXPECT_EQ(baggage_new->Get("k3"), "v3");
  auto baggage_new_2 =
      baggage_new->Set("k3", "v3_1");  // key should be updated with the latest value
  EXPECT_EQ(baggage_new_2->Get("k3"), "v3_1");

  header        = header_with_max_members();
  auto baggage2 = Baggage::FromHeader(header);
  auto baggage2_new =
      baggage2->Set("n_k1", "n_v1");  // adding to max list, should return copy of existing list
  EXPECT_EQ(baggage2_new->ToHeader(), header);
  auto baggage2_new_2 =
      baggage2_new->Set("key0", "0");  // updating on max list with size threshold should work
  EXPECT_EQ(baggage2_new_2->Get("key0"), "0");

  header            = "k1=v1,k2=v2";
  auto baggage3     = Baggage::FromHeader(header);
  auto baggage3_new = baggage3->Set("", "n_v1");  // adding invalid key, should return empty
  EXPECT_EQ(baggage3_new->ToHeader(), "");

  auto invalid_max_size_value = std::string(Baggage::kMaxKeyValueSize, 'a');
  auto baggage3_new_2 =
      baggage3->Set("k3", invalid_max_size_value);  // adding pair with length greater than
                                                    // threshold should be discarded.
  EXPECT_EQ(baggage2_new_2->Get("k3"), "");

  auto baggage3_new_3 = baggage3->Set(
      "k2", invalid_max_size_value);  // adding pair with length greater than threshold should be
                                      // discarded. old baggage should be returned.
  EXPECT_EQ(baggage3_new_3->Get("k2"), "v2");

  int num_pairs_with_max_size = Baggage::kMaxSize / Baggage::kMaxKeyValueSize;
  header        = header_with_custom_size(Baggage::kMaxKeyValueSize, num_pairs_with_max_size);
  auto baggage4 = Baggage::FromHeader(header);
  auto baggage4_new =
      baggage4->Set("k", std::string((int)Baggage::kMaxKeyValueSize - 1,
                                     'a'));  // valid pair which increases size of baggage beyond
                                             // max size threshold should be discarded.

  EXPECT_EQ(baggage4_new->Get("k"), "");
}

TEST(BaggageTest, BaggageRemove)
{
  auto header  = header_with_max_members();
  auto baggage = Baggage::FromHeader(header);

  EXPECT_EQ(baggage->Get("key0"), "value0");
  auto new_baggage = baggage->Remove("key0");
  EXPECT_EQ(new_baggage->Get("key0"), "");

  EXPECT_EQ(baggage->Get("key181"), "");
  auto new_baggage_2 = baggage->Remove("key181");
  EXPECT_EQ(new_baggage_2->Get("key181"), "");
}

TEST(BaggageTest, BaggageGetAll)
{
  std::string header  = "k1=v1,k2=v2";
  auto baggage        = Baggage::FromHeader(header);
  auto all_pairs_span = baggage->GetAll();
  int num_entries     = all_pairs_span.size();
  EXPECT_EQ(num_entries, 2);
}
