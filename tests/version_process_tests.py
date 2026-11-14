"""Check that psmodern.exe --version prints only the version and touches nothing."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

exe=Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='psmodern-version-') as folder:
    env=os.environ.copy();env.update(APPDATA=folder,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    done=subprocess.run([exe.resolve(),'--version'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    if done.returncode!=0:raise RuntimeError('--version exited with %d: %r'%(done.returncode,done.stderr))
    if not re.fullmatch(rb'PSModern \d+\.\d+\.\d+\r?\n',done.stdout):raise RuntimeError('Unexpected --version output: %r'%done.stdout)
    if done.stdout.rstrip(b'\r\n')!=b'PSModern 0.5.0':raise RuntimeError('Unexpected version: %r'%done.stdout)
    if done.stderr:raise RuntimeError('--version wrote to stderr: %r'%done.stderr)
    if list(Path(folder).iterdir()):raise RuntimeError('--version created files under APPDATA')
print('--version prints the version and creates no PSModern folder')
