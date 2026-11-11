"""Private real-bank reset regression; no copyrighted fixture is shipped.

Pass two folders containing the corrected audio_sample_probe.exe with the old
and candidate runtime DLLs. Tests decode only the user's imported assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--candidate', type=Path, required=True)
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
assets = a.assets.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
cases = [('ICS', n) for n in (28,99,102,103,104,106,116,77)]
cases += [('IGC', n) for n in (96,98,102,103,104,106)]
cases += [('ENGLISHJ_ORC',364)]
results = []
for bank, sample in cases:
    record = {'bank':bank, 'sample':sample}
    for label, folder in [('baseline',a.baseline), ('candidate',a.candidate)]:
        wav = out/f'{label}-{bank}-{sample}.wav'
        r = subprocess.run([str(folder.resolve()/'audio_sample_probe.exe'), str(assets),
                            str(assets/'Sound'/f'{bank}.PAK'), str(sample), str(wav),
                            '--verify-reuse'], capture_output=True, text=True, timeout=30)
        wav.with_suffix('.txt').write_text(r.stdout+r.stderr)
        record[label] = {'returncode':r.returncode,
                         'wav_sha256':hashlib.sha256(wav.read_bytes()).hexdigest() if wav.exists() else None,
                         'output':r.stdout+r.stderr}
    record['fresh_audio_unchanged'] = record['baseline']['wav_sha256'] == record['candidate']['wav_sha256']
    results.append(record)
    print(bank, sample, 'baseline',record['baseline']['returncode'],
          'candidate',record['candidate']['returncode'], 'unchanged',record['fresh_audio_unchanged'], flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))
ok = all(r['candidate']['returncode']==0 and r['fresh_audio_unchanged'] and
         r['baseline']['returncode'] in (0,8) for r in results)
reproduced = sum(r['baseline']['returncode']==8 for r in results)
print(f'{len(results)} banks/samples; baseline reset mismatches in {reproduced}; candidate all pass: {ok}')
raise SystemExit(0 if ok and reproduced else 1)
