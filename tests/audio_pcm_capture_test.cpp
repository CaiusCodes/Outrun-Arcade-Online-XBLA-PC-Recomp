#include "audio_pcm_capture.h"
#include <rex/cvar.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>
#include <string_view>
#include <set>
REXCVAR_DECLARE(bool,outrun_audio_capture);
REXCVAR_DECLARE(bool,outrun_audio_capture_coasting);
REXCVAR_DECLARE(bool,outrun_audio_capture_rolling);
int main(int argc,char** argv) {
  using namespace outrun::diagnostics;
  REXCVAR_GET(outrun_audio_capture)=true;
  const bool coast=argc>1 && std::string_view(argv[1])=="--coast";
  const bool rolling=argc>1 && std::string_view(argv[1])=="--rolling";
  REXCVAR_GET(outrun_audio_capture_coasting)=coast;
  REXCVAR_GET(outrun_audio_capture_rolling)=rolling;
  std::vector<uint8_t> bytes(6200);for(size_t i=0;i<6144;++i)bytes[16+i]=uint8_t(i*17);
  ArmPCM(0x41,false,false);CapturePCM(0x41550000,16,bytes.data());
  ArmPCM(0x57,true,false);CapturePCM(0x41550000,16,bytes.data());
  ArmPCM(0x57,false,true);CapturePCM(0x41550000,16,bytes.data());
  ArmPCM(0x57,false,false);
  if(coast){
    CapturePCM(0x41550000,16,bytes.data()); // Acceleration must not start the capture.
    ArmPCMRelease(0x41,false,false);CapturePCM(0x41550000,16,bytes.data());
    ArmPCMRelease(0x57,true,false);CapturePCM(0x41550000,16,bytes.data());
    ArmPCMRelease(0x57,false,true);CapturePCM(0x41550000,16,bytes.data());
    ArmPCMRelease(0x57,false,false);
  }
  CapturePCM(0x41550000,0xffffffff,bytes.data()); // Overflow rejected before read.
  CapturePCM(0x41550000,16,bytes.data());
  CapturePCM(0x41550001,16,bytes.data()); // Another client cannot mix into capture.
  CapturePCM(0x41550000,16,bytes.data());
  if(rolling){
    // Cross the ring boundary: the saved result must contain the newest8192
    // original frames in chronological order, with no other-client frames.
    for(uint32_t i=0;i<8193;++i){
      const auto marker=1000+i;std::memcpy(bytes.data()+16,&marker,4);
      CapturePCM(0x41550000,16,bytes.data());
      CapturePCM(0x41550001,16,bytes.data());
    }
  }
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
  const auto dir=std::filesystem::path(exe).parent_path()/"logs";
  // Windows may reuse a PID from an earlier CTest run. Observe only files
  // created by this invocation; never remove previous diagnostic recordings.
  std::set<std::filesystem::path> previous;
  if(std::filesystem::exists(dir))
    for(const auto& file:std::filesystem::directory_iterator(dir))previous.insert(file.path());
  SavePCM();SavePCM();
  unsigned found=0;
  for(const auto& file:std::filesystem::directory_iterator(dir)) {
    if(previous.contains(file.path()))continue;
    if(!file.path().filename().string().starts_with("audio-capture-"+std::to_string(GetCurrentProcessId())+"-"))continue;
    ++found;std::ifstream input(file.path(),std::ios::binary);std::vector<uint8_t> data{std::istreambuf_iterator<char>(input),{}};
    const unsigned count=rolling?8192:2;
    if(data.size()!=20+count*(8+6144) || std::memcmp(data.data(),"ORPCM001",8))return 1;
    uint32_t format[3];std::memcpy(format,data.data()+8,12);
    if(format[0]!=48000 || format[1]!=6 || format[2]!=256)return 2;
    for(unsigned frame=0;frame<count;++frame){
      if(rolling){const uint32_t marker=1001+frame;std::memcpy(bytes.data()+16,&marker,4);}
      if(std::memcmp(data.data()+20+frame*6152+8,bytes.data()+16,6144))return 3;
    }
  }
  if(found!=1)return 4;
  std::cout<<"PASS: race-only W arming, read-only frame bytes, driver/overflow guards, PCM format and idempotent close save\n";
}
