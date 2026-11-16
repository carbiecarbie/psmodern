"""Exercise save exclusion in real PSModern processes, including a crash."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

exe,rom=map(Path,sys.argv[1:3])
if not rom.is_file():raise SystemExit(77)

def read_save(path,seconds=10):
    """The game replaces its save atomically, and Windows refuses a read while the
    replacement happens (and before the file exists): wait for it instead of failing."""
    deadline=time.monotonic()+seconds
    while True:
        try:return path.read_bytes()
        except (PermissionError,FileNotFoundError):
            if time.monotonic()>deadline:raise
            time.sleep(.02)

def remove_folder(folder):
    """A killed process lets go of its lock file a moment later on Windows."""
    for _ in range(100):
        shutil.rmtree(folder,ignore_errors=True)
        if not Path(folder).exists():return
        time.sleep(.05)

folder=tempfile.mkdtemp(prefix='psmodern-lock-')
try:
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
        # A loaded machine starts the game slowly: wait for the overwrite, up to 30 s.
        deadline=time.monotonic()+30
        while read_save(save)==foreign and time.monotonic()<deadline:time.sleep(.05)
        if read_save(save)==foreign:raise RuntimeError('Game did not overwrite the foreign save')
        if legacy.read_bytes()!=foreign:raise RuntimeError('Import or play modified the legacy save')
        if read_save(Path(str(save)+'.bak'))!=foreign:raise RuntimeError('Backup did not preserve the foreign image')
        second=start()
        try: _,error=second.communicate(timeout=15)
        except subprocess.TimeoutExpired:raise RuntimeError('Second instance did not refuse the open save')
        if second.returncode==0 or 'save is already open' not in error.lower():raise RuntimeError('Missing save-lock diagnostic: '+error)
        first.kill();first.wait(timeout=30)
        third=start();time.sleep(2)
        if third.poll() is not None:raise RuntimeError('Save stayed locked after crash: '+third.stderr.read())
    finally:
        for process in (first,second,third):
            if process and process.poll() is None:process.kill();process.wait(timeout=30)
            if process and process.stderr:process.stderr.close()
finally:
    remove_folder(folder)
print('A second instance is refused and the lock is released after a crash')
