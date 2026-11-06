import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compare_frames import compare, HEADER
from reference import load_recording

runner=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder);rom=root/'synthetic.sms';rom.write_bytes(bytes(512*1024))
    rec=root/'test.psrec';header='psmodern-recording 1\nrom '+hashlib.sha1(rom.read_bytes()).hexdigest()+'\ncart-ram '+hashlib.sha1(bytes([1])*8192).hexdigest()+'\n'
    rec.write_text(header+'end 2\n',encoding='ascii',newline='\n')
    assert load_recording(rec)[2]==2
    args=[str(runner),str(rom),str(rec),'--dump','all','--out',str(root/'out')]
    def rejects(arguments,diagnostic):
        result=subprocess.run(arguments,capture_output=True,text=True)
        assert result.returncode and diagnostic in result.stderr,(result.returncode,result.stderr)
    rejects([str(runner)],'Usage:')
    for selection in ('0','3','2-1','1,','x','1--2'):
        bad=args.copy();bad[4]=selection;rejects(bad,'dump')
    rejects(args,'cartridge RAM SHA-1 mismatch')
    cart=root/'cart';cart.write_bytes(b'x');rejects(args+['--cart-ram',str(cart)],'8192')
    rec.write_text(header.replace(hashlib.sha1(bytes([1])*8192).hexdigest(),hashlib.sha1(bytes(8192)).hexdigest())+'end 2\n',encoding='ascii',newline='\n')
    (root/'out').mkdir();rejects(args,'already exists')
    a=root/'a';b=root/'b';a.mkdir();b.mkdir()
    row='1 '+' '.join(['0'*40]*5)+'\n'
    for p in (a,b):(p/'manifest.txt').write_text(HEADER+'\n'+row,encoding='ascii')
    assert compare(a/'manifest.txt',b/'manifest.txt')==[]
    (b/'manifest.txt').write_text(HEADER+'\n'+row.replace('0'*40,'1'*40,1),encoding='ascii')
    (a/'frame-000001.picture.bin').write_bytes(bytes(49152));(b/'frame-000001.picture.bin').write_bytes(bytes(17)+b'x'+bytes(49134))
    result=compare(a/'manifest.txt',b/'manifest.txt');assert 'frame 1: picture' in result[0] and any('0x0011' in line for line in result)
    # A later RAM divergence must be reported even after the first picture difference.
    (a/'manifest.txt').write_text(HEADER+'\n'+row+row.replace('1 ','2 ',1),encoding='ascii')
    second='2 '+'0'*40+' '+'1'*40+' '+' '.join(['0'*40]*3)+'\n'
    (b/'manifest.txt').write_text(HEADER+'\n'+row.replace('0'*40,'1'*40,1)+second,encoding='ascii')
    (a/'frame-000002.ram.bin').write_bytes(bytes(8192));(b/'frame-000002.ram.bin').write_bytes(bytes(5)+b'x'+bytes(8186))
    result=compare(a/'manifest.txt',b/'manifest.txt',[(0xc000,0xc003)])
    assert any('ram: first differing frame 2' in line for line in result) and any('$C005' in line for line in result)

    # Exact equality is required for every component, including VRAM.
    rec.write_text(header.replace(hashlib.sha1(bytes([1])*8192).hexdigest(),
                                  hashlib.sha1(bytes(8192)).hexdigest())+'end 2\n',encoding='ascii',newline='\n')
    out=root/'strict'
    subprocess.run([runner,rom,rec,'--dump','1','--out',out],check=True)
    rows=(out/'manifest.txt').read_text().splitlines()
    expected=root/'expected.txt'
    def checkpoint(lines,code):
        expected.write_text('\n'.join(lines)+'\n',encoding='ascii')
        result=subprocess.run([sys.argv[2],rom,rec,expected],capture_output=True,text=True)
        assert result.returncode==code,(result.stdout,result.stderr)
    checkpoint(rows,0)
    for component in range(1,6):
        changed=rows.copy(); fields=changed[1].split(); fields[component]='f'*40
        changed[1]=' '.join(fields); checkpoint(changed,1)
    checkpoint(rows[:-1],1)
    checkpoint(rows+[rows[-1]],1)
print('Runner arguments, synthetic comparisons and strict checkpoint checks passed')
