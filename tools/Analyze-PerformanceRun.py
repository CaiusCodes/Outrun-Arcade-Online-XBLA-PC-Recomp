"""Compare fixed race windows using guest frames and time-weighted CPU samples.

Uses the first real occlusion-query batch as an approximate race marker, then
excludes 20 seconds of warm-up and any 10-second report overlapping that cut.
Raw reports remain available for startup/transition inspection. This does not
infer physics correctness, GPU wait causes or physical display scanout.
"""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import statistics

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
p.add_argument('--cpu',type=Path,help='Optional observer log; omitted means CPU was not measured')
p.add_argument('--output',type=Path,required=True)
p.add_argument('--log-utc-offset',type=float,required=True,help='Local log timezone offset in hours')
p.add_argument('--warmup',type=float,default=20)
a=p.parse_args()
zone=timezone(timedelta(hours=a.log_utc_offset))
text=a.log.read_text(encoding='utf8',errors='replace')
lines=text.splitlines()
marker=next((line[1:24] for line in lines if 'first real GPU snapshot batch published' in line),None)
marker_source='first asynchronous real-query batch'
if marker is None:
    # Synchronous query comparisons never emit the async-worker announcement.
    # Require actual visibility-query activity, not just menu presents.
    for line in lines:
        activity=re.search(r'\[gpu_stats\].*?, ([\d.]+) ZPD snapshots',line)
        if activity and float(activity[1])>0:
            marker=line[1:24]
            marker_source='first nonzero ZPD activity report (approximate, 1s reporting interval)'
            break
if marker is None:raise SystemExit('No race marker; do not use menu-only measurements as race data.')
cut=datetime.fromisoformat(marker).replace(tzinfo=zone)+timedelta(seconds=a.warmup)
pattern=re.compile(r'\[frame_timing\] (\S+) focused=(true|false) fullscreen=(true|false) count=(\d+) fps=([\d.]+) mean_ms=([\d.]+) sd_ms=([\d.]+) min_ms=([\d.]+) max_ms=([\d.]+) over_1_5_mean=(\d+)')
rows=[]
for line in lines:
    m=pattern.search(line)
    if not m:continue
    r=dict(zip(['stream','focused','fullscreen','count','fps','mean_ms','sd_ms','min_ms','max_ms','over_1_5_mean'],m.groups()))
    for k in ['count','fps','mean_ms','sd_ms','min_ms','max_ms','over_1_5_mean']:r[k]=float(r[k])
    r['timestamp']=line[1:24]
    end=datetime.fromisoformat(r['timestamp']).replace(tzinfo=zone)
    r['included']=end-timedelta(seconds=10)>=cut and r['focused']=='true' and r['fullscreen']=='false'
    rows.append(r)
timing=[]
for stream in sorted({r['stream'] for r in rows}):
    selected=[r for r in rows if r['included'] and r['stream']==stream]
    if not selected:continue
    seconds=sum(r['count']/r['fps'] for r in selected if r['fps']>0)
    timing.append(dict(stream=stream,windows=len(selected),fps=sum(r['count'] for r in selected)/seconds if seconds else 0,
        median_sd_ms=statistics.median(r['sd_ms'] for r in selected),max_ms=max(r['max_ms'] for r in selected),
        slow_frames=sum(r['over_1_5_mean'] for r in selected),first_report=selected[0]['timestamp'],last_report=selected[-1]['timestamp']))
guest=next((r for r in timing if r['stream']=='guest_vdswap'),None)
if not guest:raise SystemExit('No complete steady focused/windowed guest-frame window.')
start=datetime.fromisoformat(guest['first_report']).replace(tzinfo=zone)-timedelta(seconds=10)
end=datetime.fromisoformat(guest['last_report']).replace(tzinfo=zone)
cpu_rows=[json.loads(line) for line in a.cpu.read_text().splitlines() if line.strip()] if a.cpu else []
weight=total=0.0;threads={};count=0
for row in cpu_rows:
    if row['kind']!='sample' or not row['focused']:continue
    sample_end=datetime.fromisoformat(row['utc'])
    sample_start=sample_end-timedelta(milliseconds=row['interval_ms'])
    overlap=(min(end,sample_end)-max(start,sample_start)).total_seconds()
    if overlap<=0:continue
    weight+=overlap;count+=1;total+=overlap*row['process_cpu_percent']
    for t in row['threads']:threads[t['tid']]=threads.get(t['tid'],0)+overlap*t['cpu_percent']
cpu=dict(samples=count,duration_seconds=weight,start_utc=start.astimezone(timezone.utc).isoformat(),end_utc=end.astimezone(timezone.utc).isoformat(),
    process_cpu_percent_one_core=total/weight if weight else None,
    top_threads=[dict(tid=tid,cpu_percent_one_core=value/weight) for tid,value in sorted(threads.items(),key=lambda x:x[1],reverse=True)[:10]])
result=dict(log=str(a.log),cpu_log=str(a.cpu) if a.cpu else None, race_marker=marker,race_marker_source=marker_source,steady_cutoff=cut.isoformat(),timing=timing,cpu=cpu if a.cpu else None,
    raw_reports=rows,slow_sdl_calls=[line for line in lines if '[input_timing]' in line])
a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in result.items() if k not in ['raw_reports','slow_sdl_calls']},indent=2))
