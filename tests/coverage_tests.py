"""Authored census fixtures: completeness, counters, units and minimum coverage."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from generate_coverage import ALL, COLUMNS, COUNTERS, checkpoints, generate

# The number of checkpoint censuses (six per version and the US-only ones).
N = len(checkpoints('us')) + len(checkpoints('br'))


def check(condition, detail):
    # Explicit, so the test still fails under -O or PYTHONOPTIMIZE.
    if not condition:
        raise RuntimeError(detail)


def row(entry, unit, cpp=0, fallback=0, forced=0, forced_failed=0, reasons=None, forced_reasons=None):
    """One census line; reasons and forced_reasons map a column name to a count."""
    counts = dict.fromkeys(COUNTERS, 0)
    counts.update(cpp=cpp, fallback=fallback, forced=forced, forced_failed=forced_failed)
    counts.update(reasons or {})
    counts.update(forced_reasons or {})
    return '\t'.join([entry, unit, *(str(counts[key]) for key in COUNTERS)]) + '\n'


def addr(bank, address):
    # Fixture addresses are formatted, so they are not citations of original code.
    return f'{bank:02X}:{address:04X}'


CTRL = '00:0339'
UNIT = addr(0, 0xF3C)
OPT = '00:05B1'
PROBE = addr(0, 0xF62)
FAR = addr(0, 0x1234)
BAD_BANK = addr(0x20, 0x339)
HEADER = '\t'.join(COLUMNS) + '\n'


def write_all(root, text, special=None):
    """Write `text` to every checkpoint; `special` maps (version, name) to other text."""
    for version in ('us', 'br'):
        (root/version).mkdir(exist_ok=True)
        for name in checkpoints(version):
            (root/version/f'{name}.tsv').write_text(HEADER+(special or {}).get((version, name), text), encoding='ascii')


def rejects(root, fragment, **options):
    try:
        generate(root, **options)
    except (ValueError, FileNotFoundError) as error:
        check(fragment in str(error), f'Wrong refusal: {error}')
    else:
        raise RuntimeError(f'Invalid census accepted (expected {fragment})')


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    # One fallback has both IRQ and frame reasons; overlapping reasons are valid.
    controller = row(CTRL, CTRL, 99, 1, reasons={'irq': 1, 'frame': 1})
    write_all(root, controller)
    output = generate(root)
    check(f'| Total | {ALL} | {99*N:,} | {N} | 0 | 0 | 99.000% |' in output, 'Wrong coverage totals')
    check('| Forced | Forced failed |' in output, 'Forced columns missing')
    check('## 00:0339\n\n| ROM' in output, 'A lone self-unit entry needs no segment heading')
    check('Forced reasons' not in output and 'forced_irq' not in output, 'Forced reasons shown without forced calls')
    check('state probes' not in output, 'Probe section without unreached entries')
    target = root/'br'/'boss.tsv'
    target.unlink()
    rejects(root, 'boss.tsv')
    for invalid, fragment in (
        (row(CTRL, CTRL, 1, 99, reasons={'irq': 99}), 'below 90%'),
        (controller.replace('99\t1', '-1\t1'), 'Invalid census counters'),
        (controller+controller, 'repeated'),
        (controller.replace(CTRL+'	'+CTRL, BAD_BANK+'	'+CTRL), 'routine entry'),
        (controller.replace(CTRL+'\t'+CTRL, CTRL+'\t'+BAD_BANK), 'routine unit'),
        (controller.replace('00:0339\t00:0339', '00:0340\t00:0340'), 'different routine sets'),
        (row(CTRL, CTRL, 99, 1, reasons={'irq': 2}), 'exceeds'),
        (row(CTRL, CTRL, 99, 1), 'without a reason'),
        (row(CTRL, CTRL, 99, 1, forced=100, reasons={'irq': 1}), 'Forced calls exceed'),
        (row(CTRL, CTRL, 99, 1, forced_failed=2, reasons={'irq': 1}), 'Forced calls exceed'),
        (row(CTRL, CTRL, 99, 1, forced=1, forced_reasons={'forced_irq': 2}, reasons={'irq': 1}), 'Forced reason exceeds'),
    ):
        target.write_text(HEADER+invalid, encoding='ascii')
        rejects(root, fragment)

# Unit grouping, zero-call entries and forced columns.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    rows = (row(CTRL, CTRL, 99, 1, reasons={'irq': 1}) +
            row(UNIT, UNIT, 50) +
            row(OPT, UNIT, 40, forced=3, forced_reasons={'forced_frame': 2, 'forced_irq': 1}) +
            row(PROBE, UNIT) +
            row('01:5FD8', '01:5FD8') +
            row('01:5FEC', '01:5FD8'))
    write_all(root, rows)
    # The real censuses run in original mode: forced calls are refused unless a test says otherwise.
    rejects(root, 'Forced calls in original mode')
    output = generate(root, allow_forced=True)
    check(output.count(f'## {UNIT}\n') == 1 and f'## {OPT}' not in output and f'## {PROBE}' not in output,
          'Segments must be grouped under their unit')
    check(f'### Segment {UNIT}' in output and f'### Segment {OPT}' in output, 'Segment headings missing')
    check(output.index(f'### Segment {OPT}') < output.index(f'### Segment {UNIT}') < output.index('## Covered by state probes only'),
          'Unit sections out of order')
    check(f'| Total | {ALL} | {40*N:,} | 0 | {3*N} | 0 | 100.000% |' in output, 'Forced totals missing')
    check('| ROM | Recording | forced_disabled | forced_irq | forced_nmi | forced_frame |' in output, 'Forced reasons missing')
    check(f'| Total | {ALL} | 0 | {N} | 0 | {2*N} | 0 | 0 | 0 | 0 |' in output, 'Forced reason totals wrong')
    tail = output[output.index('## Covered by state probes only'):]
    for entry, unit in ((PROBE, UNIT), ('01:5FD8', '01:5FD8'), ('01:5FEC', '01:5FD8')):
        check(f'- {entry} (unit {unit})' in tail, f'Unreached entry not listed: {entry}')
        check(f'Segment {entry}' not in output and f'## {entry}' not in output, f'Unreached entry has a table: {entry}')
    check(f'- {OPT}' not in tail and f'- {CTRL}' not in tail, 'Reached entry listed as unreached')
    # Units must agree across the censuses.
    (root/'us'/'boss.tsv').write_text(HEADER+rows.replace(OPT+'	'+UNIT, OPT+'	'+PROBE), encoding='ascii')
    rejects(root, 'disagree')
    # A forced body that fell back before its core step has a fallback with no window reason;
    # its forced failure accounts for it. Without the forced failure the fallback has no reason.
    fell = row(CTRL, CTRL, 99, 1, forced_failed=1)
    write_all(root, fell)
    rejects(root, 'Forced calls in original mode')
    check('| Forced | Forced failed |' in generate(root, allow_forced=True), 'Forced-failure fallback refused')
    write_all(root, row(CTRL, CTRL, 99, 1))
    rejects(root, 'without a reason', allow_forced=True)
    write_all(root, row(CTRL, CTRL, 99, 2, forced_failed=1, reasons={'irq': 1}))
    check('| Total |' in generate(root, allow_forced=True), 'Forced failure plus a reason must cover two fallbacks')
    write_all(root, row(CTRL, CTRL, 99, 3, forced_failed=1, reasons={'irq': 1}))
    rejects(root, 'without a reason', allow_forced=True)

# The 20-call gate and the one-C++-call rule, over all the censuses.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    base = row(CTRL, CTRL, 99, 1, reasons={'irq': 1})
    nothing = base + row(OPT, UNIT)

    def entry(cpp, fallback):
        return base + row(OPT, UNIT, cpp, fallback, reasons={'irq': fallback})

    def census(special):
        write_all(root, nothing, {key: entry(*counts) for key, counts in special.items()})

    opening = ('us', 'opening')
    census({opening: (18, 2)})       # 20 calls, exactly 90%
    generate(root)
    census({opening: (17, 3)})       # 20 calls, 85%
    rejects(root, f'below 90%: {OPT}')
    census({opening: (5, 14)})       # 19 calls: reported, not gated
    generate(root)
    census({opening: (4, 16)})       # 20 calls again
    rejects(root, f'below 90%: {OPT}')
    # The total over all the censuses gates, though no recording reaches 20 calls.
    census({('us', 'opening'): (5, 6), ('us', 'first-battle'): (5, 6), ('br', 'boss'): (5, 6)})
    rejects(root, f'below 90%: {OPT}')
    # Each recording is also gated on its own at 20 calls, whatever the total.
    census({('us', 'opening'): (15, 15), ('br', 'opening'): (1000, 0)})
    rejects(root, f'below 90%: us/opening/{OPT}')

    # An entry with calls and no C++ call needs a written reason on the allow-list.
    census({opening: (0, 3)})
    rejects(root, 'without a C++ call')
    output = generate(root, allowed={OPT: 'Only reached with an interrupt pending.'})
    check('## Allowed without C++ calls' in output and f'- {OPT}: Only reached with an interrupt pending.' in output,
          'Allow-list reason not shown')
    rejects(root, 'Invalid allow-list entry', allowed={OPT: '  '})
    rejects(root, 'Invalid allow-list entry', allowed={OPT: 'Reason.', FAR: 'Not in the census.'})
    # An allow-list entry that has C++ calls is stale.
    census({opening: (4, 0)})
    rejects(root, 'Allow-list entry has C++ calls', allowed={OPT: 'Stale.'})
    # An allow-listed entry the recordings never reach is stale too.
    write_all(root, nothing)
    rejects(root, 'never called', allowed={OPT: 'Stale.'})
    # An entry that is never reached needs neither: it is listed for state probes.
    write_all(root, nothing)
    check(f'- {OPT} (unit {UNIT})' in generate(root), 'Never-reached entry not listed')
# The US-only dungeon recordings are required for the US version and absent for the Brazilian one.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    write_all(root, row(CTRL, CTRL, 99, 1, reasons={'irq': 1}))
    check(not (root/'br'/'dungeon-walk-1.tsv').exists(), 'No Brazilian dungeon-walk census expected')
    generate(root)
    (root/'us'/'dungeon-continue.tsv').unlink()
    rejects(root, 'dungeon-continue.tsv')
print('Coverage fixtures passed')
