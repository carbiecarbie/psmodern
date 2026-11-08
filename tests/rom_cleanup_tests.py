"""Exercise test-folder isolation and Windows cleanup with actual open handles."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

if os.name!='nt':raise SystemExit(77)
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,
                            ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
kernel.CreateFileW.restype=ctypes.c_void_p
kernel.CloseHandle.argtypes=[ctypes.c_void_p]
kernel.CloseHandle.restype=ctypes.c_int
def check(value,message):
    if not value:raise RuntimeError(message)
def lock(directory):
    handle=kernel.CreateFileW(str(directory),0x80000000,3,None,3,0x02000000,None)
    if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    return handle

with tempfile.TemporaryDirectory(prefix='rom-cleanup-') as folder:
    root=Path(folder);base=root/'shared';base.mkdir()
    sentinel=base/'sentinel.txt';sentinel.write_text('Owned by the caller')
    handle=lock(base)
    try:
        result=subprocess.run([sys.argv[1],base],capture_output=True,text=True)
        check(result.returncode==0,('Caller-folder lock must not fail rom_unit',result.stderr))
        check(sentinel.read_text()=='Owned by the caller','rom_unit removed caller-owned files')
        # Concurrent runs must not write or remove each other's fixtures.
        children=[subprocess.Popen([sys.argv[1],base],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) for _ in range(2)]
        for child in children:
            _,error=child.communicate(timeout=10)
            check(child.returncode==0,('Concurrent rom_unit failed',error))
    finally:kernel.CloseHandle(handle)

    for transient in (True,False):
        gate=root/('transient' if transient else 'persistent')
        ready=Path(str(gate)+'.ready')
        process=subprocess.Popen([sys.argv[1],base,'--cleanup-gate',gate],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        deadline=time.monotonic()+5
        while not ready.exists() and time.monotonic()<deadline and process.poll() is None:
            time.sleep(0.001)
        if not ready.exists():
            process.kill();_,error=process.communicate()
            raise RuntimeError(('rom_unit did not reach its cleanup gate',error))
        directory=base/ready.read_text()
        handle=lock(directory)
        releaser=None
        try:
            gate.touch() # Release the child only with the directory lock held.
            if transient:
                def release():
                    time.sleep(0.1);kernel.CloseHandle(handle)
                releaser=threading.Thread(target=release);releaser.start()
            _,error=process.communicate(timeout=10)
            if releaser:releaser.join()
            check(process.returncode==(0 if transient else 1),('Cleanup did not distinguish transient/persistent locks',error))
            check((not directory.exists()) if transient else 'cleanup' in error.lower(),'Cleanup result or diagnostic was lost')
        finally:
            if not transient:kernel.CloseHandle(handle)
            if process.poll() is None:process.kill();process.communicate()
    invalid=root/'not-a-directory';invalid.write_text('fixture')
    check(subprocess.run([sys.argv[1],invalid],capture_output=True).returncode!=0,'A real test failure was hidden')
print('Isolated concurrent ROM tests, transient cleanup retry and persistent failure reporting passed')
