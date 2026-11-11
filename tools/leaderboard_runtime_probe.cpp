#include "generated/default/outrun_init.h"
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>

void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
  if(argc!=3 && argc!=4)return 2;
  try {
    const auto root=std::filesystem::absolute(argv[2]);
    Check(std::filesystem::create_directory(root),"Use a new isolated output directory");
    const bool populated=argc==4 && std::string_view(argv[3])=="--populated";
    if(populated) {
      // Synthetic fixture lives only in a freshly created probe directory.
      // Never write test scores into an existing installation's user data.
      std::filesystem::create_directories(root/"userdata/leaderboards");
      std::ofstream fixture(root/"userdata/leaderboards/58410968.txt");
      fixture<<"v 33 1234 123456 1 51412054657374 0\n";
      Check(bool(fixture),"Could not create isolated fixture");
    }
    rex::InitLogging((root/"runtime.log").string().c_str());
    rex::Runtime runtime(std::filesystem::absolute(argv[1]),root/"userdata",{},root/"cache");
    rex::RuntimeConfig config;config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
    Check(runtime.Setup(std::move(config))==0,"Runtime setup failed");
    Check(runtime.LoadXexImage("game:\\default.xex")==0,"Title metadata load failed");
    auto* base=runtime.memory()->virtual_membase();
    const auto memory=runtime.memory()->SystemHeapAlloc(4096);
    Check(memory!=0,"Guest allocation failed");
    for(uint32_t type: {0u,1u,2u}) {
      PPCContext ctx{};ctx.r1.u64=memory+3072;
      REX_STORE_U32(memory,33);REX_STORE_U32(memory+4,0);
      ctx.r3.u64=0x58410968;ctx.r4.u64=type;
      ctx.r5.u64=type==0 ? 0x1234 : type==2 ? 123456 : 1;ctx.r6.u64=5;
      ctx.r7.u64=1;ctx.r8.u64=memory;ctx.r9.u64=memory+200;ctx.r10.u64=memory+204;
      __imp__XamUserCreateStatsEnumerator(ctx,base);
      Check(ctx.r3.u32==0,"Enumerator creation failed");
      const auto expectedSize=populated ? 72u : 24u;
      Check(REX_LOAD_U32(memory+200)==expectedSize,"Unexpected stats result size");
      auto e=runtime.kernel_state()->object_table()->LookupObject<rex::system::XEnumerator>(REX_LOAD_U32(memory+204));
      Check(bool(e) && e->item_size()==expectedSize,"Invalid handle/size");
      uint32_t count=0;const auto result=memory+256;
      Check(e->WriteItems(result,base+result,&count)==0 && count==1,"First enumeration failed");
      const auto view=REX_LOAD_U32(result+4);
      Check(REX_LOAD_U32(result)==1 && view==result+8 && REX_LOAD_U32(view)==33,"Invalid board pointer");
      if(populated) {
        Check(REX_LOAD_U32(view+4)==1 && REX_LOAD_U32(view+8)==1,"Existing row missing");
        const auto row=REX_LOAD_U32(view+12);
        Check(row==result+24 && REX_LOAD_U64(row)==0x1234 && REX_LOAD_U64(row+16)==123456,
              "Existing score changed");
      } else {
        Check(REX_LOAD_U32(view+4)==0 && REX_LOAD_U32(view+8)==0 && REX_LOAD_U32(view+12)==0,"Invented rows");
      }
      count=0;
      Check(e->WriteItems(result,base+result,&count)==18 && count==0,"Enumeration end not reported");
      Check(e->Delete()==0,"Handle cleanup failed");
    }
    std::cout<<"Real runtime: "<<(populated ? "existing score preservation" : "empty board adaptation")
             <<", rank/XUID/rating queries, guest pointers, end and cleanup passed\n";
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
