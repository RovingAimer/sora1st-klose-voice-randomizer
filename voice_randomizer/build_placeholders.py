"""Create unmistakable test audio without modifying legacy voice assets.

Each file: two electronic tones, Chinese TTS, then silence to the game's original
sample count. Long lines announce their number; short lines say '测试'.
"""
from pathlib import Path
from array import array
import hashlib, io, json, math, shutil, subprocess, sys, wave

HERE=Path(__file__).resolve().parent
RATE=48000

def read_pcm(data):
    with wave.open(io.BytesIO(data),'rb') as w:
        assert (w.getframerate(),w.getnchannels(),w.getsampwidth())==(RATE,1,2)
        samples=array('h',w.readframes(w.getnframes()))
    if sys.byteorder!='little': samples.byteswap()
    return samples

def wav_bytes(samples):
    out=io.BytesIO()
    with wave.open(out,'wb') as w:
        w.setparams((1,2,RATE,0,'NONE','not compressed'))
        x=array('h',samples)
        if sys.byteorder!='little': x.byteswap()
        w.writeframes(x.tobytes())
    return out.getvalue()

def trim(samples):
    threshold=max(100,max(abs(x) for x in samples)*0.012)
    active=[i for i,x in enumerate(samples) if abs(x)>threshold]
    assert active,'TTS source is silent'
    return samples[max(0,active[0]-240):min(len(samples),active[-1]+481)]

def main():
    ffmpeg=shutil.which('ffmpeg')
    assert ffmpeg,'ffmpeg is required'
    entries=json.loads((HERE/'assets/manifest.json').read_text(encoding='utf-8'))
    source=HERE/'assets/tts_source'
    output=HERE/'assets/placeholder'
    output.mkdir(exist_ok=True)
    manifest_path=HERE/'assets/placeholder_manifest.json'
    previous={x['name']:x for x in json.loads(manifest_path.read_text(encoding='utf-8'))} if manifest_path.exists() else {}
    marker_frames=round(0.18*RATE)
    gap_frames=round(0.015*RATE)
    result=[]
    for en in entries:
        name=en['name']
        frames=en['new_frames']
        path=output/(name+'.wav')
        prior=previous.get(name)
        if (prior and prior['frames']==frames and prior['phrase']==en['placeholder_phrase']
                and path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()==prior['sha256']):
            result.append(dict(prior, category=en['category']))
            continue
        full_phrase=en['new_seconds']>=1.35
        speech=trim(read_pcm((source/(name+'.wav' if full_phrase else 'test.wav')).read_bytes()))
        budget=frames-marker_frames-gap_frames-round(0.015*RATE)
        if len(speech)>budget:
            speed=len(speech)/budget*1.015
            filters=[]
            while speed>2:
                filters.append('atempo=2'); speed/=2
            filters.append(f'atempo={speed:.9f}')
            raw=subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-f','wav','-i','pipe:0',
                                '-af',','.join(filters),'-ac','1','-ar',str(RATE),'-f','s16le','pipe:1'],
                               input=wav_bytes(speech),stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                               check=True).stdout
            speech=array('h',raw)
            if sys.byteorder!='little': speech.byteswap()
        if len(speech)>budget:
            excess=len(speech)-budget
            assert excess<RATE*0.04,'Unexpected TTS time-stretch overrun'
            speech=speech[:budget]
        gain=0.56*32767/max(abs(x) for x in speech)
        speech=array('h',(round(x*gain) for x in speech))
        fade=round(RATE*0.008)
        for i in range(min(fade,len(speech)//2)):
            speech[i]=round(speech[i]*i/fade)
            speech[-1-i]=round(speech[-1-i]*i/fade)
        samples=array('h',[0])*frames
        number=en['marker_number']
        for start,freq in [(0,740+(number%8)*13),(round(0.095*RATE),1180+(number%11)*11)]:
            length=round(0.075*RATE)
            for i in range(length):
                envelope=min(1,i/(RATE*0.006),(length-1-i)/(RATE*0.006))
                samples[start+i]=round(0.38*32767*envelope*math.sin(2*math.pi*freq*i/RATE))
        start=marker_frames+gap_frames
        samples[start:start+len(speech)]=speech
        assert len(samples)==frames
        data=wav_bytes(samples)
        path.write_bytes(data)
        decoded=read_pcm(data)
        assert len(decoded)==frames and max(abs(x) for x in decoded)<32767
        peak=max(abs(x) for x in decoded)/32768
        rms=math.sqrt(sum(x*x for x in decoded)/frames)/32768
        assert rms>0.025
        assert hashlib.sha256(data).hexdigest()!=en['old_sha256']
        result.append(dict(name=name,voice_id=en['voice_id'],frames=frames,seconds=frames/RATE,
                           phrase=en['placeholder_phrase'],category=en['category'],
                           peak=peak,rms=rms,sha256=hashlib.sha256(data).hexdigest()))
        if len(result)%100==0: print(f'Prepared {len(result)}/{len(entries)} placeholders',flush=True)
    assert len(result)==len(entries) and len({x['name'] for x in result})==len(entries)
    (HERE/'assets/placeholder_manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Short previews, separated by silence; no audio is played automatically.
    for filename,names in [
        ('placeholder_preview.wav',['v006_b0021','v006_b0078','v006_b0106b']),
        ('story_placeholder_preview.wav',['v006_02_0001','v006_02_0004','v006_02_0009'])
    ]:
        preview=array('h')
        for name in names:
            preview.extend(read_pcm((output/(name+'.wav')).read_bytes()))
            preview.extend(array('h',[0])*round(0.45*RATE))
        (HERE/filename).write_bytes(wav_bytes(preview))
    print(f'Validated {len(result)} placeholders: PCM mono, 48 kHz, 16-bit; exact original-game sample counts.')
    print('Pattern: two tones + Chinese TTS. Original legacy assets untouched.')
    print('Peak range:',min(x['peak'] for x in result),max(x['peak'] for x in result))

if __name__=='__main__': main()
