#include "opentelemetry/trace/propagation/http_text_format.h"
#include "opentelemetry/context/context.h"
#include <iostream>
#include <variant>
#include <sstream>
#include <vector>


namespace opentelemetry {
namespace baggage {

std::vector<std::string> split(const std::string &s, char delim) {
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delim)) {
        elems.push_back(std::move(item));
    }
    return elems;
}


template <typename T>
class BaggagePropagator : public trace::propagation::HTTPTextFormat<T>
{
    using Base = trace::propagation::HTTPTextFormat<T>;
    using Getter = typename Base::Getter;
    using Setter = typename Base::Setter;

public:
  static constexpr const char* BAGGAGE_KEY = "baggage";

  static std::string formatBaggage(const std::unordered_map<std::string, std::string>& baggageMap)
  {
      // TODO : add logic to handle url keys, values.
      std::string baggageStr;
      for (auto& entry : baggageMap) {
          baggageStr += entry.first + "=" + entry.second;
          baggageStr += ",";
      }

      baggageStr.pop_back();
      return baggageStr;
  }

  // we store baggage headers in context.
  void Inject(Setter setter, T &carrier, const context::Context &context) noexcept override
  {
    // get all baggage keys from context.
    // make a string from all those key value pairs
    // set them in carrier. 
    std::cout << "In inject" << std::endl;   
    if (!context.HasKey(BAGGAGE_KEY)) {
        std::cout << "context does not have baggage key" << std::endl;
        return;
    }

    const auto& baggageMap = *nostd::get<nostd::shared_ptr<std::unordered_map<std::string, std::string>>>(context.GetValue(BAGGAGE_KEY)).get();
    if (baggageMap.size() == 0) {
        std::cout << "context has empty map for baagage key" << std::endl;
        return;
    }

    const auto& baggageStr = formatBaggage(baggageMap);
    setter(carrier, BAGGAGE_KEY, baggageStr);

    std::cout << "Inject successful" << std::endl;

  }

  context::Context Extract(Getter getter,
                           const T &carrier,
                           context::Context &context) noexcept override
  {
    // get baggage header key from the carrier using getter
    // split string into key, value pairs
    // set them ito context.

    std::cout << "In extract" << std::endl;

    // TODO : assuming context is always passed. Else, we need to fetch the current context. As, it is reference
    // it will always be a valid context
    const auto& baggageHeader = getter(carrier, BAGGAGE_KEY);
    std::cout << "BaggageHeader is: " << baggageHeader << std::endl;
    const auto& baggageKeys = baggage::split({baggageHeader.begin(), baggageHeader.end()}, ',');
    std::unordered_map<std::string, std::string> baggageMap;
    for (auto& entry : baggageKeys) {
        const auto& keyVal = baggage::split(entry, '=');
        baggageMap[keyVal[0]] = keyVal[1];
    }

    auto ptr = nostd::shared_ptr<std::unordered_map<std::string, std::string>>(std::make_shared<std::unordered_map<std::string, std::string>>(baggageMap));

    return context.SetValue(BAGGAGE_KEY, ptr);
    
  }


};

} // baggage
} // opentelemtry
