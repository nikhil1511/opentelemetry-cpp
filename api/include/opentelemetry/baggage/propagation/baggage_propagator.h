#pragma once

#include "opentelemetry/baggage/baggage.h"
#include "opentelemetry/baggage/baggage_utilities.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/trace/propagation/http_text_format.h"

#include <iostream>

OPENTELEMETRY_BEGIN_NAMESPACE

namespace baggage
{
namespace propagation
{

// Baggage propagator is responsible for handling propagation of baggage across process
// or any arbitary booundaries.
template <typename T>
class BaggagePropagator : public trace::propagation::HTTPTextFormat<T>
{
  using Base   = trace::propagation::HTTPTextFormat<T>;
  using Getter = typename Base::Getter;
  using Setter = typename Base::Setter;

public:
  static constexpr const char *kBaggageHeaderName = "baggage";

  // Inject baggage stored in context into carrier 
  void Inject(Setter setter, T &carrier, const context::Context &context) noexcept override
  {
    context::Context ctx(context);

    auto baggage = BaggageUtilities::GetBaggageInContext(ctx);

    if (!baggage)
    {
      return;
    }

    const auto &baggage_header = baggage->ToHeader();
    setter(carrier, kBaggageHeaderName, baggage_header);
  }

  // Extract baggage stored in carrier and set it in context.
  context::Context Extract(Getter getter,
                           const T &carrier,
                           context::Context &context) noexcept override
  {
    auto baggage_header = getter(carrier, kBaggageHeaderName);
    auto baggage        = Baggage::FromHeader(baggage_header);
    return BaggageUtilities::SetBaggageInContext(baggage, context);
  }
};

}  // namespace propagation
}  // namespace baggage

OPENTELEMETRY_END_NAMESPACE
