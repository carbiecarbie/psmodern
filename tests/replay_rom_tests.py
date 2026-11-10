from pathlib import Path
import subprocess
import sys
import tempfile
def check(condition,detail='Test check failed'):
    if not condition:raise RuntimeError(detail)

runner,rom,recording,reference=sys.argv[1:]
if not rom or not Path(rom).exists():raise SystemExit(77)
with tempfile.TemporaryDirectory() as folder:
    outputs=[]
    for n in range(2):
        out=Path(folder)/str(n)
        subprocess.run([runner,rom,recording,'--dump','all','--out',str(out)],check=True)
        outputs.append({p.name:p.read_bytes() for p in out.iterdir()})
    check(outputs[0]==outputs[1], 'Explicit test check failed')
print('Identical manifests, all dumps and PSG log on two runs')
