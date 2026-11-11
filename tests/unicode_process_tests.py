"""Start the real program on a ROM and a profile in non-ASCII folders."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

exe,rom=map(Path,sys.argv[1:3])
if not rom.is_file():raise SystemExit(77)
# Both folders hold characters outside every ANSI code page at once.
with tempfile.TemporaryDirectory(prefix='psmodern-rom-\xe7\xe3o-\u65e5\u672c-') as rom_folder,\
     tempfile.TemporaryDirectory(prefix='psmodern-profile-\xe9\xf1-\u65e5\u672c-') as profile:
    copy=Path(rom_folder)/'Phantasy Star \xe7\xe3o \u65e5\u672c.sms'
    shutil.copyfile(rom,copy)
    env=os.environ.copy();env.update(APPDATA=profile,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    process=subprocess.Popen([exe.resolve(),copy],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
    try:
        time.sleep(1)
        if process.poll() is not None:raise RuntimeError('Exited during startup (%d): %s'%(process.returncode,process.stderr.read()))
        lock=Path(profile)/'PSModern'/'phantasy-star-us-rev-a.sav.lock'
        if not lock.is_file():raise RuntimeError('No save lock under the non-ASCII profile: %s'%lock)
    finally:
        if process.poll() is None:process.kill()
        process.wait(timeout=5)
        process.stderr.close()
print('A ROM path and a profile outside the ANSI code page start the game')
