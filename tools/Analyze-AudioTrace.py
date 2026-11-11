"""Summarize optional audio/input traces without naming unverified sound IDs."""
import argparse,collections,json,re
from pathlib import Path

def analyze(text):
    entries=[]
    for line in text.splitlines():
        if '[audio_trace] ms=' not in line:continue
        fields=dict(re.findall(r'([\w-]+)=([^ ]+)',line))
        fields['ms']=int(fields['ms']);entries.append(fields)
    keys=[e for e in entries if e.get('key') in ('41','44') and e.get('down')=='true' and e.get('repeat')=='false' and e.get('menu')=='false']
    sounds=[e for e in entries if e.get('sound-stage')=='sample-start' and e.get('menu')=='false']
    events=[]
    for key in keys:
        nearby=[s for s in sounds if 0<=s['ms']-key['ms']<=120]
        events.append({'ms':key['ms'],'key':'A' if key['key']=='41' else 'D','sound_starts_within_120ms':[{'delay_ms':s['ms']-key['ms'],'sample':int(s['sample']),'event':s['event'],'caller':s['caller']} for s in nearby]})
    counts=collections.Counter((int(s['sample']),s['caller']) for s in sounds)
    keyboard=[e for e in entries if e.get('source')=='keyboard' and e.get('menu')=='false']
    controller=[e for e in entries if e.get('source')=='controller' and e.get('menu')=='false']
    return {'records':len(entries),'race_keyboard_changes':len(keyboard),'race_controller_changes':len(controller),'nonrepeat_race_steering_presses':len(keys),'race_sound_starts':len(sounds),'sound_start_counts':[{'sample':sample,'caller':caller,'count':count} for (sample,caller),count in counts.most_common(30)],'steering_correlations':events,'note':'Correlation only; numeric samples/callers are not yet identified as exhaust or gear sounds.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('log',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
    result=analyze(args.log.read_text(encoding='utf-8-sig'));out=args.output or args.log.with_suffix('.audio-summary.json');out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='steering_correlations'},indent=2));print(f'Detailed steering correlations: {out}')
