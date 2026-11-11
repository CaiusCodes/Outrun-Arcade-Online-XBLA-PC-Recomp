"""Summarize opt-in GPU timing logs without treating menus/loading as racing.

Use --from-time / --to-time to isolate a user-identified stretch of a race.
The measurements are CPU wall time at the command processor, not GPU timestamps.
"""
import argparse
import json
from pathlib import Path
import re

NUMBER = r'(\d+(?:\.\d+)?)'
PATTERN = re.compile(
    r'^\[([^]]+)\].*\[gpu_stats\] ' + NUMBER + r' fps \(' + NUMBER +
    r' ms avg, worst ' + NUMBER + r'\); per frame: ' + NUMBER + r' draws, ' +
    NUMBER + r' submissions, ' + NUMBER + r' ZPD snapshots, ' + NUMBER +
    r' query resolves \(' + NUMBER + r' ms resolving, ' + NUMBER +
    r' ms of it waiting on the GPU\), ' + NUMBER + r' fake$')
FIELDS = ('fps', 'frame_ms', 'worst_frame_ms', 'draws', 'submissions',
          'snapshots', 'query_closes', 'resolve_ms', 'query_wait_ms', 'fake_snapshots')
TIMING_PATTERN = re.compile(
    r'^\[([^]]+)\].*\[outrun_timing\] draw ' + NUMBER + r' ms, submit ' +
    NUMBER + r' ms, fence ' + NUMBER + r' ms \(inclusive CPU/frame\); async (true|false)$')


def parse(text):
    samples = []
    for line in text.splitlines():
        match = PATTERN.match(line.strip())
        if not match:
            timing = TIMING_PATTERN.match(line.strip())
            if timing and samples and timing[1][:19] == samples[-1]['timestamp'][:19]:
                samples[-1].update(draw_cpu_ms=float(timing[2]), submit_cpu_ms=float(timing[3]),
                                   fence_cpu_ms=float(timing[4]), async_queries=timing[5]=='true')
            continue
        samples.append(dict(timestamp=match[1], **dict(zip(FIELDS, map(float, match.groups()[1:])))))
    return samples


def summarize(samples, threshold=50):
    intervals = []
    active = []
    for sample in samples + [None]:
        if sample is not None and sample['fps'] < threshold:
            active.append(sample)
        elif active:
            intervals.append({
                'start': active[0]['timestamp'], 'end': active[-1]['timestamp'],
                'samples': len(active), 'minimum_fps': min(s['fps'] for s in active),
                'mean_frame_ms': sum(s['frame_ms'] for s in active)/len(active),
                'mean_query_wait_ms': sum(s['query_wait_ms'] for s in active)/len(active),
                'maximum_query_closes_per_frame': max(s['query_closes'] for s in active),
                'maximum_draws_per_frame': max(s['draws'] for s in active),
            })
            for field in ('draw_cpu_ms', 'submit_cpu_ms', 'fence_cpu_ms'):
                measured = [s[field] for s in active if field in s]
                if measured:
                    intervals[-1]['mean_'+field] = sum(measured)/len(measured)
            active = []
    return {'sample_count': len(samples), 'threshold_fps': threshold,
            'slow_intervals': intervals,
            'caution': 'One-second CPU wall-time samples; may include loading/menus. Query closes are not necessarily GPU flushes when batching is enabled.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--from-time', help='Inclusive HH:MM:SS local log time')
    parser.add_argument('--to-time', help='Inclusive HH:MM:SS local log time')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    samples = parse(args.log.read_text(encoding='utf-8', errors='replace'))
    samples = [s for s in samples if
               (not args.from_time or s['timestamp'][11:19] >= args.from_time) and
               (not args.to_time or s['timestamp'][11:19] <= args.to_time)]
    report = dict(log=str(args.log), **summarize(samples))
    if args.output:
        args.output.write_text(json.dumps(dict(report, samples=samples), indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    if not samples:
        raise SystemExit('No timing samples found; enable gpu_perf_stats in the profiling build.')


if __name__ == '__main__':
    main()
