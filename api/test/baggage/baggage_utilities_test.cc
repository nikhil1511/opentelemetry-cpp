#include "opentelemetry/baggage/baggage_utilities.h"

using namespace opentelemetry;
using namespace opentelemetry::baggage;

#include <gtest/gtest.h>

TEST(BaggageUtilitiesTest, GetBaggageInContext)
{
    context::Context ctx = context::Context{};
    auto empty_baggage = BaggageUtilities::GetBaggageInContext(ctx);
    EXPECT_EQ(empty_baggage, nullptr);
    nostd::shared_ptr<Baggage> baggage(new Baggage{});
    baggage = baggage->Set("k1", "v1");
    ctx = ctx.SetValue("baggage", baggage);
    auto baggage_in_context = BaggageUtilities::GetBaggageInContext(ctx);
    ASSERT_TRUE(baggage_in_context != nullptr);
    EXPECT_EQ(baggage_in_context->Get("k1"), "v1");
}

TEST(BaggageUtilitiesTest, SetBaggageInContext)
{
    context::Context ctx = context::Context{};
    nostd::shared_ptr<Baggage> baggage(new Baggage{});
    baggage = baggage->Set("k1", "v1");
    ctx = BaggageUtilities::SetBaggageInContext(baggage, ctx);
    auto baggage_in_context = nostd::get<nostd::shared_ptr<Baggage>>(ctx.GetValue("baggage"));
    EXPECT_EQ(baggage_in_context, baggage);
}

TEST(BaggageUtilitiesTest, ClearBaggageInContext)
{
    context::Context ctx = context::Context{};
    nostd::shared_ptr<Baggage> baggage(new Baggage{});
    baggage = baggage->Set("k1", "v1");
    ctx = ctx.SetValue("baggage", baggage);
    ctx = BaggageUtilities::ClearBaggageInContext(ctx);
    EXPECT_TRUE(BaggageUtilities::GetBaggageInContext(ctx) == nullptr);
}


