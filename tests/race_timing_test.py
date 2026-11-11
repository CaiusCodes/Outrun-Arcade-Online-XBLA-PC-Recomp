import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('race_timing', Path(__file__).resolve().parents[1]/'tools/Analyze-RaceTiming.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class RaceTimingTest(unittest.TestCase):
    def test_real_log_format_and_incomplete_line(self):
        line = ('[2026-09-19 21:16:05.547] [info] [gpu] [t15412] [gpu_stats] '
                '28.0 fps (35.7 ms avg, worst 40.0); per frame: 900 draws, '
                '25.0 submissions, 48.0 ZPD snapshots, 24.0 query resolves '
                '(19.00 ms resolving, 18.50 ms of it waiting on the GPU), 0.0 fake')
        result = timing.parse('unrelated log\n'+line+'\n'+line[:100])
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['query_wait_ms'], 18.5)
        self.assertEqual(result[0]['query_closes'], 24)
        self.assertEqual(result[0]['fps'], 28)
        paired = timing.parse(line+'\n[2026-09-19 21:16:05.547] [info] [gpu] '
            '[outrun_timing] draw 11.00 ms, submit 1.20 ms, fence 0.01 ms (inclusive CPU/frame); async true')
        self.assertEqual(paired[0]['draw_cpu_ms'], 11)
        self.assertTrue(paired[0]['async_queries'])
        self.assertEqual(timing.summarize(paired)['slow_intervals'][0]['mean_fence_cpu_ms'], 0.01)

    def test_separate_dips_and_trailing_dip(self):
        samples = [dict(timestamp=str(i), fps=fps, frame_ms=1000/fps,
                        query_wait_ms=8, query_closes=5, draws=900)
                   for i, fps in enumerate([60, 28, 30, 60, 29])]
        intervals = timing.summarize(samples)['slow_intervals']
        self.assertEqual([x['samples'] for x in intervals], [2, 1])
        self.assertEqual(intervals[0]['minimum_fps'], 28)
        self.assertEqual(intervals[1]['end'], '4')

    def test_no_data_is_not_successful_performance(self):
        self.assertEqual(timing.summarize([])['sample_count'], 0)
        self.assertEqual(timing.summarize([])['slow_intervals'], [])


if __name__ == '__main__':
    unittest.main()
