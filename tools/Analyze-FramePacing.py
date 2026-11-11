"""Summarize 10-second timing windows without confusing host and guest FPS.
Discard the first two reports of each stream/focus/display run (startup/transitions).
Retain the raw reports in JSON so stalls and max frame times remain reviewable.
"""
import argparse,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',type=Path)
a=p.parse_args(); rows=[]; summary=[]
pattern=re.compile(r'\[frame_timing\] (\S+) focused=(true|false) fullscreen=(true|false) count=(\d+) fps=([\d.]+) mean_ms=([\d.]+) sd_ms=([\d.]+) min_ms=([\d.]+) max_ms=([\d.]+) over_1_5_mean=(\d+)')
for log in a.logs:
    runs={}; previous={}; age={}
    for line in log.read_text(encoding='utf8',errors='replace').splitlines():
        m=pattern.search(line)
        if not m:continue
        stream,focus,full=m.group(1,2,3);state=(focus,full)
        age[stream]=age.get(stream,0)+1 if previous.get(stream)==state else 1
        previous[stream]=state
        row=dict(log=str(log),timestamp=line[1:24],stream=stream,focused=focus=='true',fullscreen=full=='true',count=int(m[4]),fps=float(m[5]),mean_ms=float(m[6]),sd_ms=float(m[7]),min_ms=float(m[8]),max_ms=float(m[9]),over_1_5_mean=int(m[10]),included=age[stream]>2)
        rows.append(row)
        if row['included']:runs.setdefault((stream,*state),[]).append(row)
    for (stream,focus,full),values in sorted(runs.items()):
        result=dict(log=str(log),stream=stream,focused=focus,fullscreen=full,windows=len(values),median_fps=statistics.median(v['fps'] for v in values),median_mean_ms=statistics.median(v['mean_ms'] for v in values),median_sd_ms=statistics.median(v['sd_ms'] for v in values),worst_ms=max(v['max_ms'] for v in values),slow_frames=sum(v['over_1_5_mean'] for v in values))
        summary.append(result)
        print(f"{log.name} {stream:20} focused={focus:5} fullscreen={full:5} windows={len(values):3} fps={result['median_fps']:.3f} mean={result['median_mean_ms']:.6f} sd={result['median_sd_ms']:.6f} worst={result['worst_ms']:.3f} slow={result['slow_frames']}")
if a.output:a.output.write_text(json.dumps({'summary':summary,'reports':rows},indent=2),encoding='utf8')
