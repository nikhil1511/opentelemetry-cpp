#include "opentelemetry/nostd/string_view.h"

#include <iostream>
#include <map>
#include <string>

#include <gtest/gtest.h>

#include "opentelemetry/baggage/propagation/baggage_propagator.h"

using namespace opentelemetry;
using namespace opentelemetry::baggage;

static nostd::string_view Getter(const std::map<std::string, std::string> &carrier,
                                 nostd::string_view key = "baggage")
{
  auto it = carrier.find(std::string(key));
  if (it != carrier.end())
  {
    return nostd::string_view(it->second);
  }
  return "";
}

static void Setter(std::map<std::string, std::string> &carrier,
                   nostd::string_view key   = "baggage",
                   nostd::string_view value = "")
{
  carrier[std::string(key)] = std::string(value);
}

using MapBaggagePropagator =
    baggage::propagation::BaggagePropagator<std::map<std::string, std::string>>;

static MapBaggagePropagator baggage_propagator = MapBaggagePropagator();

TEST(BaggagePropagatorTest, InjectBaggage)
{
  std::map<std::string, std::string> carrier;
  context::Context ctx = context::Context{};
  baggage_propagator.Inject(Setter, carrier,
                            ctx);  // for context with no baggage nothing is added in carrier
  EXPECT_EQ(carrier.count("baggage"), 0);

  nostd::shared_ptr<Baggage> baggage{new Baggage{}};
  baggage = baggage->Set("k1", "v1");
  baggage = baggage->Set("k2", "v2");
  ctx     = BaggageUtilities::SetBaggageInContext(baggage, ctx);
  baggage_propagator.Inject(Setter, carrier, ctx);
  EXPECT_EQ(carrier["baggage"], "k2=v2,k1=v1");
}

TEST(BaggagePropagatorTest, ExtractBaggage)
{
  std::map<std::string, std::string> carrier;
  context::Context ctx = context::Context{};

  baggage_propagator.Extract(Getter, carrier, ctx);  // for empty carrier, context is unchanged
  auto empty_baggage = BaggageUtilities::GetBaggageInContext(ctx);
  EXPECT_TRUE(empty_baggage == nullptr);

  carrier["baggage"]     = "k1=v1,k2=v2";
  carrier["traceparent"] = "parent";
  ctx                    = baggage_propagator.Extract(Getter, carrier, ctx);
  auto baggage           = BaggageUtilities::GetBaggageInContext(ctx);
  EXPECT_EQ(baggage->Get("k1"), "v1");
  EXPECT_EQ(baggage->Get("k2"), "v2");
  EXPECT_EQ(baggage->GetAll().size(), 2);
}
