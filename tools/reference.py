"""Replay an authored recording in MesenCE 2.2.1 (Python standard library)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from compare_frames import COMPONENTS, SIZES, HEADER

def load_recording(path):
    data=Path(path).read_bytes()
    lines=data.decode('ascii').split('\n')
    if b'\r' in data or lines[-1] != '':
        raise ValueError('Recording must use LF and end with LF')
    lines.pop()
    if len(lines)<4 or lines[0]!='psmodern-recording 1':
        raise ValueError('Recording line 1: wrong format/version or missing header')
    hashes=[]
    for n,key in ((1,'rom'),(2,'cart-ram')):
        if not re.fullmatch(key+' [0-9a-f]{40}',lines[n]):
            raise ValueError(f'Recording line {n+1}: invalid header')
        hashes.append(lines[n].split()[1])
    changes={}; prior=('-', '-', '0', '0');last=-1
    for n,line in enumerate(lines[3:-1],4):
        m=re.fullmatch(r'input (0|[1-9][0-9]*) p1=(-|U?D?L?R?1?2?) p2=(-|U?D?L?R?1?2?) pause=([01]) reset=([01])',line)
        if not m or not m[2] or not m[3]: raise ValueError(f'Recording line {n}: invalid change')
        frame=int(m[1]);state=m.groups()[1:]
        if frame>2**64-1:raise ValueError(f'Recording line {n}: frame exceeds uint64')
        if frame<=last or state==prior:raise ValueError(f'Recording line {n}: nonincreasing or redundant change')
        changes[frame]=state;last=frame;prior=state
    if not re.fullmatch(r'end (0|[1-9][0-9]*)',lines[-1]):raise ValueError(f'Recording line {len(lines)}: missing end')
    total=int(lines[-1].split()[1])
    if total>2**64-1:raise ValueError('End frame exceeds uint64')
    if last>=total:raise ValueError('Change after end')
    return hashes,changes,total

def selection(spec,total):
    if spec=='all':return set(range(1,total+1))
    result=set()
    for item in spec.split(','):
        if not re.fullmatch(r'[1-9][0-9]*(?:-[1-9][0-9]*)?',item):raise ValueError('Invalid dump selection')
        values=[int(x) for x in item.split('-')];a,b=values[0],values[-1]
        if a>b or b>total:raise ValueError('Dump selection outside recording')
        result.update(range(a,b+1))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mesen');p.add_argument('rom');p.add_argument('recording');p.add_argument('--dump',required=True);p.add_argument('--out',required=True);p.add_argument('--cart-ram');p.add_argument('--reference-label',default='MesenCE 2.2.1 (unspecified build)')
    a=p.parse_args();hashes,changes,total=load_recording(a.recording);selected=selection(a.dump,total)
    if any(state[3]=='1' for state in changes.values()):
        raise ValueError('MesenCE 2.2.1 cannot replay the SMS Reset pin; refusing this recording')
    if total==0:raise ValueError('Reference run requires at least one frame')
    rom=Path(a.rom).resolve();exe=Path(a.mesen).resolve();out=Path(a.out).resolve()
    if not a.reference_label.strip():raise ValueError('Reference label must not be empty')
    if hashlib.sha1(rom.read_bytes()).hexdigest()!=hashes[0]:raise ValueError('ROM SHA-1 mismatch')
    cart=Path(a.cart_ram).read_bytes() if a.cart_ram else bytes(8192)
    if len(cart)!=8192 or hashlib.sha1(cart).hexdigest()!=hashes[1]:raise ValueError('Initial cartridge RAM SHA-1 mismatch')
    out.mkdir(parents=True,exist_ok=False)
    home=out/'home';home.mkdir();shutil.copy2(exe,home/exe.name)
    libraries=sorted(exe.parent.glob('*.dll'))
    for library in libraries:shutil.copy2(library,home/library.name)
    identity={'label':a.reference_label,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
              'dll_sha256':{library.name:hashlib.sha256(library.read_bytes()).hexdigest() for library in libraries}}
    (out/'reference.json').write_text(json.dumps(identity,indent=2,sort_keys=True)+'\n',encoding='ascii')
    (home/'settings.json').write_text('{}',encoding='ascii')
    (home/'Saves').mkdir();(home/'Saves'/(rom.stem+'.sav')).write_bytes(cart)
    (out/'initial-cart-ram.bin').write_bytes(cart)
    def quote(s):return json.dumps(str(s).replace('\\','/'))
    state=('-', '-', '0', '0');inputs=[]
    names=dict(zip('UDLR12',('up','down','left','right','one','two')))
    for n in range(total):
        state=changes.get(n,state);pads=[]
        for pad in state[:2]:
            fields=[name+'='+('true' if key in pad else 'false') for key,name in names.items()]
            fields+=['pause='+('true' if state[2]=='1' else 'false'),'reset='+('true' if state[3]=='1' else 'false')]
            pads.append('{'+','.join(fields)+'}')
        inputs.append('{'+','.join(pads)+'}')
    config=out/'config.lua';config.write_text('return {out='+quote(out.as_posix()+'/')+',cart='+quote(out/'initial-cart-ram.bin')+',frames='+str(total)+',inputs={'+','.join(inputs)+'}}',encoding='ascii')
    script=out/'capture.lua';script.write_text(Path(__file__).with_suffix('.lua').read_text().replace('CONFIG_PATH',config.as_posix()),encoding='ascii')
    command=[str(home/exe.name),'--testRunner','--timeout=600','--debug.ScriptWindow.ScriptTimeout=1200','--debug.ScriptWindow.AllowIoOsAccess=true','--sms.Region=Ntsc','--sms.Revision=Sms1','--sms.RamPowerOnState=AllZeros','--sms.EnableFmAudio=false','--sms.Port1.Type=SmsController','--sms.Port2.Type=SmsController',str(rom),str(script)]
    env=dict(os.environ,TEMP=str(out),TMP=str(out),DOTNET_BUNDLE_EXTRACT_BASE_DIR=str(out/'bundle'))
    with (out/'console.log').open('w') as log:subprocess.run(command,cwd=home,env=env,stdout=log,stderr=log,check=True,timeout=660)
    if not (out/'complete.txt').exists():raise ValueError('Reference capture did not complete; inspect console.log')
    with (out/'manifest.txt').open('w',encoding='ascii',newline='\n') as manifest:
        manifest.write(HEADER+'\n')
        for n in range(1,total+1):
            digests=[]
            for component,size in zip(COMPONENTS,SIZES):
                path=out/f'frame-{n:06d}.{component}.bin';data=path.read_bytes()
                if len(data)!=size:raise ValueError('Wrong capture size')
                digests.append(hashlib.sha1(data).hexdigest())
                if n not in selected:path.unlink()
            manifest.write(str(n)+' '+' '.join(digests)+'\n')
    print(f'Captured {total} frames with {a.reference_label} ({identity["executable_sha256"]}) into {out}')

if __name__=='__main__':
    main()
