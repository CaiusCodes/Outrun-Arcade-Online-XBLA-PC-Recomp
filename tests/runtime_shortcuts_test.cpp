#include "runtime_shortcuts.h"
#include <rex/cvar.h>
#include <array>
#include <cstdlib>
#include <iostream>
int main(){
  using namespace rex::ui;
  // This test isolates the port's gate. Some SDK snapshots have an additional
  // default-off dispatcher gate; enable that only inside this test process.
  if(!rex::cvar::GetFlagByName("enable_rexglue_menus").empty())
    rex::cvar::SetFlagByName("enable_rexglue_menus","true");
  unsigned calls=0;
  const std::array keys{VirtualKey::kF3,VirtualKey::kF4,VirtualKey::kF7,VirtualKey::kOem3};
  const std::array names{"outrun_test_debug","outrun_test_settings","outrun_test_achievements","outrun_test_console"};
  const std::array defaults{"F3","F4","F7","Backtick"};
  for(size_t i=0;i<keys.size();++i)RegisterBind(names[i],defaults[i],"Test",[&]{++calls;});
  for(auto key:keys){
    KeyEvent disabled(nullptr,key,1,false,false,false,false,false);
    if(outrun::DispatchRuntimeShortcut(false,disabled) || disabled.is_handled() || calls)std::abort();
    KeyEvent enabled(nullptr,key,1,false,false,false,false,false);
    if(!outrun::DispatchRuntimeShortcut(true,enabled) || !enabled.is_handled())std::abort();
    calls=0;
  }
  rex::cvar::SetFlagByName(names[0],"W");
  KeyEvent rebound(nullptr,VirtualKey::kW,1,false,false,false,false,false);
  if(outrun::DispatchRuntimeShortcut(false,rebound) || rebound.is_handled() || calls)std::abort();
  KeyEvent close(nullptr,VirtualKey::kF4,1,false,false,false,true,false);
  if(outrun::DispatchRuntimeShortcut(false,close) || close.is_handled() || calls)std::abort();
  for(auto name:names)UnregisterBind(name);
  std::cout<<"PASS: SDK shortcuts and saved rebindings blocked; disabled dispatcher leaves input available\n";
}
