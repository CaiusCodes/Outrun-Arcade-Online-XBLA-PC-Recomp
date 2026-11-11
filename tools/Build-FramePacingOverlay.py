"""OutRun-local timing overlay. Reuses the tested compatibility object cohort.
Never writes to the sibling SDK. Stages: diagnostics, vsync, timer, redraw.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import importlib.util
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/Build-RendererOverlay.py')
helper=importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
edge,run_rsp=helper.edge,helper.run_rsp

def replace(source,old,new):
    if source.count(old)!=1: raise ValueError('SDK anchor changed: '+old[:100])
    return source.replace(old,new)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',choices=['diagnostics','vsync','timer','redraw'],required=True)
    p.add_argument('--sdk',type=Path,default=ROOT.parent/'rexglue-sdk-0.9.0')
    a=p.parse_args(); sdk=a.sdk.resolve(); build=sdk/'out/build/win-amd64'
    out=ROOT/'out/frame-pacing'/a.stage; out.mkdir(parents=True,exist_ok=True)
    ninja=(build/'CMakeFiles/impl-Release.ninja').read_text()
    clang=Path('C:/Program Files/LLVM/bin/clang++.exe')
    level=['diagnostics','vsync','timer','redraw'].index(a.stage)
    sources={}
    gs=(sdk/'src/graphics/graphics_system.cpp').read_text()
    gs='#include "frame_timing_log.h"\n'+gs
    gs=replace(gs,'REXCVAR_DEFINE_STRING(trace_gpu_prefix,',
      'REXCVAR_DEFINE_BOOL(vblank_timing_log, false, "GPU/Diagnostics", "Log vblank, host present and guest frame spacing every 10 seconds");\nREXCVAR_DEFINE_STRING(trace_gpu_prefix,')
    gs=replace(gs,'            MarkVblank();','            static outrun::timing::Stream timing; timing.Tick("vblank");\n            MarkVblank();')
    if level>=2:
      gs=replace(gs,'        while (vsync_worker_running_) {\n          apply_scale();',
        '''        HANDLE wait_timer = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        uint64_t interval_ticks = (REXCVAR_GET(vsync) || REXCVAR_GET(fixed_guest_vblank))
            ? vsync_interval_ticks : no_vsync_interval_ticks;
        const uint64_t spin_ticks = std::max(uint64_t(1), guest_tick_frequency/1000);
        while (vsync_worker_running_) {''')
      gs=replace(gs,'          uint64_t interval_ticks = (REXCVAR_GET(vsync) || REXCVAR_GET(fixed_guest_vblank))\n                                        ? vsync_interval_ticks\n                                        : no_vsync_interval_ticks;', '')
      gs=replace(gs,'            last_frame_time += interval_ticks;','''            last_frame_time += interval_ticks;
            // Preserve the existing catch-up and live network clock scaling.
            apply_scale();
            interval_ticks = (REXCVAR_GET(vsync) || REXCVAR_GET(fixed_guest_vblank))
                ? vsync_interval_ticks : no_vsync_interval_ticks;''')
      gs=replace(gs,'          rex::thread::Sleep(std::chrono::milliseconds(1));','''          const uint64_t elapsed = chrono::Clock::QueryGuestTickCount()-last_frame_time;
          if (elapsed >= interval_ticks) continue;
          const uint64_t remaining = interval_ticks-elapsed;
          if (remaining > spin_ticks) {
            const uint64_t sleep_ticks = remaining-spin_ticks;
            LARGE_INTEGER due;
            due.QuadPart = -std::max(int64_t(1), int64_t(double(sleep_ticks)*10000000.0/guest_tick_frequency));
            if (wait_timer && SetWaitableTimer(wait_timer,&due,0,nullptr,nullptr,FALSE))
              WaitForSingleObject(wait_timer,INFINITE);
            else rex::thread::Sleep(std::chrono::microseconds(sleep_ticks*1000000ull/guest_tick_frequency));
          } else rex::thread::MaybeYield();''')
      gs=replace(gs,'        return 0;\n      }));','        if (wait_timer) CloseHandle(wait_timer);\n        return 0;\n      }));')
    sources['src/graphics/graphics_system.cpp']=gs
    # Preserve the already-tested bulk register optimization verbatim.
    base=(ROOT/'out/renderer-race-bulk-20260920/base_command_processor.cpp').read_text()
    base='#include "frame_timing_log.h"\n'+base
    base=replace(base,'  guest_frame_count_.fetch_add(1, std::memory_order_relaxed);',
      '  static outrun::timing::Stream timing; timing.Tick("guest_swap_gpu");\n  guest_frame_count_.fetch_add(1, std::memory_order_relaxed);')
    sources['src/graphics/command_processor.cpp']=base
    presenter=(sdk/'src/ui/d3d12/d3d12_presenter.cpp').read_text()
    presenter='#include "frame_timing_log.h"\n'+presenter
    presenter=replace(presenter,'  // Even if presentation has failed,',
      '  if (SUCCEEDED(present_result)) { static outrun::timing::Stream timing; timing.Tick("host_present"); }\n  // Even if presentation has failed,')
    if level>=1:
      presenter=replace(presenter,'paint_context_.swap_chain_allows_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0',
        'paint_context_.swap_chain_allows_tearing && rex::cvar::GetFlagByName("vsync") != "true" ? DXGI_PRESENT_ALLOW_TEARING : 0')
    sources['src/ui/d3d12/d3d12_presenter.cpp']=presenter
    video=(sdk/'src/kernel/xboxkrnl/xboxkrnl_video.cpp').read_text()
    video='#include "frame_timing_log.h"\n'+video
    video=replace(video,'  // All of these parameters are REQUIRED.',
      '  static outrun::timing::Stream timing; timing.Tick("guest_vdswap");\n  // All of these parameters are REQUIRED.')
    sources['src/kernel/xboxkrnl/xboxkrnl_video.cpp']=video
    if level>=3:
      headers={}
      h='include/rex/ui/imgui_dialog.h'
      headers[h]=replace((sdk/h).read_text(),
        '  virtual void OnDraw(ImGuiIO& io) { (void)io; }',
        '  virtual void OnDraw(ImGuiIO& io) { (void)io; }\n public:\n  virtual bool WantsContinuousRedraw() const { return true; }')
      h='include/rex/ui/overlay/debug_overlay.h'
      s=(sdk/h).read_text(); start=s.index('class FpsOverlayDialog')
      s=s[:start]+s[start:].replace(' protected:',
        '  bool WantsContinuousRedraw() const override { return false; }\n\n protected:',1)
      headers[h]=s
      h='include/rex/ui/overlay/achievement_toast.h'
      headers[h]=replace((sdk/h).read_text(),'  std::mutex mutex_;','  mutable std::mutex mutex_;').replace(
        '  void Push(const rex::system::AchievementEvent& event) override;',
        '  void Push(const rex::system::AchievementEvent& event) override;\n  bool WantsContinuousRedraw() const override { std::lock_guard lock(mutex_); return !queue_.empty(); }')
      for h,s in headers.items():
        path=out/h; path.parent.mkdir(parents=True,exist_ok=True); path.write_text(s)
      # Recompile every SDK TU that instantiates a dialog vtable, including XAM
      # and the app overlay factory, with the matching expanded interface.
      import re
      paths=re.findall(r'^build .*?: CXX_COMPILER__\S+_unscanned_Release (.+?) \|\|',ninja,re.M)
      for path in paths:
        path=Path(path.replace('$:',':'))
        if path.is_relative_to(sdk/'src/ui') or path==sdk/'src/kernel/xam/xam_ui.cpp':
          relative=path.relative_to(sdk).as_posix()
          if relative not in sources: sources[relative]=path.read_text()
      sources['src/ui/imgui_drawer.cpp']=replace(sources['src/ui/imgui_drawer.cpp'],
        '  if (!dialogs_.empty()) {\n    // Repaint',
        '  if (std::any_of(dialogs_.begin(), dialogs_.end(), [](const auto* dialog) { return dialog->WantsContinuousRedraw(); })) {\n    // Repaint')
    # Existing D3D12 custom renderer is kept byte-for-byte as an object input.
    replacements={'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/d3d12/command_processor.cpp.obj':
        ROOT/'out/renderer-race-bulk-20260920/command_processor.cpp.obj'}
    manifest={'stage':a.stage,'sdk':str(sdk),'inputs':[],'outputs':{}}
    for relative,source in sources.items():
      name=Path(relative).stem+'-'+Path(relative).parent.name
      patched=out/(name+'.cpp'); patched.write_text(source,encoding='utf-8')
      # Find the unique compilation edge by its absolute source input.
      header,settings=edge(ninja,'_unscanned_Release '+str(sdk/relative).replace('\\','/').replace(':','$:')+' ||')
      objname=header.split(': CXX_COMPILER__',1)[0]
      obj=out/(name+'.obj')
      args=['-I'+str(out/'include')] if level>=3 else []
      for key in ('DEFINES','INCLUDES','FLAGS'): args+=shlex.split(settings[key])
      args+=['-I'+str(ROOT/'src'),'-I'+str((sdk/relative).parent)]
      run_rsp(clang,args+['-c',str(patched),'-o',str(obj)],'compile-'+name,out,build)
      replacements[objname]=obj
      manifest['inputs'].append({'path':str(sdk/relative),'sha256':hashlib.sha256((sdk/relative).read_bytes()).hexdigest()})
    for target in ['rexruntime','rexgpu-xenos']:
      header,settings=edge(ninja,': CXX_SHARED_LIBRARY_LINKER__'+target+'_Release ')
      explicit=header.split(': CXX_SHARED_LIBRARY_LINKER__'+target+'_Release ',1)[1].split(' |',1)[0]
      objects=[]
      for name in explicit.split():
        path=replacements.get(name,(build/name.replace('$:',':')).resolve())
        if not path.is_file(): raise ValueError('Missing object '+str(path))
        objects.append(str(path))
      args=['-nostartfiles','-nostdlib']
      for key in ('LANGUAGE_COMPILE_FLAGS','LINK_FLAGS'): args+=shlex.split(settings[key].replace('\\','/'))
      args+=['-o',str(out/(target+'.dll')),'-Xlinker','/MANIFEST:EMBED','-Xlinker','/implib:'+str(out/(target+'.lib')),'-Xlinker','/pdb:'+str(out/(target+'.pdb')),'-Xlinker','/version:0.0']
      args+=objects+shlex.split(settings['LINK_LIBRARIES'].replace('\\','/'))
      run_rsp(clang,args,'link-'+target,out,build)
      manifest['outputs'][target]=hashlib.sha256((out/(target+'.dll')).read_bytes()).hexdigest()
    (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2))
    print('Built '+str(out))
if __name__=='__main__': main()

