#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/sinks/base_sink.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

class SlowSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  std::atomic<unsigned> messages{0};
 protected:
  void sink_it_(const spdlog::details::log_msg&) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ++messages;
  }
  void flush_() override {}
};

int main() {
  if (!rex::cvar::SetFlagByName("log_async", "true")) return 77;
  auto sink = std::make_shared<SlowSink>();
  rex::LogConfig config;
  config.category_sinks["async-check"] = {sink};
  config.category_sinks_exclusive = true;
  rex::InitLogging(config);
  auto logger = rex::GetLogger(rex::RegisterLogCategory("async-check"));
  const auto start = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 20; ++i) logger->info("message {}", i);
  const auto producer_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  rex::FlushLogging();
  if (sink->messages.load() != 20) {
    std::cerr << "FlushLogging returned before queued messages were written\n";
    return 1;
  }
  rex::ShutdownLogging();
  if (producer_ms > 200 || sink->messages.load() != 20) {
    std::cerr << "Async logging blocked the producer or lost messages\n";
    return 1;
  }
  std::cout << "Slow-sink check passed: " << producer_ms << "ms producer time; all 20 messages drained.\n";
}
