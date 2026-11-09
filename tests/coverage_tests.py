"""Authored census fixtures: completeness, counters and minimum coverage."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from generate_coverage import CHECKPOINTS, COLUMNS, generate


def rejects(root, fragment):
    try:
        generate(root)
    except (ValueError, FileNotFoundError) as error:
        assert fragment in str(error), error
    else:
        raise AssertionError('Invalid census accepted')


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    header = '\t'.join(COLUMNS) + '\n'
    # One fallback has both IRQ and frame reasons; overlapping reasons are valid.
    row = '00:0339\t99\t1\t0\t1\t0\t1\t0\t0\t0\t0\n'
    for version in ('us', 'br'):
        (root/version).mkdir()
        for name in CHECKPOINTS:
            (root/version/f'{name}.tsv').write_text(header+row, encoding='ascii')
    output = generate(root)
    assert '| Total | All twelve | 1,188 | 12 | 99.000% |' in output
    target = root/'br'/'boss.tsv'
    target.unlink()
    rejects(root, 'boss.tsv')
    for invalid, fragment in (
        ('00:0339\t1\t99\t99\t0\t0\t0\t0\t0\t0\t0\n', 'below 90%'),
        (row.replace('99\t1', '-1\t1'), 'Invalid census counters'),
        (row+row, 'repeated'),
        (row.replace('00:0339', '20:0339'), 'routine entry'),
        (row.replace('00:0339', '00:0340'), 'different routine sets'),
        (row.replace('99\t1\t0\t1', '99\t1\t0\t2'), 'exceeds'),
        ('00:0339\t99\t1\t0\t0\t0\t0\t0\t0\t0\t0\n', 'without a reason'),
    ):
        target.write_text(header+invalid, encoding='ascii')
        rejects(root, fragment)
print('Coverage fixtures passed')
