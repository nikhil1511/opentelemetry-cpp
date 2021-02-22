#pragma once

#include "opentelemetry/context/context.h"
#include "opentelemetry/baggage/baggage.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace baggage {


// Class to provide functionalities for context interaction of baggage.
class BaggageUtilities
{
public:
    // Extracts baggage from the context and return it. 
    static nostd::shared_ptr<Baggage> GetBaggageInContext(context::Context& context)
    {
        if (!context.HasKey(kBaggageKey)) {
            return nostd::shared_ptr<Baggage>(nullptr);
        }

        return nostd::get<nostd::shared_ptr<Baggage>>(context.GetValue(kBaggageKey));
    }

    // Insert the Baggage to a Context instance and returns new context object with the given baggage.
    static context::Context SetBaggageInContext(nostd::shared_ptr<Baggage> baggage, context::Context& context)
    {
        return context.SetValue(kBaggageKey, baggage);
    }

    // Removes all baggage entries from the context and returns a new context with no baggage entries.
    static context::Context ClearBaggageInContext(context::Context& context)
    {
        nostd::shared_ptr<Baggage> null_baggage(nullptr);
        return SetBaggageInContext(null_baggage, context);
    }

private:
    static constexpr const char* kBaggageKey = "baggage";
};

} //baggage
OPENTELEMETRY_END_NAMESPACE