"""Authored census fixtures: completeness, counters, units and minimum coverage."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from generate_coverage import ALL, COLUMNS, COUNTERS, EVIDENCE, LOCATION, checkpoints, generate as strict_generate, read_extra_classes


def generate(root, **options):
    """The fixtures below exercise the counters with refused calls; the real rule (no refusal at all)
    is tested at the end with strict_generate."""
    options.setdefault('allow_refusals', True)
    return strict_generate(root, **options)

# All shared and release-specific checkpoint censuses.
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
# The reasons come from the census header: a census without some of today's
# reasons (milestone 14 retires them) is read, and mixed headers are refused.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    short = ('disabled', 'frame', 'guard')
    columns = ('entry', 'unit', 'cpp', 'fallback', 'forced', 'forced_failed', *short, *(f'forced_{reason}' for reason in short))
    line = '\t'.join([CTRL, CTRL, '99', '1', '0', '0', '0', '1', '0', '0', '0', '0']) + '\n'
    for version in ('us', 'br'):
        (root/version).mkdir()
        for name in checkpoints(version):
            (root/version/f'{name}.tsv').write_text('\t'.join(columns)+'\n'+line, encoding='ascii')
    output = generate(root)
    check('| ROM | Recording | disabled | frame | guard |' in output and 'ei_delay' not in output, 'Reasons not taken from the header')
    (root/'us'/'boss.tsv').write_text(HEADER+row(CTRL, CTRL, 99, 1, reasons={'irq': 1}), encoding='ascii')
    rejects(root, 'different fallback reason columns')
    (root/'us'/'boss.tsv').write_text('\t'.join(columns[:-1])+'\n'+line[:line.rindex('\t')]+'\n', encoding='ascii')
    rejects(root, 'Invalid census header')

# The transition census beside each checkpoint census: all or none, a section when present.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    write_all(root, row(CTRL, CTRL, 99, 1, reasons={'irq': 1}))
    check('Transition to rendered code' not in generate(root), 'Transition section without transition censuses')
    text = ('section\tkey\tdetail\tcount\n'
            f'final\t{CTRL}\treturned 0042\t7\n'
            f'rendered\t{CTRL}\tcore_final 0042\t5\n'
            f'origin\t{CTRL}\tcommon.cpp\t12\n'
            'native\t00:0059\trom\t3\n'
            'summary\tbody_irq\t-\t2\n'
            'summary\tbody_nmi\t-\t1\n')
    for version in ('us', 'br'):
        for name in checkpoints(version):
            (root/version/f'{name}.transition.tsv').write_text(text, encoding='ascii')
    output = generate(root)
    check(f'- {7*N:,} segment finals executed by the core and {5*N:,} rendered' in output and
          f'- {3*N:,} native core steps' in output and f'| common.cpp | {7*N:,} | {5*N:,} |' in output,
          'Transition totals wrong')
    (root/'br'/'boss.transition.tsv').unlink()
    rejects(root, 'Transition census missing')
    (root/'br'/'boss.transition.tsv').write_text('section\tkey\tcount\n', encoding='ascii')
    rejects(root, 'Invalid transition census header')
    (root/'br'/'boss.transition.tsv').write_text(text.replace('\t7\n', '\t-7\n'), encoding='ascii')
    rejects(root, 'Invalid transition census row')
print('Coverage fixtures passed')

# Milestone 14 (review R3 L5): the real censuses may hold no refused call.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    write_all(root, row(CTRL, CTRL, 99, 0))
    check('| Total |' in strict_generate(root), 'A census with no refusal is accepted')
    write_all(root, row(CTRL, CTRL, 99, 1, reasons={'irq': 1}))
    try:
        strict_generate(root)
    except ValueError as error:
        check(f'Refused calls in the recordings: {CTRL}' in str(error), f'Wrong refusal: {error}')
    else:
        raise RuntimeError('A census with a refused call was accepted')

# Milestone 19: the evidence categories, the kept group figures and the reasons file.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)/'checkpoints'
    root.mkdir()
    # The checkpoints reach CTRL on both versions and UNIT on the US one only;
    # OPT and PROBE are reached by no checkpoint.
    both = row(CTRL, CTRL, 99)
    write_all(root, both + row(UNIT, UNIT) + row(OPT, UNIT) + row(PROBE, UNIT),
              {('us', 'opening'): both + row(UNIT, UNIT, 5) + row(OPT, UNIT) + row(PROBE, UNIT)})
    reasons = Path(directory)/'reasons.tsv'
    reasons.write_text('segment\tgap\tevidence\treason\n', encoding='utf-8', newline='\n')

    def census_tree(name, files):
        """A group census root: {(version, file): text}."""
        tree = Path(directory)/name
        for (version, file), text in files.items():
            (tree/version).mkdir(parents=True, exist_ok=True)
            (tree/version/file).write_text(HEADER+text, encoding='ascii')
        return tree
    none = {'playthrough': 'none', 'routes': 'none'}
    report = {}
    output = strict_generate(root, groups=none, reasons_path=reasons, report=report)
    check('| Checkpoint recordings | 2 | 1 | 2 |' in output, 'Checkpoint category wrong')
    check('- Reached by no recording: 2 (0 of them dispatched only by a prepared-state route, 2 by neither a recording '
          'nor a route).' in output, 'Probe-only count wrong')
    check('| No reason yet | 0 | 2 | 1 | 3 |' in output, 'Rows without a reason not counted')
    check('- Reached by a US recording and by no Brazilian one: 1.' in output, 'US-only count wrong')
    check(output.count('no census recorded yet') == 2, 'Absent groups must say so')
    check(report['missing'] == {'probe-only': sorted([OPT, PROBE]), 'us-only': [UNIT]}, 'Missing reasons wrong')
    def rejects_with(fragment, **options):
        rejects(root, fragment, **options)
    rejects_with('Segments without a reason', groups=none, reasons_path=reasons, require_reasons=True)
    # The playthrough group reaches OPT on both versions and UNIT on the Brazilian one.
    links = census_tree('links', {('us', 'link-01.tsv'): both + row(UNIT, UNIT) + row(OPT, UNIT, 3) + row(PROBE, UNIT),
                                  ('br', 'link-01.tsv'): both + row(UNIT, UNIT, 1) + row(OPT, UNIT, 2) + row(PROBE, UNIT)})
    routes = census_tree('routes', {('us', 'object-0-E3.tsv'): both + row(UNIT, UNIT) + row(OPT, UNIT) + row(PROBE, UNIT, 1)})
    fresh = {'playthrough': links, 'routes': routes}
    output = strict_generate(root, groups=fresh, reasons_path=reasons, report=report)
    check('| Any recording | 3 | 3 | 3 |' in output and
          '- Reached by no recording: 1 (1 of them dispatched only by a prepared-state route, 0 by neither a recording '
          'nor a route).' in output and
          '- Reached by a US recording and by no Brazilian one: 0.' in output, 'Group categories wrong')
    check('from 1 censuses of this run' in output, 'Group source not stated')
    check(f'- {PROBE} (unit {UNIT}' in output and f'- {OPT} (unit' not in output, 'Probe-only list must use every recording')
    # A segment only a route dispatches is listed under the routes, not under the probes (review 2026-11-25).
    check('## Reached by prepared-state routes only' in output and '## Covered by state probes only' not in output,
          'A route-only segment listed as probe-only')
    check(output.index('## Reached by prepared-state routes only') < output.index(f'- {PROBE} (unit {UNIT}; reason: no reason yet)'),
          'Route-only segment not in its section')
    previous = Path(directory)/'previous.md'
    previous.write_text(output, encoding='utf-8', newline='\n')
    # Without the group census the previous figures are kept, and the output says so.
    kept = strict_generate(root, groups={'playthrough': ('keep', previous), 'routes': ('keep', previous)}, reasons_path=reasons)
    check(kept.count('figures kept from the previous generated file (previous.md)') == 2, 'Kept figures not stated')
    def figures(text):
        return [line for line in text.splitlines() if not line.startswith(('- Playthrough group', '- Prepared-state'))]
    check(figures(kept) == figures(output), 'Kept figures differ from the previous ones')
    # Never a silent fallback: nothing to keep is an error.
    previous.write_text('# Rewritten routine coverage\n', encoding='utf-8', newline='\n')
    rejects_with('No playthrough figures to keep', groups={'playthrough': ('keep', previous), 'routes': 'none'}, reasons_path=reasons)
    rejects_with('No census found', groups={'playthrough': Path(directory)/'missing', 'routes': 'none'}, reasons_path=reasons)
    refused = census_tree('refused', {('us', 'link-01.tsv'): row(CTRL, CTRL, 99, 1, reasons={'irq': 1}) + row(UNIT, UNIT) +
                                      row(OPT, UNIT) + row(PROBE, UNIT)})
    rejects_with('Refused calls', groups={'playthrough': refused, 'routes': 'none'}, reasons_path=reasons)
    # The reasons file: one reason per segment and gap, no stale entry, a strict format.
    def with_reasons(text):
        reasons.write_text('segment\tgap\tevidence\treason\n' + text, encoding='utf-8', newline='\n')
    with_reasons(f'{PROBE}\tprobe-only\tInference\tThe rare branch: an inn with a full party.\n')
    output = strict_generate(root, groups=fresh, reasons_path=reasons, require_reasons=True)
    check('| Inference | 1 | 0 | 0 | 1 |' in output and '| No reason yet | 0 | 0 | 0 | 0 |' in output,
          'Reasons by evidence wrong')
    check(f'- {PROBE} (unit {UNIT}; reason: Inference)' in output, 'Evidence label not listed')
    with_reasons(f'{PROBE}\tprobe-only\tUnknown\tOne.\n{PROBE}\tprobe-only\tUnknown\tTwo.\n')
    rejects_with('Two probe-only reasons', groups=fresh, reasons_path=reasons)
    with_reasons(f'{PROBE}\tprobe-only\tUnknown\tOne.\n{OPT}\tprobe-only\tROM run\tA recording reaches it now.\n')
    rejects_with('Stale reasons', groups=fresh, reasons_path=reasons)
    with_reasons(f'{UNIT}\tus-only\tROM run\tThe Brazilian chain reaches it now.\n')
    rejects_with('Stale reasons', groups=fresh, reasons_path=reasons)
    for bad, fragment in ((f'{PROBE}\tprobe-only\tUnknown\t \n', 'Invalid reason'),
                          (f'{PROBE}\tsometimes\tUnknown\tR.\n', 'Invalid reason'),
                          (f'{FAR}\tprobe-only\tUnknown\tR.\n', 'not registered'),
                          (f'{PROBE} probe-only Unknown R.\n', 'Invalid reason'),
                          (f'{PROBE}\tprobe-only\tR.\n', 'Invalid reason'),
                          (f'{PROBE}\tprobe-only\tProbably\tR.\n', 'Invalid evidence label'),
                          (f'{PROBE}\tprobe-only\tInference\tE26: town/area 4 row 27 col 21 (scene record).\n', 'Location data'),
                          (f'{PROBE}\tprobe-only\tInference\tA dungeon event on cell C8.\n', 'Location data'),
                          (f'{PROBE}\tprobe-only\tInference\tSEARCH at (28,1E).\n', 'Location data')):
        with_reasons(bad)
        rejects_with(fragment, groups=fresh, reasons_path=reasons)
    check(EVIDENCE == ('ROM run', 'ROM static', 'Inference', 'Unknown'), 'Evidence vocabulary changed')
    reasons.write_text('segment\tgap\treason\n', encoding='utf-8', newline='\n')
    rejects_with('Invalid reasons file', groups=fresh, reasons_path=reasons)
    reasons.write_text('segment\tgap\tevidence\treason\r\n', encoding='utf-8', newline='\n')
    rejects_with('Invalid reasons file', groups=fresh, reasons_path=reasons)

    # The inventory (optional hardening, review 2026-11-25): a fresh group census
    # directory must hold exactly the censuses the inventory lists.
    with_reasons(f'{PROBE}\tprobe-only\tInference\tR.\n')
    inventory = Path(directory)/'inventory.txt'

    def with_inventory(text):
        inventory.write_bytes(text.encode('utf-8'))
    with_inventory('# expected\n\nus link 01\nbr link 01\nus route object-0-E3\n')
    rejects_with('Invalid inventory line 5', groups=fresh, reasons_path=reasons, inventory=inventory)
    # Route names are lowercase like recording names: the fixture route is renamed.
    (routes/'us'/'object-0-E3.tsv').rename(routes/'us'/'object-0-e3.tsv')
    with_inventory('# expected\n\nus link 01\nbr link 01\nus route object-0-e3\n')
    output = strict_generate(root, groups=fresh, reasons_path=reasons, inventory=inventory)
    check(output.count(', as the inventory lists') == 2, 'Inventory match not stated')
    with_inventory('us link 01\nbr link 01\nus link 02\nus route object-0-e3\n')
    rejects_with('Census missing', groups=fresh, reasons_path=reasons, inventory=inventory)
    # The reviewer's case: one of the route censuses removed from a fresh directory.
    with_inventory('us link 01\nbr link 01\nus route object-0-e3\nbr route object-0-e3\n')
    rejects_with('Census missing under', groups=fresh, reasons_path=reasons, inventory=inventory)
    with_inventory('us link 01\nus route object-0-e3\n')
    rejects_with('not in the inventory: br/link-01.tsv', groups=fresh, reasons_path=reasons, inventory=inventory)
    # An extra recording's census lives under extra/.
    (links/'us'/'extra').mkdir()
    census_tree('links', {('us', 'extra/battle.tsv'): both + row(UNIT, UNIT) + row(OPT, UNIT) + row(PROBE, UNIT)})
    with_inventory('us link 01\nbr link 01\nus route object-0-e3\n')
    rejects_with('not in the inventory: us/extra/battle.tsv', groups=fresh, reasons_path=reasons, inventory=inventory)
    with_inventory('us link 01\nbr link 01\nus extra battle\nus route object-0-e3\n')
    strict_generate(root, groups=fresh, reasons_path=reasons, inventory=inventory)
    (links/'us'/'extra'/'battle.tsv').unlink()
    # A group stated to have no census yet may have no inventory line.
    with_inventory('us link 01\nbr link 01\nus route object-0-e3\n')
    rejects_with('states that none exists yet', groups={'playthrough': links, 'routes': 'none'}, reasons_path=reasons,
                 inventory=inventory)
    for bad, fragment in (('us link 1\n', 'Invalid inventory line 1'), ('us chain 01\n', 'Invalid inventory line'),
                          ('jp link 01\n', 'Invalid inventory line'), ('us extra Battle\n', 'Invalid inventory line'),
                          ('us link 01\nus link 01\n', 'Repeated inventory line 2'), ('us link 01', 'LF line endings'),
                          ('us link 01\r\n', 'LF line endings'), ('us extra caf\u00e9\n', 'not ASCII')):
        with_inventory(bad)
        rejects_with(fragment, groups=fresh, reasons_path=reasons, inventory=inventory)
print('Evidence category fixtures passed')

# The class of each part 3 recording's start image, from the class line of its
# .start file: synthetic fixtures, then the committed files.
with tempfile.TemporaryDirectory() as directory:
    recordings = Path(directory)/'recordings'
    for version in ('us', 'br'):
        (recordings/version/'playthrough'/'extra').mkdir(parents=True)
    extra = recordings/'us'/'playthrough'/'extra'
    head = 'psmodern-extra-start 2\nfrom link-08\n'

    def with_start(name, text):
        (extra/name).write_bytes(text.encode('latin-1'))
    with_start('misc.start', head + 'class chain-save\nbasis Unchanged.\n')
    with_start('town-04.start', head + 'class edited\nbasis Moved to a town.\nbyte 0401 40\n')
    check(read_extra_classes(recordings) == {'chain-save': 1, 'edited': 1, 'artificial': 0}, 'Class counts wrong')

    def refuses(text, fragment):
        with_start('town-04.start', text)
        try:
            read_extra_classes(recordings)
        except ValueError as error:
            check(fragment in str(error), f'Wrong refusal: {error}')
        else:
            raise RuntimeError(f'Invalid start file accepted (expected {fragment})')
    for bad, fragment in (('psmodern-extra-start 1\nfrom link-08\nbyte 0401 40\n', 'version 1'),
                          ('psmodern-extra-start 3\nfrom link-08\nclass edited\nbasis M.\nbyte 0401 40\n', 'line 1 must be'),
                          ('psmodern-extra-start 2\nfrom link-8\nclass edited\nbasis M.\nbyte 0401 40\n', "expected 'from link-NN'"),
                          (head + 'basis Moved.\nbyte 0401 40\n', 'the class is required'),
                          (head + 'class likely\nbasis Moved.\nbyte 0401 40\n', "unknown class 'likely'"),
                          (head + 'class chain-save\nbasis Moved.\nbyte 0401 40\n', 'chain-save is exactly'),
                          (head + 'class edited\nbasis Moved.\n', 'chain-save is exactly'),
                          (head + 'class edited\nbyte 0401 40\n', 'the basis is required'),
                          (head + 'class edited\nbasis \nbyte 0401 40\n', 'the basis is empty'),
                          (head + 'class edited\nbasis To row 27 col 21.\nbyte 0401 40\n', 'location data'),
                          (head + 'class edited\nbasis Moved.\nbyte 0401 40', 'final LF'),
                          (head + 'class edited\r\nbasis Moved.\nbyte 0401 40\n', 'printable ASCII'),
                          (head + 'class edited\nbasis Moved caf\u00e9.\nbyte 0401 40\n', 'printable ASCII'),
                          (head + 'class edited\nbasis Moved.\nbyte 0402 40\nbyte 0401 41\n', 'strictly increasing'),
                          (head + 'class edited\nbasis Moved.\nbyte 2000 40\n', 'strictly increasing')):
        refuses(bad, fragment)
counts = read_extra_classes()
check(sum(counts.values()) == len(list(Path(__file__).resolve().parents[1].glob('recordings/*/playthrough/extra/*.start'))),
      'The committed start files were not all counted')
print('Extra start classes passed:', ', '.join(f'{count} {name}' for name, count in counts.items()))
# The C++ reader of the start files applies the same location rule, character
# for character (tests/playthrough_tests.cpp, location_data).
check('R"(' + LOCATION.pattern + ')"' in (Path(__file__).resolve().parent/'playthrough_tests.cpp').read_text(encoding='utf-8'),
      'The location rule of tests/playthrough_tests.cpp differs from that of tools/generate_coverage.py')
