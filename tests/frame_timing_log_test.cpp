#include "frame_timing_log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

REXCVAR_DEFINE_BOOL(vblank_timing_log,false,"Test","Timing diagnostic test gate");
int main() {
  const auto log=std::filesystem::current_path()/"frame-timing-idle-test.log";
  rex::InitLogging(log.string().c_str());
  rex::cvar::SetFlagByName("vblank_timing_log","true");
  auto close_start=std::chrono::steady_clock::now();
  {
    outrun::timing::Stream stream;
    stream.Tick("idle_test");
    // First report contains the one initial frame; the second must still be
    // emitted with zero frames even though Tick never runs again.
    std::this_thread::sleep_for(std::chrono::milliseconds(20300));
    close_start=std::chrono::steady_clock::now();
  }
  if(std::chrono::steady_clock::now()-close_start>std::chrono::seconds(1))
    throw std::runtime_error("Diagnostic shutdown waited for the ten-second timer");
  rex::FlushLogging();
  std::ifstream input(log);
  if(!input.is_open())throw std::runtime_error("Timing test log did not open");
  const std::string text{std::istreambuf_iterator<char>(input),{}};
  if(text.find("idle_test")==std::string::npos || text.find("count=0 fps=0.000")==std::string::npos)
    throw std::runtime_error("Stalled frame stream failed to report zero FPS");
  rex::ShutdownLogging();
  std::cout<<"PASS: an idle stream reports zero frames/FPS and shuts down promptly\n";
}
