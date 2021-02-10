//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "opentelemetry/cpp-httplib/httplib.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/baggage/baggage_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include <map>
#include <string>
#include <memory>
#include <chrono>
#include <thread>

namespace trace = opentelemetry::trace;
namespace context = opentelemetry::context;
namespace nostd = opentelemetry::nostd;

static nostd::string_view Getter(const httplib::Headers &carrier,
                                 nostd::string_view trace_type = "traceparent")
{
  auto it = carrier.find(std::string(trace_type));
  if (it != carrier.end())
  {
    return nostd::string_view(it->second);
  }
  return "";
}

static void Setter(httplib::Headers &carrier,
                   nostd::string_view trace_type        = "traceparent",
                   nostd::string_view trace_description = "")
{
  std::cout << "setter called with trace_type: " << trace_type << " and description: " << trace_description << std::endl; 
  carrier.insert({std::string(trace_type), std::string(trace_description)});
}

namespace
{
nostd::shared_ptr<trace::Tracer> get_tracer()
{
  auto provider = trace::Provider::GetTracerProvider();
  return provider->GetTracer("foo_library");
}

void f1()
{
  auto span  = get_tracer()->StartSpan("f1");
  auto scope = get_tracer()->WithActiveSpan(span);

  span->End();
}

void f2()
{
  std::map<std::string, std::string> attrs;
  attrs["key"] = "value";
  auto span  = get_tracer()->StartSpan("f2", attrs);
  span->SetAttribute("key", "value2");
  span->AddEvent("even1");
  auto scope = get_tracer()->WithActiveSpan(span);

  f1();
  f1();

  span->End();
}
}  // namespace


using MapBaggagePropagator = opentelemetry::baggage::BaggagePropagator<httplib::Headers>;

void sendRequest(MapBaggagePropagator& propagator) {
  // Upstream case
  // inject
  auto currentContext = context::RuntimeContext::GetCurrent();
  std::unordered_map<std::string, std::string> dummyMap;
  dummyMap["key"] = "value"; 
  auto stdBaggageKeys = std::make_shared<std::unordered_map<std::string, std::string>>(dummyMap);
  auto baggageKeys = nostd::shared_ptr<std::unordered_map<std::string, std::string>>(stdBaggageKeys);
  //auto newContext = currentContext.SetValue("baggage", baggageKeys);
  httplib::Headers injectHeaders = {};
  propagator.Inject(Setter, injectHeaders, currentContext);

  // send too downstream python
  httplib::Client cli("http://localhost:90");

  auto res = cli.Get("/", injectHeaders);
  if (!res) {
    std::cout << "Res error is: " << res.error() << std::endl;
  } else {
    std::cout << "Status is: " << res->status << std::endl;
  }

}

void foo_library()
{
  auto propagator = MapBaggagePropagator();

  

  // doownstream case
  // http server
  httplib::Headers headers;
  httplib::Server svr;
  svr.Get("/hi", [&](const httplib::Request &req, httplib::Response &res) {
    auto span  = get_tracer()->StartSpan("library");
    auto scope = get_tracer()->WithActiveSpan(span);
    headers = req.headers;
    std::cout << "Printing headers" << std::endl;
    for (auto elem : headers) {
      std::cout << elem.first << " " << elem.second << std::endl; 
    }

    std::cout << "End of headers printing" << std::endl; 
    // extract
    auto currentExtractContext = context::RuntimeContext::GetCurrent();
    auto newExtractContext = propagator.Extract(Getter, headers, currentExtractContext);
    auto baggageMaps = *nostd::get<nostd::shared_ptr<std::unordered_map<std::string, std::string>>>(newExtractContext.GetValue("baggage")).get();
    std::cout << "BaggageMaps vals:" << std::endl;
    for (auto entry : baggageMaps) {
      std::cout << entry.first << " " << entry.second << std::endl;
    }
    res.set_content("Hello World!", "text/plain");
    auto token = context::RuntimeContext::Attach(newExtractContext);
    f2();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    sendRequest(propagator);

    context::RuntimeContext::Detach(*token.get());
    span->End();

  });
  svr.listen("0.0.0.0", 80);
}
