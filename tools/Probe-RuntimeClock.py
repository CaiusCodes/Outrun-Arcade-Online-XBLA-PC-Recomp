"""Inspect a private runtime's stale-clock guard without launching the game.

Loads the DLL into this process only. The deliberately future-valued previous
host sample simulates a clock read taken before another thread advanced the
shared timestamp. All modified globals are restored before returning.
The extended checks also exercise a 50 MHz clock in this process only.
This is a correctness probe, not a gameplay-speed benchmark.
"""
import argparse
import ctypes
import hashlib
import json
import os
import threading
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-fixed", action="store_true",
                        help="Also test fractional conversion and concurrent clock reads; fail on errors")
    args = parser.parse_args()
    runtime = args.runtime.resolve(strict=True)
    with os.add_dll_directory(str(runtime.parent)):
        dll = ctypes.CDLL(str(runtime))
        update = getattr(dll, "?UpdateGuestClock@chrono@rex@@YA_KXZ")
        update.argtypes = []
        update.restype = ctypes.c_uint64
        host_frequency = getattr(dll, "?host_tick_frequency_platform@Clock@chrono@rex@@SA_KXZ")
        host_frequency.argtypes = []
        host_frequency.restype = ctypes.c_uint64
        host_now = getattr(dll, "?host_tick_count_platform@Clock@chrono@rex@@SA_KXZ")
        host_now.argtypes = []
        host_now.restype = ctypes.c_uint64
        last_host = ctypes.c_uint64.in_dll(dll, "?last_host_tick_count_@chrono@rex@@3_KA")
        last_guest = ctypes.c_uint64.in_dll(dll, "?last_guest_tick_count_@chrono@rex@@3_KA")
        saved_host, saved_guest = last_host.value, last_guest.value
        try:
            future_host = host_now() + 60 * host_frequency()
            last_host.value = future_host
            last_guest.value = 123456789
            result = update()
            observed_host = last_host.value
            observed_guest = last_guest.value
        finally:
            last_host.value, last_guest.value = saved_host, saved_guest
        report = {
            "runtime": str(runtime),
            "sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
            "scope": "isolated process; simulated stale host sample; no game launched",
            "future_previous_host": future_host,
            "observed_previous_host": observed_host,
            "stale_sample_preserved_previous_host": observed_host == future_host,
            "guest_ticks_unchanged": result == observed_guest == 123456789,
            "interpretation": "A stale sample must leave BOTH shared counters unchanged."
        }
        if args.expect_fixed:
            remainder = ctypes.c_uint64.in_dll(dll, "?guest_tick_remainder_@chrono@rex@@3_KA")
            ratio = (ctypes.c_uint64 * 2).in_dll(dll, "?guest_tick_ratio_@chrono@rex@@3U?$pair@_K_K@std@@A")
            saved = (last_host.value, last_guest.value, remainder.value, tuple(ratio))
            fractional_errors = 0
            try:
                # An awkward ratio catches discarded sub-tick fractions. Compare
                # to the exact timestamps actually consumed, not a wall-time guess.
                ratio[:] = (2, 3)
                remainder.value = 0
                last_host.value = host_now()
                last_guest.value = 0
                origin = last_host.value
                for _ in range(2000):
                    value = update()
                    expected, rem = divmod((last_host.value-origin)*2, 3)
                    fractional_errors += value != expected or remainder.value != rem
            finally:
                last_host.value, last_guest.value, remainder.value = saved[:3]
                ratio[:] = saved[3]
            report['fractional_samples'] = 2000
            report['fractional_errors'] = fractional_errors
            set_frequency = getattr(dll, "?set_guest_tick_frequency@Clock@chrono@rex@@SAX_K@Z")
            set_frequency.argtypes = [ctypes.c_uint64]
            set_frequency.restype = None
            set_frequency(50000000)
            errors = []
            counts = []
            stop = threading.Event()
            def reader():
                previous, count = update(), 0
                while not stop.is_set():
                    current = update()
                    if current < previous:
                        errors.append((previous, current))
                    previous = current
                    count += 1
                counts.append(count)
            threads = [threading.Thread(target=reader) for _ in range(4)]
            for thread in threads:
                thread.start()
            host_start = host_now()
            guest_start = update()
            try:
                time.sleep(2)
                guest_end = update()
                host_end = host_now()
            finally:
                stop.set()
                for thread in threads:
                    thread.join()
            rate = (guest_end-guest_start) / ((host_end-host_start)/host_frequency())
            report.update(concurrent_reads=sum(counts), backwards_reads=len(errors),
                          measured_guest_hz=rate, requested_guest_hz=50000000)
            report['passed'] = (report['stale_sample_preserved_previous_host'] and
                                report['guest_ticks_unchanged'] and fractional_errors == 0 and
                                not errors and abs(rate/50000000-1) < .01)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if args.expect_fixed and not report['passed']:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
