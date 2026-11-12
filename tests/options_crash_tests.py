"""Terminate real processes at each companion/raw publication boundary."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager

@contextmanager
def temporary():
    holder=tempfile.TemporaryDirectory(prefix='psmodern-options-crash-')
    try:
        yield holder.name
    finally:
        for attempt in range(6):
            try:
                holder.cleanup()
                break
            except OSError:
                if attempt==5:
                    print(f'Warning: temporary cleanup failed: {holder.name}',file=sys.stderr)
                else:
                    time.sleep(.04)

exe=Path(sys.argv[1]).resolve()
for phase in ('backup-before','backup-after','metadata-before','metadata-after','raw-before','raw-after'):
    with temporary() as folder:
        root=Path(folder)
        raw=root/'phantasy-star-us-rev-a.sav'
        original=bytes([44])+bytes(8191)
        raw.write_bytes(original)
        digest=hashlib.sha256(original).hexdigest()
        text=f'psmodern-options 1\ncommitted {digest} 50 400\npending -\nbackup -\n'
        metadata=Path(str(raw)+'.options')
        metadata.write_bytes(text.encode('ascii'))
        result=subprocess.run([exe,'--crash-worker',root,phase],capture_output=True,text=True,timeout=10)
        if result.returncode!=23:
            raise RuntimeError(f'{phase}: expected intentional process exit23, got {result.returncode}: {result.stdout} {result.stderr}')
        expected=bytes([45])+bytes(8191) if phase=='raw-after' else original
        if raw.read_bytes()!=expected:
            raise RuntimeError(f'{phase}: raw SRAM was neither complete old nor expected complete new image')
        rows=metadata.read_text(encoding='ascii').splitlines()
        selected=next((record for role in ('pending','committed','backup') for row in rows[1:]
                       if row.split()[0]==role and (record:=row.split()[1:])[0]==hashlib.sha256(expected).hexdigest()),None)
        if selected is None or selected[1:3]!=['50','400'] or selected[3:] not in ([],['0']):  # format 1, or 2 with automap off
            raise RuntimeError(f'{phase}: companion cannot recover options matching the durable SRAM')
        backup=Path(str(raw)+'.bak')
        if backup.exists() and backup.read_bytes()!=original:
            raise RuntimeError(f'{phase}: raw backup changed bytes')
        companion_backup=Path(str(metadata)+'.bak')
        if phase!='backup-before':
            if not companion_backup.is_file() or companion_backup.read_bytes()!=text.encode('ascii'):
                raise RuntimeError(f'{phase}: companion backup is absent or no longer describes the original image/options')
        if phase in ('metadata-before','metadata-after','raw-before','raw-after'):
            if not backup.is_file() or backup.read_bytes()!=original:
                raise RuntimeError(f'{phase}: raw backup must already be published before main companion staging')
        elif backup.exists():
            raise RuntimeError(f'{phase}: raw backup unexpectedly preceded companion backup publication')
        if phase in ('backup-before','backup-after','metadata-before') and metadata.read_bytes()!=text.encode('ascii'):
            raise RuntimeError(f'{phase}: main companion changed before its publication boundary')
        if list(root.glob('*.tmp-*')):
            raise RuntimeError(f'{phase}: publication left a temporary file')
        # Starting again proves the operating-system lock dies with the process.
        recovery=subprocess.run([exe,'--crash-worker',root,'none'],capture_output=True,text=True,timeout=10)
        if recovery.returncode:
            raise RuntimeError(f'{phase}: restart/retry failed: {recovery.stdout} {recovery.stderr}')
print('Six process-crash boundaries recover complete raw SRAM and matching preferences.')
