#include <rex/cvar.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/vulkan/provider.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
  try {
    rex::cvar::Init(argc,argv);
    const bool vulkan=argc>1 && std::string(argv[1])=="--vulkan";
    std::unique_ptr<rex::ui::GraphicsProvider> provider;
    if(vulkan)provider=rex::ui::vulkan::VulkanProvider::Create(false,true);
    else provider=rex::ui::d3d12::D3D12Provider::Create();
    if(!provider)throw std::runtime_error("Graphics provider unavailable");
    auto presenter=provider->CreatePresenter();
    if(!presenter)throw std::runtime_error("Filter shader/pipeline initialization failed");
    using Apply=bool(*)(void*);
    auto apply=reinterpret_cast<Apply>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),"outrun_apply_present_filter"));
    if(!apply)throw std::runtime_error("Spatial runtime bridge missing");
    using Effect=rex::ui::Presenter::GuestOutputPaintConfig::Effect;
    const Effect expected[]={Effect::kBilinear,Effect::kCas,Effect::kFsr};
    for(unsigned choice: {0u,1u,2u,0u}) {
      rex::cvar::SetFlagByName("outrun_present_filter",std::to_string(choice));
      if(!apply(presenter.get()) || presenter->GetGuestOutputPaintConfigFromUIThread().GetEffect()!=expected[choice])
        throw std::runtime_error("Live filter selection mismatch");
    }
    std::cout<<(vulkan?"Vulkan: ":"D3D12: ")<<"PASS: headless spatial shader initialization and Standard/CAS/FSR/Standard switching\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
