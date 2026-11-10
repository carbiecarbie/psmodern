"""Real reference-driver regression with authored Z80 programs, no game ROM."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compare_frames import compare

runner,executable=sys.argv[1:3]
if not executable or not Path(executable).exists():raise SystemExit(77)
driver=Path(sys.argv[3]) if len(sys.argv)>3 else Path(__file__).resolve().parents[1]/'tools/reference.py'
with tempfile.TemporaryDirectory(prefix='reference-boundary-') as folder:
    root=Path(folder)
    for mode in ("nop","vram","halt"):
        delayed_vram=mode=="vram"
        data=bytearray(512*1024)
        # Set R1, then enable INT just before the frame boundary. The NOP
        # finishes at 59736; IRQ entry must not leak its stack into the dump.
        data[:8]=bytes.fromhex('3e60 d3bf 3e81 d3bf')
        ei=8+(59728-36)//4
        data[ei]=0xfb
        if mode=="halt":data[ei+1]=0x76
        if delayed_vram:
            # RET NZ is not taken (5 T); EI + OUT ends at 59746, with its
            # queued VRAM transfer completing at 59759 during IRQ entry.
            data=bytearray(512*1024)
            setup=bytes.fromhex('3e60 d3bf 3e81 d3bf 010000')
            data[:len(setup)]=setup
            at=len(setup)+(59726-46)//4
            data[at:at+4]=bytes.fromhex('c0 fb d3 be')
        stem=mode
        rom=root/(stem+'.sms');rom.write_bytes(data)
        rec=root/(stem+'.psrec')
        rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
                       '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
        actual=root/(stem+'-ps');expected=root/(stem+'-saída 漢字')
        subprocess.run([runner,rom,rec,'--dump','all','--out',actual],check=True)
        subprocess.run([sys.executable,'-B',driver,executable,rom,rec,'--dump','all','--out',expected,
                        '--reference-label','instruction boundary regression'],check=True)
        # Both independent producers must publish the boundary CPU and the
        # per-frame port-write stream; the normal comparison checks the hashes.
        for result in (actual, expected):
            if not (result/'frame-000001.cpu.bin').is_file():
                raise RuntimeError('Missing frame-boundary CPU state')
            if not (result/'frame-000001.psg.bin').is_file():
                raise RuntimeError('Missing frame PSG writes')
        differences=compare(actual/'manifest.txt',expected/'manifest.txt')
        if differences:raise RuntimeError('\n'.join(differences))
        wanted='1 '+('59746' if delayed_vram else '59736')
        if (expected/'boundaries.txt').read_text().strip()!=wanted:
            raise RuntimeError('Reference did not capture the instruction end: '+wanted)
    # HALT must still produce a boundary capture when no interrupt wakes it.
    data=bytearray(512*1024);data[0]=0x76
    rom=root/'halt-idle.sms';rom.write_bytes(data)
    rec=root/'halt-idle.psrec';rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
        '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
    idle=root/'halt-idle-ce'
    subprocess.run([sys.executable,'-B',driver,executable,rom,rec,'--dump','all','--out',idle,
                    '--reference-label','idle HALT regression'],check=True,timeout=12)
    cpu=(idle/'frame-000001.cpu.bin').read_bytes()
    if len(cpu)!=30 or cpu[-1]!=1:raise RuntimeError('HALT state missing at frame boundary')
    idle_ps=root/'halt-idle-ps'
    subprocess.run([runner,rom,rec,'--dump','all','--out',idle_ps],check=True)
    ps=(idle_ps/'frame-000001.cpu.bin').read_bytes()
    if cpu[:25]!=ps[:25] or cpu[26:]!=ps[26:] or cpu[25]==ps[25]:
        raise RuntimeError('HALT must expose the known reference R difference')
    if not any('cpu: first differing frame 1' in line for line in compare(idle_ps/'manifest.txt',idle/'manifest.txt')):
        raise RuntimeError('CPU checkpoint hid the HALT difference')
    # VRAM writes at PSG-looking addresses are not port writes.
    data=bytearray(512*1024);data[:12]=bytes.fromhex('3e40 d3bf 3e40 d3bf 3e88 d3be')
    rom=root/'vram-alias.sms';rom.write_bytes(data)
    rec=root/'vram-alias.psrec';rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
        '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
    actual=root/'vram-alias-ps';expected=root/'vram-alias-ce'
    subprocess.run([runner,rom,rec,'--dump','all','--out',actual],check=True)
    subprocess.run([sys.executable,'-B',driver,executable,rom,rec,'--dump','all','--out',expected,
                    '--reference-label','VRAM alias regression'],check=True)
    for result in (actual,expected):
        if (result/'frame-000001.psg.bin').read_bytes():raise RuntimeError('VRAM write falsely reported as PSG')
    # Interleaved transfers exercise alias filtering while PSG is active.
    data=bytearray(512*1024)
    code=bytearray.fromhex('3e40 d3bf 3e40 d3bf')
    for i in range(64):code.extend((0x3e,i,0xd3,0xbe,0x3e,0x90|(i&15),0xd3,0x7f))
    data[:len(code)]=code
    rom=root/'interleaved.sms';rom.write_bytes(data)
    rec=root/'interleaved.psrec';rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
        '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
    actual=root/'interleaved-ps';expected=root/'interleaved-ce'
    subprocess.run([runner,rom,rec,'--dump','all','--out',actual],check=True)
    subprocess.run([sys.executable,'-B',driver,executable,rom,rec,'--dump','all','--out',expected,
                    '--reference-label','interleaved regression'],check=True)
    for result in (actual,expected):
        events=(result/'frame-000001.psg.bin').read_bytes()
        if len(events)!=64*3 or list(events[2::3])!=[0x90|(i&15) for i in range(64)]:
            raise RuntimeError('Interleaved VRAM and PSG writes were confused')
    differences=compare(actual/'manifest.txt',expected/'manifest.txt')
    if differences:raise RuntimeError('\n'.join(differences))
    # Two authored PSG OUTs establish both bytes and their exact cycle offsets.
    data=bytearray(512*1024);data[:8]=bytes.fromhex('3e90 d37f 3e9f d340')
    rom=root/'psg.sms';rom.write_bytes(data)
    rec=root/'psg.psrec';rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
        '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
    actual=root/'psg-ps';expected=root/'psg-ce'
    subprocess.run([runner,rom,rec,'--dump','all','--out',actual],check=True)
    subprocess.run([sys.executable,'-B',driver,executable,rom,rec,'--dump','all','--out',expected,
                    '--reference-label','PSG regression'],check=True)
    for result in (actual,expected):
        if (result/'frame-000001.psg.bin').read_bytes()!=bytes.fromhex('0011 90 0023 9f'):
            raise RuntimeError('PSG write bytes or timestamps differ')
    differences=compare(actual/'manifest.txt',expected/'manifest.txt')
    if differences:raise RuntimeError('\n'.join(differences))
print('Reference CPU, boundary, VRAM and PSG comparisons passed')
