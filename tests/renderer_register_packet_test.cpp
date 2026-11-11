#include <rex/memory.h>
#include <rex/memory/ring_buffer.h>
#include <array>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace memory = rex::memory;
static bool bulk_enabled;
#define REXCVAR_GET(name) bulk_enabled
#define REXGPU_ERROR(...) ((void)0)
struct Trace {
  unsigned starts = 0, ends = 0, words = 0;
  void WritePacketStart(uint32_t, uint32_t count) { ++starts; words = count; }
  void WritePacketEnd() { ++ends; }
};
class CommandProcessor {
 public:
  virtual ~CommandProcessor() = default;
  Trace trace_writer_;
  std::vector<std::pair<uint32_t,uint32_t>> writes;
  void WriteRegister(uint32_t index, uint32_t value) { writes.emplace_back(index,value); }
  virtual void WriteRegistersFromMem(uint32_t, uint32_t*, uint32_t);
  void WriteRegisterRangeFromRing(memory::RingBuffer*, uint32_t, uint32_t);
  bool ExecutePacketType0(memory::RingBuffer*, uint32_t);
};
#include "renderer-register-test-methods.inc"
class Backend final : public CommandProcessor {
 public:
  unsigned ranges = 0;
  void WriteRegistersFromMem(uint32_t index, uint32_t* data, uint32_t count) override {
    ++ranges;
    CommandProcessor::WriteRegistersFromMem(index,data,count);
  }
};
int main() {
  std::mt19937 random(0x58410968);
  // Actual SDK RingBuffer exercises endian conversion and physical wrap.
  // Actual extracted packet and range methods exercise malformed packet handling,
  // tracing, virtual backend dispatch and repeated-register side effects.
  for (unsigned trial=0; trial<2000; ++trial) {
    alignas(4) std::array<uint8_t,512> a{}, b{};
    const unsigned offset = (trial%128)*4;
    const unsigned count = 1+random()%127;
    const bool repeated = trial%3 == 0;
    const bool truncated = trial%11 == 0;
    const unsigned available = count-(truncated ? 1 : 0);
    const uint32_t packet = ((count-1)<<16)|(uint32_t(repeated)<<15)|(random()&0x7FFF);
    for (auto& byte:a) byte=uint8_t(random());
    b=a;
    memory::RingBuffer original(a.data(),a.size()), optimized(b.data(),b.size());
    for (auto* ring:{&original,&optimized}) {
      ring->set_read_offset(offset);
      ring->set_write_offset((offset+available*4)%a.size());
    }
    Backend scalar, bulk;
    bulk_enabled=false;
    const bool scalar_ok=scalar.ExecutePacketType0(&original,packet);
    bulk_enabled=true;
    const bool bulk_ok=bulk.ExecutePacketType0(&optimized,packet);
    if (scalar_ok!=bulk_ok || scalar_ok==truncated || scalar.writes!=bulk.writes ||
        original.read_offset()!=optimized.read_offset() ||
        scalar.trace_writer_.starts!=bulk.trace_writer_.starts ||
        scalar.trace_writer_.ends!=bulk.trace_writer_.ends ||
        scalar.trace_writer_.words!=bulk.trace_writer_.words ||
        ((repeated||truncated) && bulk.ranges) ||
        (!repeated && !truncated && !bulk.ranges)) {
      std::cerr<<"Mismatch in trial "<<trial<<'\n'; return 1;
    }
  }
  std::cout<<"PASS: 2000 differential Type-0 packets, including wrapped, repeated and truncated payloads\n";
}
