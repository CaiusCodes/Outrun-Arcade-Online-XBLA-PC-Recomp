#include <rex/runtime.h>
#include <rex/audio/xma/context.h>
#include <rex/logging.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

// Private read-only sound identification. Not part of any release allowlist.
static uint32_t LE(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static bool WriteWav(const std::filesystem::path& path,const std::vector<uint8_t>& pcm,uint32_t rate){
  if(std::filesystem::exists(path))return false;
  std::ofstream wav(path,std::ios::binary);
  auto number=[&](uint32_t value,unsigned count){for(unsigned i=0;i<count;++i)wav.put(char(value>>(8*i)));};
  wav.write("RIFF",4);number(uint32_t(pcm.size()+36),4);wav.write("WAVEfmt ",8);number(16,4);number(1,2);number(1,2);number(rate,4);number(rate*2,4);number(2,2);number(16,2);wav.write("data",4);number(uint32_t(pcm.size()),4);wav.write(reinterpret_cast<const char*>(pcm.data()),pcm.size());
  return bool(wav);
}
int main(int argc,char** argv){
  if(argc!=5 && argc!=6){std::cerr<<"assets bank sample output.wav [--verify-reuse]\n";return 1;}
  const bool verifyReuse=argc==6 && std::strcmp(argv[5],"--verify-reuse")==0;
  if(argc==6 && !verifyReuse)return 1;
  const auto output=std::filesystem::absolute(argv[4]);
  if(std::filesystem::exists(output))return 2;
  std::ifstream bank(argv[2],std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(bank),{}};
  const uint32_t wanted=std::stoul(argv[3]);size_t at=0;
  while(at+8<=bytes.size() && LE(bytes.data()+at)!=wanted){
    const auto size=LE(bytes.data()+at+4);
    if(size>bytes.size()-at-8)return 3;at+=8+size;
  }
  if(at+8>bytes.size())return 3;
  const auto size=LE(bytes.data()+at+4);at+=8;
  if(size>bytes.size()-at || size<12 || std::memcmp(bytes.data()+at,"RIFF",4))return 3;
  std::vector<uint8_t> format,encoded;
  for(size_t chunk=at+12;chunk+8<=at+size;){
    const auto length=LE(bytes.data()+chunk+4);
    if(length>at+size-chunk-8)return 3;
    if(!std::memcmp(bytes.data()+chunk,"fmt ",4))format.assign(bytes.begin()+chunk+8,bytes.begin()+chunk+8+length);
    if(!std::memcmp(bytes.data()+chunk,"data",4))encoded.assign(bytes.begin()+chunk+8,bytes.begin()+chunk+8+length);
    chunk+=8+length+(length&1);
  }
  if(format.size()!=32 || format[0]!=0x65 || format[1]!=1 || format[8]!=1 || format[9]!=0 || format[29]!=1 || encoded.empty() || encoded.size()%2048 || encoded.size()/2048>4095)return 4;
  const auto rate=LE(format.data()+16);if(rate!=32000)return 4;
  rex::InitLogging((output.string()+".log").c_str());
  rex::Runtime runtime(std::filesystem::absolute(argv[1]),output.parent_path()/"probe-userdata",{},output.parent_path()/"probe-cache");
  rex::RuntimeConfig config;config.tool_mode=true;
  if(runtime.Setup(std::move(config)))return 5;
  auto* memory=runtime.memory();
  const auto contextAddress=memory->SystemHeapAlloc(64);
  const auto inputAddress=memory->SystemHeapAlloc(uint32_t(encoded.size()),2048,rex::memory::kSystemHeapPhysical);
  const auto outputAddress=memory->SystemHeapAlloc(8192,256,rex::memory::kSystemHeapPhysical);
  if(!contextAddress || !inputAddress || !outputAddress)return 5;
  auto* contextBytes=memory->TranslateVirtual(contextAddress);
  std::memset(contextBytes,0,64);
  std::memcpy(memory->TranslatePhysical(inputAddress),encoded.data(),encoded.size());
  rex::audio::XmaContext decoder;
  if(decoder.Setup(0,memory,contextAddress))return 5;
  decoder.set_is_allocated(true);
  auto decode=[&](rex::audio::XmaContext& activeDecoder,std::vector<uint8_t>& pcm,bool interrupt=false,unsigned ringBlocks=31)->int {
  rex::audio::XMA_CONTEXT_DATA data(contextBytes);
  data.input_buffer_0_packet_count=uint32_t(encoded.size()/2048);
  data.input_buffer_0_valid=1;data.input_buffer_0_ptr=inputAddress;
  data.input_buffer_read_offset=32;data.sample_rate=1;
  const unsigned blocks=interrupt?3:ringBlocks;
  data.output_buffer_ptr=outputAddress;data.output_buffer_block_count=blocks;
  // Equal read/write offsets represent an empty ring. Reserve headroom on
  // complete decodes too: filling all 31 blocks made earlier probes silently
  // discard full laps and report only a short remainder of each sound.
  data.output_buffer_padding=1;
  data.output_buffer_valid=1;data.subframe_decode_count=1;data.Store(contextBytes);
  uint32_t previousBit=~0u;unsigned stalled=0;bool drained=false;
  for(unsigned pass=0;pass<10000;++pass){
    activeDecoder.Enable();activeDecoder.Work();data=rex::audio::XMA_CONTEXT_DATA(contextBytes);
    const unsigned read=data.output_buffer_read_offset*256,write=data.output_buffer_write_offset*256;
    const auto* buffer=memory->TranslatePhysical(outputAddress);
    for(unsigned pos=read;pos!=write;pos=(pos+2)%(blocks*256)){
      pcm.push_back(buffer[pos+1]);pcm.push_back(buffer[pos]);
    }
    if(interrupt && !pcm.empty())return 0;
    if(read==write && !data.IsAnyInputBufferValid()){drained=true;break;}
    if(data.error_status && data.error_status!=4)return 6;
    if(read==write && data.input_buffer_read_offset==previousBit)++stalled;else stalled=0;
    previousBit=data.input_buffer_read_offset;
    if(stalled>10){std::cerr<<"Decoder stalled\n";return 6;}
    // EOF may leave decoded subframes in the decoder. Drain those through its
    // documented consume-only context instead of truncating the sample tail.
    if(!data.IsAnyInputBufferValid())data.input_buffer_0_packet_count=data.input_buffer_1_packet_count=0;
    data.output_buffer_read_offset=data.output_buffer_write_offset;
    data.output_buffer_valid=1;data.Store(contextBytes);
  }
  if(!drained || pcm.empty())return 6;
  return 0;
  };
  std::vector<uint8_t> pcm;
  if(const int result=decode(decoder,pcm))return result;
  if(!WriteWav(output,pcm,rate))return 7;
  std::cout<<"sample "<<wanted<<": "<<pcm.size()/2<<" mono samples\n";
  if(verifyReuse){
    // Decoded PCM must not depend on how often this tool drains the ring.
    // This catches lost full laps, which same-capacity reuse comparisons miss.
    for(unsigned blocks:{7u,15u}){
      std::memset(contextBytes,0,64);
      rex::audio::XmaContext fresh;
      if(fresh.Setup(0,memory,contextAddress))return 5;
      fresh.set_is_allocated(true);
      std::vector<uint8_t> alternate;
      if(const int result=decode(fresh,alternate,false,blocks))return result;
      if(alternate!=pcm){std::cerr<<"Ring-size mismatch: "<<blocks<<" blocks, baseline="<<pcm.size()<<" alternate="<<alternate.size()<<" bytes\n";return 10;}
    }
    std::cout<<"Ring verification: 7/15/31-block decodes byte-identical\n";
    // Private deterministic isolation: both legitimate context reset paths,
    // with stale output bytes deliberately present. No game data is changed.
    bool mismatch=false;
    for(unsigned pass=0;pass<4;++pass){
      decoder.Clear();std::memset(contextBytes,0,64);
      std::vector<uint8_t> interrupted;
      if(const int result=decode(decoder,interrupted,true))return result;
      if(interrupted.size()>=pcm.size()){std::cerr<<"Sample too short to interrupt\n";return 9;}
      if(pass&1){decoder.Release();decoder.set_is_allocated(true);}
      else decoder.Clear();
      std::memset(contextBytes,0,64);
      std::memset(memory->TranslatePhysical(outputAddress),pass&1?0x7f:0xa5,8192);
      std::vector<uint8_t> replay;
      if(const int result=decode(decoder,replay))return result;
      if(replay!=pcm){
        size_t first=pcm.size(),last=0,count=0;
        for(size_t n=0;n<std::min(pcm.size(),replay.size());n+=2){
          if(pcm[n]!=replay[n] || pcm[n+1]!=replay[n+1]){first=std::min(first,n/2);last=n/2;++count;}
        }
        std::cerr<<"Reuse mismatch on pass "<<pass<<": baseline="<<pcm.size()<<" replay="<<replay.size()<<" bytes; differing samples="<<count<<" first="<<first<<" last="<<last<<"\n";
        if(!WriteWav(output.string()+".reuse-"+std::to_string(pass)+".wav",replay,rate))return 7;
        mismatch=true;
      }
    }
    if(mismatch)return 8;
    std::cout<<"Reuse verification: four interrupted, poisoned-buffer restarts byte-identical (Clear/Release)\n";
  }
  return 0;
}
