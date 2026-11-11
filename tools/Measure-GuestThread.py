"""Read-only scheduler LR/CPU sampler for an explicitly selected private copy.

Does not launch/focus/suspend a process, write its memory or inject input.
Requires its default-off outrun_thread_sample_log announcement. LR is the
guest return address, not the current instruction; correlate with CPU and code.
"""
import argparse
from collections import Counter
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import re
import time

kernel = c.WinDLL('kernel32', use_last_error=True)
psapi = c.WinDLL('psapi', use_last_error=True)

def api(name, restype, args):
    f=getattr(kernel,name);f.restype=restype;f.argtypes=args;return f

open_process=api('OpenProcess',w.HANDLE,[w.DWORD,w.BOOL,w.DWORD])
open_thread=api('OpenThread',w.HANDLE,[w.DWORD,w.BOOL,w.DWORD])
close=api('CloseHandle',w.BOOL,[w.HANDLE])
query_path=api('QueryFullProcessImageNameW',w.BOOL,[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)])
read=api('ReadProcessMemory',w.BOOL,[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)])
exit_code=api('GetExitCodeProcess',w.BOOL,[w.HANDLE,c.POINTER(w.DWORD)])
thread_exit=api('GetExitCodeThread',w.BOOL,[w.HANDLE,c.POINTER(w.DWORD)])
thread_times=api('GetThreadTimes',w.BOOL,[w.HANDLE,*([c.POINTER(w.FILETIME)]*4)])
process_times=api('GetProcessTimes',w.BOOL,[w.HANDLE,*([c.POINTER(w.FILETIME)]*4)])
current_process=api('GetCurrentProcess',w.HANDLE,[])
psapi.EnumProcesses.restype=w.BOOL
psapi.EnumProcesses.argtypes=[c.POINTER(w.DWORD),w.DWORD,c.POINTER(w.DWORD)]

def cpu(handle, function):
    values=[w.FILETIME() for _ in range(4)]
    if not function(handle,*(c.byref(x) for x in values)): return None
    return sum((x.dwHighDateTime<<32)|x.dwLowDateTime for x in values[2:])*1e-7

def find_process(exe):
    ids=(w.DWORD*8192)(); size=w.DWORD()
    if not psapi.EnumProcesses(ids,c.sizeof(ids),c.byref(size)):raise c.WinError(c.get_last_error())
    matches=[]
    for pid in ids[:size.value//c.sizeof(w.DWORD)]:
        h=open_process(0x1000|0x10,False,pid)  # query-limited + VM_READ only
        if not h:continue
        name=c.create_unicode_buffer(32768); length=w.DWORD(len(name))
        if query_path(h,0,name,c.byref(length)) and os.path.normcase(name.value)==os.path.normcase(str(exe)):
            matches.append((pid,h))
        else:close(h)
    if len(matches)>1:
        for _,h in matches:close(h)
        raise RuntimeError('Multiple matching copies; no sampling performed')
    return matches[0] if matches else None

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--wait-seconds',type=float,default=43200)
    p.add_argument('--seconds',type=float,default=240)
    a=p.parse_args();exe=a.exe.resolve(strict=True)
    pattern=re.compile(r'\[thread_sample\] pid=(\d+) tid=(\d+) lr_address=([0-9A-Fa-f]+)')
    with a.output.open('x',encoding='utf8',buffering=1) as output:
        def emit(record):output.write(json.dumps(record)+'\n')
        emit({'kind':'waiting','exe':str(exe),'scope':'read-only LR hints; no current-PC sampling'})
        deadline=time.monotonic()+a.wait_seconds;target=None
        while time.monotonic()<deadline:
            target=find_process(exe)
            if target:break
            time.sleep(1)
        if not target:
            emit({'kind':'timeout','wait_seconds':a.wait_seconds,'reason':'No matching user-launched copy appeared'})
            raise RuntimeError('No user-launched copy appeared')
        pid,handle=target;thread=None
        observer_start=cpu(current_process(),process_times)
        started=time.monotonic();last=started;counts=Counter();failures=0
        last_thread_cpu=None;last_process_cpu=cpu(handle,process_times)
        try:
            # Read only logs modified for this process run, not an old LR address.
            process_log=None; address=None;tid=None
            code=w.DWORD()
            while time.monotonic()-started<a.seconds:
                if not exit_code(handle,c.byref(code)) or code.value!=259:break
                if address is None:
                    logs=sorted((exe.parent/'logs').glob('*.log'),key=lambda f:f.stat().st_mtime,reverse=True)
                    if logs and time.time()-logs[0].stat().st_mtime<5:
                        match=pattern.search(logs[0].read_text(encoding='utf8',errors='replace'))
                        if match and int(match[1])==pid:
                            tid=int(match[2]);address=int(match[3],16);process_log=str(logs[0])
                            thread=open_thread(0x0800,False,tid)  # QUERY_LIMITED_INFORMATION
                            if not thread:raise c.WinError(c.get_last_error())
                            last_thread_cpu=cpu(thread,thread_times);last=time.monotonic()
                            last_process_cpu=cpu(handle,process_times)
                            emit({'kind':'start','pid':pid,'tid':tid,'log':process_log})
                    time.sleep(.1);continue
                if not thread_exit(thread,c.byref(code)) or code.value!=259:break
                value=c.c_uint64();got=c.c_size_t()
                if read(handle,address,c.byref(value),8,c.byref(got)) and got.value==8:
                    counts[f'{value.value:08X}']+=1
                else:failures+=1
                now=time.monotonic()
                if now-last>=10:
                    tc=cpu(thread,thread_times);pc=cpu(handle,process_times)
                    emit({'kind':'sample','elapsed_seconds':now-started,'seconds':now-last,
                          'thread_cpu_percent_one_core':100*(tc-last_thread_cpu)/(now-last) if tc is not None and last_thread_cpu is not None else None,
                          'process_cpu_percent_one_core':100*(pc-last_process_cpu)/(now-last) if pc is not None and last_process_cpu is not None else None,
                          'lr_counts':dict(counts),'read_failures':failures})
                    last=now;last_thread_cpu=tc;last_process_cpu=pc;counts.clear();failures=0
                time.sleep(.005)
            emit({'kind':'end','elapsed_seconds':time.monotonic()-started,
                  'observer_cpu_seconds':cpu(current_process(),process_times)-observer_start,
                  'partial_lr_counts':dict(counts),'partial_read_failures':failures})
        finally:
            if thread:close(thread)
            close(handle)

if __name__=='__main__':main()
