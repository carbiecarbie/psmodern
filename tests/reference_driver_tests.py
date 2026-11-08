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
    for delayed_vram in (False,True):
        data=bytearray(512*1024)
        # Set R1, then enable INT just before the frame boundary. The NOP
        # finishes at 59736; IRQ entry must not leak its stack into the dump.
        data[:8]=bytes.fromhex('3e60 d3bf 3e81 d3bf')
        ei=8+(59728-36)//4
        data[ei]=0xfb
        if delayed_vram:
            # RET NZ is not taken (5 T); EI + OUT ends at 59746, with its
            # queued VRAM transfer completing at 59759 during IRQ entry.
            data=bytearray(512*1024)
            setup=bytes.fromhex('3e60 d3bf 3e81 d3bf 010000')
            data[:len(setup)]=setup
            at=len(setup)+(59726-46)//4
            data[at:at+4]=bytes.fromhex('c0 fb d3 be')
        stem='vram' if delayed_vram else 'nop'
        rom=root/(stem+'.sms');rom.write_bytes(data)
        rec=root/(stem+'.psrec')
        rec.write_text('psmodern-recording 1\nrom '+hashlib.sha1(data).hexdigest()+
                       '\ncart-ram '+hashlib.sha1(bytes(8192)).hexdigest()+'\nend 1\n',encoding='ascii',newline='\n')
        actual=root/(stem+'-ps');expected=root/(stem+'-saída 漢字')
        subprocess.run([runner,rom,rec,'--dump','all','--out',actual],check=True)
        subprocess.run([sys.executable,driver,executable,rom,rec,'--dump','all','--out',expected,
                        '--reference-label','instruction boundary regression'],check=True)
        differences=compare(actual/'manifest.txt',expected/'manifest.txt')
        if differences:raise RuntimeError('\n'.join(differences))
        wanted='1 '+('59746' if delayed_vram else '59736')
        if (expected/'boundaries.txt').read_text().strip()!=wanted:
            raise RuntimeError('Reference did not capture the instruction end: '+wanted)
print('Reference captures instruction end before IRQ entry, including pending VRAM, on Unicode paths')
