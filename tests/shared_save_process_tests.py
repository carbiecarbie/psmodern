"""Exercise migration recovery and OS process locks with no real saves."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

exe=Path(sys.argv[1]).resolve()
suffixes=('', '.bak', '.options', '.options.bak', '.automap', '.automap.bak')
legacy='phantasy-star-us-rev-a.sav'
shared='phantasy-star.sav'

def fixture(root):
    for i,suffix in enumerate(suffixes):
        (root/(legacy+suffix)).write_bytes(bytes([17])*8192 if i==0 else bytes([i,0,255])*7)

def run(root, phase='none'):
    return subprocess.run([str(exe),'--run',str(root),phase],capture_output=True,text=True,timeout=20)

def equal(root):
    for suffix in suffixes:
        assert (root/'shared'/(shared+suffix)).read_bytes()==(root/(legacy+suffix)).read_bytes(),suffix

def ready(proc,root):
    deadline=time.monotonic()+15
    while not (root/'ready').exists() and proc.poll() is None and time.monotonic()<deadline:
        time.sleep(.02)
    if not (root/'ready').exists():
        proc.terminate();proc.wait(timeout=5)
        raise RuntimeError('worker did not reach coordination point')

def finish(proc,root,success=True):
    (root/'release').write_text('go')
    out,err=proc.communicate(timeout=20)
    if (proc.returncode==0)!=success:
        raise RuntimeError(f'worker exit {proc.returncode}: {out} {err}')

for phase in ('stage','copy1','copy2','copy3','copy4','copy5','copy6','before','after'):
    with tempfile.TemporaryDirectory(prefix='psmodern-shared-crash-') as tmp:
        root=Path(tmp);fixture(root)
        originals={s:((root/(legacy+s)).read_bytes(),(root/(legacy+s)).stat().st_mtime_ns) for s in suffixes}
        result=run(root,phase)
        if result.returncode!=23: raise RuntimeError(f'{phase}: expected 23, got {result.returncode}: {result.stderr}')
        assert (root/'shared').exists()==(phase=='after'),phase
        if phase=='after': equal(root)
        result=run(root)
        if result.returncode: raise RuntimeError(f'{phase}: retry failed: {result.stderr}')
        equal(root)
        for s,(contents,mtime) in originals.items():
            path=root/(legacy+s)
            assert path.read_bytes()==contents and path.stat().st_mtime_ns==mtime,(phase,s)

# One in-flight import owns the global nonblocking lock; a second initializer
# cannot race it, and the completed store can be reopened after lock release.
with tempfile.TemporaryDirectory(prefix='psmodern-shared-concurrent-') as tmp:
    root=Path(tmp);fixture(root)
    proc=subprocess.Popen([str(exe),'--hold',str(root),'none'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        ready(proc,root)
        assert run(root).returncode!=0,'concurrent initializer unexpectedly acquired import lock'
        assert not (root/'shared').exists()
        finish(proc,root);equal(root)
        assert run(root).returncode==0,'published store cannot be reopened'
    finally:
        if proc.poll() is None: proc.kill();proc.wait()

# A foreign publication between staging and rename must never be replaced.
with tempfile.TemporaryDirectory(prefix='psmodern-shared-noclobber-') as tmp:
    root=Path(tmp);fixture(root)
    proc=subprocess.Popen([str(exe),'--hold',str(root),'none'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        ready(proc,root);(root/'shared').mkdir();(root/'shared'/'foreign').write_bytes(b'keep')
        finish(proc,root,success=False)
        assert (root/'shared'/'foreign').read_bytes()==b'keep'
        assert not (root/'shared'/shared).exists()
        assert run(root).returncode==0,'existing shared directory must remain authoritative'
    finally:
        if proc.poll() is None: proc.kill();proc.wait()

# Verify the independent OS process locks, including a locked legacy candidate
# when the other candidate remains available and requires an explicit choice.
for kind in ('shared-import.lock','shared/phantasy-star.sav.lock','phantasy-star-brazil.sav.lock','phantasy-star-brazil.sav'):
    with tempfile.TemporaryDirectory(prefix='psmodern-shared-lock-') as tmp:
        root=Path(tmp);fixture(root)
        if kind.startswith('shared/'): (root/'shared').mkdir()
        if 'brazil' in kind: (root/'phantasy-star-brazil.sav').write_bytes(bytes([20])*8192)
        proc=subprocess.Popen([str(exe),'--lock',str(root),kind],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            ready(proc,root); result=run(root)
            if 'brazil' in kind:
                assert result.returncode==0,result.stderr
                equal(root)
            else: assert result.returncode!=0,'locked storage was unexpectedly opened'
            finish(proc,root)
        finally:
            if proc.poll() is None: proc.kill();proc.wait()
print('Nine crash boundaries, concurrent/no-clobber publication and three process locks and an unreadable candidate passed.')
