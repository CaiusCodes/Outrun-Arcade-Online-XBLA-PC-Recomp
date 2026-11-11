#include "renderer_query_barrier.h"
#include "renderer_work_queue.h"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <iostream>
int main() {
  using namespace std::chrono_literals;
  try {
    int owner=0,other=0;
    outrun::RendererWorkQueue queue;
    std::promise<void> entered,release;
    auto gate=release.get_future().share();
    std::atomic<int> result{0};
    queue.Submit([&]{entered.set_value();gate.wait();result=42;});
    entered.get_future().wait();
    outrun::query_order::Register(&owner,[&]{queue.Drain();queue.CheckError();});
    // An unrelated backend must remain independent of this blocked worker.
    outrun::query_order::BeforeGuestWrite(&other);
    auto fence=std::async(std::launch::async,[&]{
      outrun::query_order::BeforeGuestWrite(&owner);
      if(result!=42)throw std::runtime_error("Completion overtook query result");
      result=100; // Simulate query-memory reuse or guest fence publication.
    });
    const bool blocked=fence.wait_for(30ms)==std::future_status::timeout;
    release.set_value();fence.get();
    if(!blocked || result!=100)throw std::runtime_error("Write ordering failed");
    outrun::query_order::Forget(&owner);
    outrun::query_order::BeforeGuestWrite(&owner);
    outrun::query_order::Register(&owner,[]{throw std::runtime_error("worker failed");});
    bool propagated=false;
    try{outrun::query_order::BeforeGuestWrite(&owner);}catch(...){propagated=true;}
    outrun::query_order::Forget(&owner);
    if(!propagated)throw std::runtime_error("Failure falsely acknowledged");
    std::cout<<"PASS: pending result before fence/reuse, unrelated owner, unregister, failure propagation\n";
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
