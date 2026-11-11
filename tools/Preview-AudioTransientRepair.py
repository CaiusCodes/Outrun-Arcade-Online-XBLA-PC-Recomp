"""Private offline listening experiment. Never used by the game or packaging.

Detect abrupt block-edge transients and bridge 12ms of affected stereo audio.
This removes all content in each repaired span, not an identified sound voice.
"""
import argparse
import array
import json
import math
from pathlib import Path
import struct
import sys
import wave

RATE = 48000
BLOCK = 256
SPAN = 576


def repair(channels):
    """Return private copies and repair spans; never mutate the original mix."""
    output = [array.array('d', channel) for channel in channels]
    events = []
    for channel, source in enumerate(channels):
        next_allowed = 0
        for at in range(BLOCK, len(source) - SPAN, BLOCK):
            if at < next_allowed:
                continue
            before = source[at - 33:at]
            after = source[at:at + SPAN + 1]
            if not all(math.isfinite(x) for x in (*before, *after)):
                continue
            jump = abs(source[at] - source[at - 1])
            slope = sum(abs(before[i] - before[i - 1]) for i in range(1, 33)) / 32
            # The confirmed 4.053s event has a noisy burst AFTER its leading
            # edge. The previous experiment rejected it using post-edge slope.
            if jump <= .30 or jump <= 8 * max(slope, .001):
                continue
            left, right = source[at - 1], source[at + SPAN]
            for i in range(SPAN):
                output[channel][at + i] = left + (right - left) * (i + 1) / (SPAN + 1)
            events.append({'channel': channel, 'start_sample': at,
                           'end_sample_exclusive': at + SPAN,
                           'seconds': at / RATE, 'jump': jump,
                           'previous_mean_slope': slope})
            next_allowed = at + SPAN + 33
    return output, sorted(events, key=lambda e: (e['start_sample'], e['channel']))


def read_mix(path):
    raw = path.read_bytes()
    if raw[:8] != b'ORPCM001' or struct.unpack_from('<III', raw, 8) != (RATE, 6, BLOCK):
        raise ValueError('Unsupported capture')
    record = 8 + 6 * BLOCK * 4
    if (len(raw) - 20) % record:
        raise ValueError('Truncated capture')
    channels = [array.array('d'), array.array('d')]
    for at in range(20, len(raw), record):
        frame = struct.unpack_from('>1536f', raw, at + 8)
        for c in range(2):
            channels[c].extend(.4 * (frame[c * BLOCK + i] +
                                    .5 * frame[2 * BLOCK + i] +
                                    frame[(4 + c) * BLOCK + i]) for i in range(BLOCK))
    return channels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report_path = args.output.with_suffix('.json')
    if args.output.exists() or report_path.exists():
        raise ValueError('Refusing to overwrite an existing output')
    original = read_mix(args.capture)
    corrected, events = repair(original)
    pcm = array.array('h')
    for pair in zip(*corrected):
        for value in pair:
            pcm.append(round(max(-1, min(1, value)) * 32767) if math.isfinite(value) else 0)
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(str(args.output), 'wb') as out:
        out.setparams((2, 2, RATE, 0, 'NONE', 'not compressed'))
        out.writeframes(pcm.tobytes())
    report = {'source': str(args.capture.resolve()), 'events': events,
              'duration_seconds': len(original[0]) / RATE,
              'replaced_channel_samples': len(events) * SPAN,
              'total_channel_samples': len(original[0]) * 2,
              'scope': 'Offline diagnostic only. Each span replaces the entire affected channel mix. '
                       'May remove legitimate transients; not a root-cause fix or runtime feature.'}
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'{len(events)} channel spans repaired; {report["duration_seconds"]:.3f}s preview saved')


if __name__ == '__main__':
    main()
