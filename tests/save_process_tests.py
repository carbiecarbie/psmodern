"""Exercise save exclusion in real PSModern processes, including a crash."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

exe,rom=map(Path,sys.argv[1:3])
if not rom.is_file():raise SystemExit(77)
with tempfile.TemporaryDirectory(prefix='psmodern-lock-') as folder:
    env=os.environ.copy();env.update(APPDATA=folder,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    def start():
        return subprocess.Popen([exe.resolve(),rom.resolve()],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    legacy=Path(folder)/'PSModern'/'phantasy-star-us-rev-a.sav'
    legacy.parent.mkdir(); foreign=bytes([0xa5])*8192;legacy.write_bytes(foreign)
    save=legacy.parent/'shared'/'phantasy-star.sav'
    first=start();second=None;third=None
    try:
        time.sleep(1)
        if first.poll() is not None:raise RuntimeError('First instance exited during startup: '+first.stderr.read())
        deadline=time.monotonic()+5
        while save.read_bytes()==foreign and time.monotonic()<deadline:time.sleep(.05)
        if save.read_bytes()==foreign:raise RuntimeError('Game did not overwrite the foreign save')
        if legacy.read_bytes()!=foreign:raise RuntimeError('Import or play modified the legacy save')
        if Path(str(save)+'.bak').read_bytes()!=foreign:raise RuntimeError('Backup did not preserve the foreign image')
        second=start()
        try: _,error=second.communicate(timeout=3)
        except subprocess.TimeoutExpired:raise RuntimeError('Second instance did not refuse the open save')
        if second.returncode==0 or 'save is already open' not in error.lower():raise RuntimeError('Missing save-lock diagnostic: '+error)
        first.kill();first.wait(timeout=5)
        third=start();time.sleep(1)
        if third.poll() is not None:raise RuntimeError('Save stayed locked after crash: '+third.stderr.read())
    finally:
        for process in (first,second,third):
            if process and process.poll() is None:process.kill();process.wait(timeout=5)
print('A second instance is refused and the lock is released after a crash')
