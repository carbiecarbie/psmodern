"""Compare replay manifests without displaying captured byte values."""
import argparse
from pathlib import Path
import re

COMPONENTS = ('picture', 'ram', 'vram', 'cram', 'cart-ram')
SIZES = (49152, 8192, 16384, 32, 8192)
HEADER = 'frame ' + ' '.join(COMPONENTS)

def read_manifest(path):
    lines = Path(path).read_text(encoding='ascii').splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError('Invalid manifest header')
    rows = []
    for n, line in enumerate(lines[1:], 1):
        fields = line.split(' ')
        if len(fields) != 6 or fields[0] != str(n) or any(not re.fullmatch('[0-9a-f]{40}', x) for x in fields[1:]):
            raise ValueError(f'Invalid manifest line {n+1}')
        rows.append(fields[1:])
    return rows

def compare(left, right, stack_ranges=()):
    left, right = Path(left), Path(right)
    a, b = read_manifest(left), read_manifest(right)
    result=[];seen=set();nonstack=None
    for n in range(max(len(a), len(b))):
        if n >= min(len(a), len(b)):
            result.append(f'First difference: frame {n+1}: missing frame');break
        different = [i for i in range(5) if a[n][i] != b[n][i]]
        if different:
            if not result:result.append(f'First difference: frame {n+1}: ' + ', '.join(COMPONENTS[i] for i in different))
            for i in different:
                first=i not in seen
                if first:result.append(f'{COMPONENTS[i]}: first differing frame {n+1}');seen.add(i)
                if not first and (i!=1 or nonstack is not None):continue
                name = f'frame-{n+1:06d}.{COMPONENTS[i]}.bin'
                x, y = left.parent/name, right.parent/name
                if x.exists() and y.exists():
                    xb, yb = x.read_bytes(), y.read_bytes()
                    if len(xb) != SIZES[i] or len(yb) != SIZES[i]:
                        raise ValueError(f'Invalid dump size: {name}')
                    offset = next((j for j in range(len(xb)) if xb[j] != yb[j]), None)
                    if offset is None and first:
                        result.append(f'{COMPONENTS[i]}: dumps equal despite different manifest hashes')
                    elif offset is not None and first:
                        address = f' (work RAM ${0xc000+offset:04X})' if i == 1 else ''
                        values=f', left={xb[offset]:02X} right={yb[offset]:02X}' if i==1 else ''
                        result.append(f'{COMPONENTS[i]}: first offset 0x{offset:04X}{address}{values}')
                    if i==1 and nonstack is None:
                        j=next((j for j in range(len(xb)) if xb[j]!=yb[j] and not any(lo<=0xc000+j<=hi for lo,hi in stack_ranges)),None)
                        if j is not None:nonstack=f'RAM outside excluded stack ranges: frame {n+1}, ${0xc000+j:04X}, left={xb[j]:02X} right={yb[j]:02X}'
    if 1 in seen:result.append(nonstack or 'RAM outside excluded stack ranges: none found in available dumps')
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left'); parser.add_argument('right')
    parser.add_argument('--stack-range',action='append',default=[],help='Inclusive work RAM addresses in hex, e.g. CAF8:CAFB; excludes only from the additional RAM summary')
    args = parser.parse_args()
    try:
        ranges=[tuple(int(x,16) for x in item.split(':')) for item in args.stack_range]
        if any(len(r)!=2 or not 0xc000<=r[0]<=r[1]<=0xdfff for r in ranges):raise ValueError('Invalid stack range')
        differences = compare(args.left, args.right,ranges)
        print('\n'.join(differences) if differences else 'Identical manifests')
        raise SystemExit(bool(differences))
    except (OSError, ValueError) as error:
        parser.exit(2, str(error)+'\n')
