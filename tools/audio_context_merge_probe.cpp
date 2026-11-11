#include <array>
#include <atomic>
#include <mutex>
#include <rex/kernel.h>
#include <rex/memory.h>
#include <rex/thread.h>
// Research-only access to the exact installed decoder's merge operation.
// No game is launched and no SDK source or guest installation is modified.
#include <rex/audio/xma/context.h>
#include <iostream>
struct PrivateMerge {
  using type=void(rex::audio::XmaContext::*)(const rex::audio::XMA_CONTEXT_DATA&,const rex::audio::XMA_CONTEXT_DATA&,uint8_t*);
  friend type Merge(PrivateMerge);
};
template<class Tag,typename Tag::type Method>struct MergeAccess{friend typename Tag::type Merge(Tag){return Method;}};
// Explicit instantiation permits private-member access without changing its
// access specifier (which is part of MSVC's decorated symbol).
template struct MergeAccess<PrivateMerge,&rex::audio::XmaContext::StoreContextMerged>;
int main(){
  using rex::audio::XMA_CONTEXT_DATA;
  std::array<uint8_t,64> memory{};
  XMA_CONTEXT_DATA initial(memory.data());
  initial.input_buffer_0_valid=1;initial.output_buffer_valid=1;
  initial.output_buffer_read_offset=3;initial.output_buffer_write_offset=4;
  initial.Store(memory.data());
  auto decoded=initial;decoded.output_buffer_write_offset=9;
  decoded.input_buffer_read_offset=2048;
  decoded.loop_count=5;decoded.input_buffer_0_valid=0;
  // Guest consumes output while the decoder works on its prior snapshot.
  auto guest=initial;guest.output_buffer_read_offset=7;guest.input_buffer_0_ptr=0x12345678;guest.stop_when_done=1;guest.Store(memory.data());
  rex::audio::XmaContext decoder;
  (decoder.*Merge(PrivateMerge{}))(decoded,initial,memory.data());
  XMA_CONTEXT_DATA merged(memory.data());
  std::cout<<"consumer read before merge=7; after merge="<<merged.output_buffer_read_offset
           <<"; producer write after merge="<<merged.output_buffer_write_offset<<'\n';
  if(merged.output_buffer_write_offset!=9 || merged.input_buffer_read_offset!=2048 || merged.loop_count!=5 || merged.input_buffer_0_valid || merged.input_buffer_0_ptr!=0x12345678 || !merged.stop_when_done)return 2;
  return merged.output_buffer_read_offset==7?0:1;
}
