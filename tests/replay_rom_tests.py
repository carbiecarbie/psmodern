from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib
import subprocess
import sys
import tempfile
def check(condition,detail='Test check failed'):
    if not condition:raise RuntimeError(detail)

# Entries of the four option segments (encounter compare on the map and in
# dungeons, experience load and add) and the segments only a random fight
# reaches: the group choice and what follows the dungeon cell test.
OPTION_ENTRIES=('00:0F5B','01:5FB0','00:170D','00:1773')
FIGHT_ENTRIES=('01:5FD8','01:5FEC','00:0F62','00:0F6D')
COMPARE_ENTRIES=('00:0F5B','01:5FB0')

def replay(runner,rom,recording,out,cart,flags=(),dump='1'):
    command=[runner,rom,recording,'--dump',dump,'--out',str(out)]
    if cart:command+=['--cart-ram',str(cart)]
    subprocess.run(command+list(flags),check=True,capture_output=True)
    return out

def files(out):return {p.name:p.read_bytes() for p in out.iterdir()}

def census(out):
    lines=(out/'routine-census.tsv').read_text(encoding='ascii').splitlines()
    header=lines[0].split('\t')
    return {row[0]:dict(zip(header,[row[0],row[1],*map(int,row[2:])])) for row in map(lambda line:line.split('\t'),lines[1:])}

def calls(rows,entry):
    return rows[entry]['cpp']+rows[entry]['fallback'] if entry in rows else 0

def cartridge_image(recording,reference):
    # A recording that starts with nonzero cartridge RAM needs its image; without it the test skips.
    header=Path(recording).read_text(encoding='ascii').splitlines()[2]
    check(header.startswith('cart-ram '),'Recording header lacks the cartridge RAM digest')
    if header.split()[1]==hashlib.sha1(bytes(8192)).hexdigest():return None
    image=Path(reference)/'cart-ram.bin' if reference else None
    if not image or not image.is_file():raise SystemExit(77)
    return image

def enhanced(rows,flags):
    # An option segment whose option is on is always admitted: every call is forced, none falls back.
    # One whose option is at its default follows the original window rule and is never forced.
    given=dict(zip(flags[::2],flags[1::2]))
    active={'00:0F5B':'--encounter-rate','01:5FB0':'--encounter-rate','00:170D':'--experience-rate','00:1773':'--experience-rate'}
    reached=[]
    for entry in OPTION_ENTRIES:
        total=calls(rows,entry)
        if not total:continue
        row=rows[entry]
        check(row['forced_failed']==0,f'{entry} failed a forced call with {flags}: {row}')
        if given.get(active[entry],'100')!='100':
            check(row['fallback']==0 and row['forced']==total,f'{entry} forced {row["forced"]} of {total} calls, {row["fallback"]} fell back, with {flags}')
            reached.append((entry,total))
        else:
            check(row['forced']==0,f'{entry} forced calls with its option at the default: {row}')
    return reached

def fights(folder):
    # Across the recordings of one version at least one original-mode run starts a random fight, so the
    # zero-marker check at rate 0 is known to be able to fail. The options tests leave one file each.
    files_found=sorted(Path(folder).glob('*.txt')) if Path(folder).is_dir() else []
    if not files_found:raise SystemExit(77)
    totals={f.stem:int(f.read_text(encoding='ascii')) for f in files_found}
    print('Random fight marker calls in the original runs:',totals)
    check(any(totals.values()),'No recording of this version reaches a random fight, so the rate-0 check could not fail')

def main():
    if sys.argv[1]=='fights':return fights(sys.argv[2])
    runner,rom,recording=sys.argv[1:4]
    if not rom or not Path(rom).exists():raise SystemExit(77)
    mode=sys.argv[4] if len(sys.argv)>4 else 'determinism'
    with tempfile.TemporaryDirectory() as folder:
        folder=Path(folder)
        if mode=='determinism':
            outputs=[]
            for n in range(2):
                out=Path(folder)/str(n)
                subprocess.run([runner,rom,recording,'--dump','all','--out',str(out)],check=True)
                outputs.append({p.name:p.read_bytes() for p in out.iterdir()})
            check(outputs[0]==outputs[1], 'Explicit test check failed')
            print('Identical manifests, all dumps and PSG log on two runs')
            return
        # Options mode: argv[5] is the folder holding the recording's cartridge RAM image (or empty),
        # argv[6] is 'full' (default) or 'identity-only' (only the 100 and 100 comparison, for a hand check),
        # argv[7] optionally names the file that receives the original run's random fight marker calls.
        cart=cartridge_image(recording,sys.argv[5] if len(sys.argv)>5 else '')
        check(mode=='options','Unknown mode '+mode)
        identity_only=len(sys.argv)>6 and sys.argv[6]=='identity-only'
        passes={'plain':(),'hundred':('--encounter-rate','100','--experience-rate','100')}
        if not identity_only:
            passes['half-and-four']=('--encounter-rate','50','--experience-rate','400')
            passes['off-and-double']=('--encounter-rate','0','--experience-rate','200')
            # Encounters left alone, the play stays the original's until the first award, so the experience
            # segments are reached wherever the original reached them.
            passes['experience-only']=('--experience-rate','400')
        # The runs are independent, so they go side by side.
        with ThreadPoolExecutor(len(passes)) as pool:
            futures={name:pool.submit(replay,runner,rom,recording,folder/name,cart,flags) for name,flags in passes.items()}
            outs={name:future.result() for name,future in futures.items()}
        # Both flags at 100 play the original: identical manifests, sound log, census and dump.
        plain=files(outs['plain'])
        check(plain==files(outs['hundred']),'Explicit 100 and 100 differ from a run without flags')
        check({'manifest.txt','psg-writes.txt','routine-census.tsv'}<=set(plain),'The run lost an output file')
        original=census(outs['plain'])
        check(all(row['forced']==0 and row['forced_failed']==0 for row in original.values()),'Original mode counted forced calls')
        print('Flags at 100 and 100 give identical manifests, dump, sound log and census')
        # The game diverges from the recording by design: only the census is read.
        for name in ('half-and-four','off-and-double','experience-only'):
            if name not in outs:continue
            rows=census(outs[name])
            print(name,'option segments reached (entry, calls; all forced, none fell back):',enhanced(rows,passes[name]) or 'none')
            if name=='experience-only':
                for entry in ('00:170D','00:1773'):
                    if calls(original,entry):
                        check(calls(rows,entry)>0,f'{entry} not reached with experience at 400 although the original reached it')
            if name=='off-and-double':
                # No random fight starts: the group choice (01:5FD8, also the map check's tail
                # through 01:5FEC) and what follows the dungeon cell test are never reached,
                # while the checks themselves still run wherever the original reached them.
                for entry in FIGHT_ENTRIES:
                    check(calls(rows,entry)==0,f'{entry} reached with encounters off: {rows[entry]}')
                for entry in COMPARE_ENTRIES:
                    if calls(original,entry):
                        check(calls(rows,entry)>0,f'{entry} never reached with encounters off although the original reached it')
        markers={entry:calls(original,entry) for entry in FIGHT_ENTRIES}
        print('Random fight segments in the original run:',markers)
        if len(sys.argv)>7:
            Path(sys.argv[7]).parent.mkdir(parents=True,exist_ok=True)
            Path(sys.argv[7]).write_text(str(sum(markers.values())),encoding='ascii')
main()
