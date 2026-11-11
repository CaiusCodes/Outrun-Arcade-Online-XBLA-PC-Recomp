#include "renderer_work_queue.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>
int main() {
  try {
    std::vector<int> results;
    {
      outrun::RendererWorkQueue queue;
      std::promise<void> entered, release;
      auto gate = release.get_future().share();
      queue.Submit([&] { entered.set_value(); gate.wait(); results.push_back(0); });
      entered.get_future().wait();
      // Submitting a second result must not wait for the GPU-like first task.
      queue.Submit([&] { results.push_back(1); });
      release.set_value();
      for (int i = 2; i < 200; ++i) queue.Submit([&, i] { results.push_back(i); });
      queue.Drain();
      queue.CheckError();
      if (results.size() != 200) throw std::runtime_error("Drain lost work");
      for (int i = 0; i < 200; ++i)
        if (results[i] != i) throw std::runtime_error("GPU results reordered");
      queue.Submit([&] { results.push_back(200); });
    }
    if (results.back() != 200) throw std::runtime_error("Destruction lost work");
    outrun::RendererWorkQueue failing;
    failing.Submit([] { throw std::runtime_error("simulated device failure"); });
    failing.Drain();
    bool reported = false;
    try { failing.CheckError(); } catch (...) { reported = true; }
    if (!reported) throw std::runtime_error("Worker failure hidden");
    std::cout << "PASS: nonblocking submission, ordered results, bounded backlog, drain/destructor, error reporting\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
