#include "audio_pcm_capture.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <mutex>
#include <vector>
#include <array>
#include <filesystem>
#include <fstream>
#include <algorithm>
REXCVAR_DEFINE_BOOL(outrun_audio_capture,false,"OutRun",
                   "Capture up to 44 seconds of game PCM locally for steering-pop diagnosis")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_capture_coasting,false,"OutRun",
                   "Diagnostic: begin PCM capture on W release instead of acceleration")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_capture_rolling,false,"OutRun",
                   "Diagnostic: retain the latest 44 seconds instead of the first")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace outrun::diagnostics {
namespace {
constexpr size_t payload=6*256*4,recordSize=8+payload,maxFrames=8192;
std::mutex pcmMutex;
std::vector<uint8_t> pcm;
bool armed=false,finished=false;
uint32_t client=~0u;
size_t nextFrame=0;
void BeginCapture(const char* trigger) {
  std::lock_guard lock(pcmMutex);
  if(armed || finished)return;
  try{pcm.reserve(maxFrames*recordSize);armed=true;REXLOG_INFO("[audio_capture] Armed on {}; read-only PCM capture, close normally to save",trigger);}
  catch(const std::exception& e){finished=true;REXLOG_WARN("[audio_capture] Disabled: {}",e.what());}
}
}
void ArmPCM(uint16_t key,bool menu,bool title) {
  if(!REXCVAR_GET(outrun_audio_capture) || REXCVAR_GET(outrun_audio_capture_coasting) || key!=0x57 || menu || title)return;
  BeginCapture("W press");
}
void ArmPCMRelease(uint16_t key,bool menu,bool title) {
  if(!REXCVAR_GET(outrun_audio_capture) || !REXCVAR_GET(outrun_audio_capture_coasting) || key!=0x57 || menu || title)return;
  BeginCapture("W release (coasting)");
}
void CapturePCM(uint32_t driver,uint32_t address,const uint8_t* base) {
  if(!REXCVAR_GET(outrun_audio_capture))return;
  std::lock_guard lock(pcmMutex);
  if(!armed || finished || (driver&0xffff0000)!=0x41550000)return;
  if(client==~0u)client=driver;
  if(client!=driver || uint64_t(address)+payload>0x100000000ull)return;
  const bool full=pcm.size()/recordSize>=maxFrames;
  if(full && !REXCVAR_GET(outrun_audio_capture_rolling)){armed=false;finished=true;return;}
  // Exact shipped driver input format: 6 planar channels, 256 BE float32
  // samples/channel at48kHz. Copy only; never alter the original guest buffer.
  const uint64_t time=AudioTraceTime();
  const auto* stamp=reinterpret_cast<const uint8_t*>(&time);
  if(full){
    auto* destination=pcm.data()+nextFrame*recordSize;
    std::copy_n(stamp,8,destination);
    std::copy_n(base+address,payload,destination+8);
    nextFrame=(nextFrame+1)%maxFrames;
  }else{
    pcm.insert(pcm.end(),stamp,stamp+8);
    pcm.insert(pcm.end(),base+address,base+address+payload);
  }
}
void SavePCM() {
  if(!REXCVAR_GET(outrun_audio_capture))return;
  std::vector<uint8_t> output;
  size_t oldest=0;
  {std::lock_guard lock(pcmMutex);armed=false;finished=true;output.swap(pcm);oldest=nextFrame;}
  if(output.empty())return;
  // Save chronological frames after a wrap. This occurs only on shutdown,
  // outside the render callback; the guest buffer is always read-only.
  if(oldest)std::rotate(output.begin(),output.begin()+oldest*recordSize,output.end());
  try {
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto dir=std::filesystem::path(exe).parent_path()/"logs";
    std::filesystem::create_directories(dir);
    const auto path=dir/("audio-capture-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".bin");
    std::ofstream file(path,std::ios::binary);
    file.write("ORPCM001",8);const std::array<uint32_t,3> format{48000,6,256};
    file.write(reinterpret_cast<const char*>(format.data()),12);
    file.write(reinterpret_cast<const char*>(output.data()),output.size());file.flush();
    if(!file)throw std::runtime_error("PCM write failed");
    REXLOG_INFO("[audio_capture] Saved {} frames to {}",output.size()/recordSize,path.string());
  }catch(const std::exception& e){REXLOG_WARN("[audio_capture] Save failed: {}",e.what());}
}
}
