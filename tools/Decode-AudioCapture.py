"""Convert optional private game PCM capture to stereo WAV and timestamp index."""
import argparse,array,json,math,struct,sys,wave
from pathlib import Path
def decode(source,dest):
    data=source.read_bytes()
    if data[:8]!=b'ORPCM001' or struct.unpack_from('<III',data,8)!=(48000,6,256):raise ValueError('Unsupported capture format')
    size=8+6*256*4
    if (len(data)-20)%size:raise ValueError('Truncated capture')
    frames=(len(data)-20)//size;times=[]
    with wave.open(str(dest),'wb') as wav:
        wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(48000)
        for frame in range(frames):
            pos=20+frame*size;times.append(struct.unpack_from('<Q',data,pos)[0]);samples=array.array('f',data[pos+8:pos+size])
            if sys.byteorder=='little':samples.byteswap()
            pcm=array.array('h')
            for i in range(256):
                for c in (0,1):
                    # Match the shipped SDL driver's stereo downmix/headroom.
                    value=.4*(samples[c*256+i]+.5*samples[2*256+i]+samples[(4+c)*256+i])
                    value=max(-1.,min(1.,value)) if math.isfinite(value) else 0.
                    pcm.append(round(value*32767))
            if sys.byteorder!='little':pcm.byteswap()
            wav.writeframesraw(pcm.tobytes())
    index={'rate':48000,'frames':frames,'duration_seconds':frames*256/48000,'audio_frame_start_ms':times,'note':'Game PCM before host audio queue, stereo downmix; timing marks are submission times, not speaker playback times.'}
    dest.with_suffix('.json').write_text(json.dumps(index),encoding='utf-8');return index
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('capture',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args();out=args.output or args.capture.with_suffix('.wav');index=decode(args.capture,out);print(f"Decoded {index['duration_seconds']:.2f}s to {out}")
