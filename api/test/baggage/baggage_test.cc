#include "opentelemetry/nostd/string_view.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "opentelemetry/baggage/baggage.h"

using namespace opentelemetry;
using namespace opentelemetry::baggage;

// ----------------------- Entry Class Tests --------------------------

TEST(EntryTest, KeyValueConstruction)
{
  nostd::string_view key      = "test_key";
  nostd::string_view val      = "test_value";
  nostd::string_view metadata = "metadata";
  Baggage::Entry e(key, val, metadata);
  EXPECT_EQ(key.size(), e.GetKey().size());
  EXPECT_EQ(key, e.GetKey());
  EXPECT_EQ(val.size(), e.GetValue().size());
  EXPECT_EQ(val, e.GetValue());
  EXPECT_EQ(metadata.size(), e.GetMetadata().size());
  EXPECT_EQ(metadata, e.GetMetadata());
}

TEST(EntryTest, Copy)
{
  Baggage::Entry e("test_key", "test_value", "test_metadata");
  Baggage::Entry copy(e);
  EXPECT_EQ(copy.GetKey(), e.GetKey());
  EXPECT_EQ(copy.GetValue(), e.GetValue());
  EXPECT_EQ(copy.GetMetadata(), e.GetMetadata());
}

TEST(EntryTest, Assignment)
{
  Baggage::Entry e("test_key", "test_value", "test_metadata");
  Baggage::Entry empty;
  empty = e;
  EXPECT_EQ(empty.GetKey(), e.GetKey());
  EXPECT_EQ(empty.GetValue(), e.GetValue());
  EXPECT_EQ(empty.GetMetadata(), e.GetMetadata());
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

TEST(BaggageTest, ValidateExtractHeader)
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
    std::vector<const char *> keys;
    std::vector<const char *> values;
    std::vector<const char *> metadata;
  } testcases[] = {{"k1=v1", {"k1"}, {"v1"}, {""}},
                   {"k1=V1,K2=v2;metadata,k3=v3",
                    {"k1", "K2", "k3"},
                    {"V1", "v2", "v3"},
                    {"", "metadata", ""}},  // metdata is read
                   {",k1 =v1,k2=v2 ; metadata,",
                    {"k1", "k2"},
                    {"v1", "v2"},
                    {"", "metadata"}},  // key, value and metadata are trimmed
                   {"1a-2f%40foo=bar%251,a%2A%2Ffoo-_%2Fbar=bar+4",
                    {"1a-2f@foo", "a*/foo-_/bar"},
                    {"bar%1", "bar 4"},
                    {"", ""}},
                   {"k1=v1,k2=v2,invalidmember", {"k1", "k2"}, {"v1", "v2"}, {"", ""}},
                   {",", {}, {}, {}},
                   {",=,", {}, {}, {}},
                   {"", {}, {}, {}},
                   {"k1=%5zv", {}, {}, {}},  // invalid hex : invalid second digit
                   {"k1=%5", {}, {}, {}},    // invalid hex : missing two digits
                   {"k%z2=v1", {}, {}, {}},  // invalid hex : invalid first digit
                   {"k%00=v1", {}, {}, {}},  // key not valid
                   {"k=v%7f", {}, {}, {}},   // value not valid
                   {invalid_key_value_size_header.data(), {}, {}, {}}};
  for (auto &testcase : testcases)
  {
    auto baggage        = Baggage::FromHeader(testcase.input);
    auto all_pairs_span = baggage->GetAll();
    int num_entries     = all_pairs_span.size();
    EXPECT_EQ(testcase.keys.size(), num_entries);
    for (int i = 0; i < testcase.keys.size(); i++)
    {
      EXPECT_EQ(testcase.keys[i], all_pairs_span[i].GetKey());
      EXPECT_EQ(testcase.values[i], all_pairs_span[i].GetValue());
      EXPECT_EQ(testcase.metadata[i], all_pairs_span[i].GetMetadata());
    }
  }

  // For header with maximum threshold pairs, no pair is dropped
  EXPECT_EQ(Baggage::FromHeader(max_pairs_header.data())->ToHeader(), max_pairs_header.data());

  // For header with total size more than threshold, baggage is dropped
  EXPECT_EQ(Baggage::FromHeader(invalid_total_size_header.data())->ToHeader(), "");
}

TEST(BaggageTest, ValidateInjectHeader)
{
  struct
  {
    std::vector<const char *> keys;
    std::vector<const char *> values;
    std::vector<const char *> metadata;
    const char *header;
  } testcases[] = {{{"k1"}, {"v1"}, {""}, "k1=v1"},
                   {{"k3", "k2", "k1"},
                    {"v3", "v2", "v1"},
                    {"metadata3", "", "metadata1;mk=mv"},
                    "k1=v1;metadata1;mk=mv,k2=v2,k3=v3;metadata3"},
                   {{"k3", "k2", "k1"}, {"", "v2", "v1"}, {"", "", ""}, "k1=v1,k2=v2,k3="},
                   {{"1a-2f@foo", "a*/foo-_/bar"},
                    {"bar%1", "bar 4"},
                    {"", ""},
                    "a%2A%2Ffoo-_%2Fbar=bar+4,1a-2f%40foo=bar%251"}};
  for (auto &testcase : testcases)
  {
    nostd::shared_ptr<Baggage> baggage(new Baggage{});
    for (int i = 0; i < testcase.keys.size(); i++)
    {
      baggage = baggage->Set(testcase.keys[i], testcase.values[i], testcase.metadata[i]);
    }
    EXPECT_EQ(baggage->ToHeader(), testcase.header);
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

  header            = header_with_max_members();
  auto baggage2     = Baggage::FromHeader(header);
  auto baggage2_new = baggage2->Set("key0", "0");  // updating on max list should work
  EXPECT_EQ(baggage2_new->Get("key0"), "0");

  header            = "k1=v1,k2=v2";
  auto baggage3     = Baggage::FromHeader(header);
  auto baggage3_new = baggage3->Set("", "n_v1");  // adding invalid key, should return empty
  EXPECT_EQ(baggage3_new->ToHeader(), "");
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
  for (int i = 0; i < num_entries; i++)
  {
    EXPECT_EQ(baggage->Get(all_pairs_span[i].GetKey()), all_pairs_span[i].GetValue());
  }
}
