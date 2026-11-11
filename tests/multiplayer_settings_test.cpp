#include "multiplayer_settings.h"
#include "default_player_names.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>
using namespace outrun::multiplayer;
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(){
  const auto dir=std::filesystem::current_path()/("mp-settings-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  try {
    Check(std::filesystem::create_directory(dir),"Exclusive test directory failed");
    const auto path=dir/"multiplayer-settings.toml";
    Check(!LoadPreferences(path),"Missing settings not handled");
    for(const char* bad:{"bad host","0.0.0.0","224.0.0.1","26.1.2","26.1.2.256","26.1.2.3\n","169.254.1.2","26.01.2.3"})
      Check(!ValidateHostAddress(bad).empty(),"Invalid host address accepted");
    for(const char* good:{"","26.1.2.3","192.168.1.2","127.0.0.1"})Check(ValidateHostAddress(good).empty(),"Valid manual/automatic host rejected");
    std::set<std::string_view> names;
    for (auto name:kDefaultPlayerNames) {
      Check(ValidateName(name).empty(),"Default name exceeds game constraints");
      Check(names.insert(name).second,"Duplicate pool entry biases selection");
    }
    const auto assigned=LoadOrCreatePreferences(path,"");
    Check(names.contains(assigned.name) && !assigned.showPing,"First launch did not use supplied pool");
    Check(*LoadPreferences(path)==assigned,"Assigned name not persisted");
    Check(LoadOrCreatePreferences(path,"Override")==assigned,"Restart changed saved name");
    for(int i=0;i<128;++i)Check(names.contains(RandomDefaultName()),"Random name outside supplied pool");
    std::filesystem::remove(path);
    Check(LoadOrCreatePreferences(path,"Existing Name").name=="Existing Name","Explicit config name lost");
    for(const char* bad:{"","                "," leading","trailing ","abcdefghijklmnop","bad\nname","a\"b","a\\b","ab#c","café"})
      Check(!ValidateName(bad).empty(),"Invalid name accepted");
    const Preferences original{"OutRun Join",false},changed{"Jay_12345678901",true};
    SavePreferences(path,original);
    Check(*LoadPreferences(path)==original,"Initial round trip failed");
    SavePreferences(path,changed);
    Check(*LoadPreferences(path)==changed,"Replacement/restart failed");
    Check(LoadOrCreatePreferences(path,"")==changed,"Custom name or ping replaced on restart");
    // Local name: optional in the file, up to 4 letters/numbers; blank follows the online name.
    Check(HighScoreInitials(changed)=="JAY1","Initials not taken from the online name");
    std::set<std::string_view> locals;
    for(size_t i=0;i<kDefaultLocalNames.size();++i){
      Check(!kDefaultLocalNames[i].empty() && ValidateLocalName(kDefaultLocalNames[i]).empty(),"Pool local name invalid");
      Check(locals.insert(kDefaultLocalNames[i]).second,"Duplicate pool local name");
      Check(HighScoreInitials(Preferences{std::string(kDefaultPlayerNames[i]),false})==kDefaultLocalNames[i],"Pool name not given its local name");
    }
    Check(HighScoreInitials(Preferences{"Turbo Dave",false})=="TDAV","Pool pairing changed");
    for(const char* bad:{"ABCDE","A B","A-B","\xe9"})Check(!ValidateLocalName(bad).empty(),"Invalid local name accepted");
    for(const char* good:{"","A","ab12","ZZZZ"})Check(ValidateLocalName(good).empty(),"Valid local name rejected");
    auto local=changed;local.localName="ab1";SavePreferences(path,local);
    Check(*LoadPreferences(path)==local && HighScoreInitials(local)=="AB1","Local name not persisted or used");
    {std::ofstream out(path);out<<"player_name = \"Old File\"\nshow_ping = false\nhost_address = \"\"\n";}
    Check(LoadPreferences(path)->localName.empty() && HighScoreInitials(*LoadPreferences(path))=="OLDF","File without local name rejected");
    SavePreferences(path,changed);
    auto manual=changed;manual.hostAddress="26.1.2.3";manual.hostAddressSpecified=true;SavePreferences(path,manual);
    Check(LoadPreferences(path)->hostAddress=="26.1.2.3"&&LoadPreferences(path)->hostAddressSpecified,"Manual host not persisted");
    SavePreferences(path,changed);
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(handle!=INVALID_HANDLE_VALUE,"Cannot lock destination");
    bool failed=false;try{SavePreferences(path,original);}catch(...){failed=true;}
    CloseHandle(handle);
    Check(failed && *LoadPreferences(path)==changed,"Failed save damaged previous settings");
    for(const auto& entry:std::filesystem::directory_iterator(dir))Check(entry.path()==path,"Temporary file leaked");
    for(const char* malformed:{"player_name = \"\"\nshow_ping = false\n","player_name = \"x\"\n", "player_name = \"x\"\nshow_ping = yes\n", "player_name = \"x\"\nshow_ping = false\nshow_ping = true\n"}) {
      {std::ofstream out(path);out<<malformed;}
      bool rejected=false;try{LoadPreferences(path);}catch(...){rejected=true;}
      Check(rejected,"Malformed settings accepted");
      bool initRejected=false;try{LoadOrCreatePreferences(path,"");}catch(...){initRejected=true;}
      Check(initRejected,"First-launch initialization overwrote malformed settings");
      std::ifstream preserved(path);std::string contents((std::istreambuf_iterator<char>(preserved)),{});
      Check(contents==malformed,"Malformed settings were modified");
    }
    std::filesystem::remove(path);std::filesystem::remove(dir);
    std::cout<<"PASS: supplied name pool, first-launch persistence, existing identity preservation, name limits, failed replacement preservation, malformed files\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<" ("<<dir<<")\n";return 1;}
}
