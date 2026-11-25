"""Generate ROM-free routine coverage from the checkpoint censuses.

Milestone 19 adds the evidence categories: the checkpoints, the playthrough
group (the chain links and part 3's recordings, CTest label playthrough), the
prepared-state routes and probes only. The group's and the routes' censuses are
optional inputs. Without one, the figures recorded in the previous generated
file are kept and the output says so; --no-playthrough-yet and --no-routes-yet
state instead that none exist yet. It never falls back silently. The reasons
file (coverage-reasons.tsv beside this script) gives the reason for every
segment that no recording reaches or that only US recordings reach.
"""
import argparse
import csv
from pathlib import Path
import re
import sys

CHECKPOINTS = ('opening', 'first-battle', 'first-dungeon', 'later-town', 'boss', 'deep-dungeon')
# Milestone 7's dungeon recordings, milestone 11's map recordings (the road, the
# vehicles and the ice) and milestone 12's (the field menu, the shops, the
# hospital and the church) exist for the US version only.
US_ONLY = ('dungeon-walk-1', 'dungeon-walk-2', 'dungeon-continue', 'road', 'vehicles', 'ice',
           'field-menu', 'places', 'ending', 'sound-flute', 'pause-gameplay', 'pause-walk', 'pause-phase-sweep', 'party-defeat')
BR_ONLY = ('save-name',)


def checkpoints(version):
    return CHECKPOINTS + (US_ONLY if version == 'us' else BR_ONLY)


RECORDINGS = len(checkpoints('us')) + len(checkpoints('br'))
ALL = f'All {RECORDINGS}'
NUMBER_WORDS = ('zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten',
                'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen',
                'nineteen', 'twenty')


def in_words(number):
    return NUMBER_WORDS[number] if number < len(NUMBER_WORDS) else str(number)
# The fallback reasons of the census written by the runner and the checkpoint
# tests (routine_fallback_names in src/machine/routines.h). The readers below
# take the reasons from each census header instead, so a reason removed from
# the program (milestone 14 retires the window reasons of the interrupt
# mechanism as segments turn exact) needs no change here: these constants are
# only the fixtures' current set.
REASONS = ('disabled', 'irq', 'nmi', 'frame', 'ei_delay', 'interrupt_delay', 'halted', 'unmapped_stack', 'mapper_write', 'guard')
FORCED_REASONS = tuple(f'forced_{reason}' for reason in REASONS)
BASE_COUNTERS = ('cpp', 'fallback', 'forced', 'forced_failed')
COUNTERS = (*BASE_COUNTERS, *REASONS, *FORCED_REASONS)
COLUMNS = ('entry', 'unit', *COUNTERS)
ENTRY = r'[0-1][0-9A-F]:[0-9A-B][0-9A-F]{3}'
# An entry with at least MIN_CALLS calls needs MIN_SHARE of them in C++.
MIN_CALLS = 20
MIN_SHARE = 0.9
# Entries allowed to run without a single C++ call although the recordings
# reach them, each with its written reason. Empty on purpose: a segment the
# recordings reach and never run in C++ is a defect until proven otherwise.
NO_CPP_ALLOWED = {}


def census_reasons(header):
    """The fallback reasons named by a census header: entry, unit, the four
    counters, the reasons, then the same reasons again with a forced_ prefix."""
    if header is None or tuple(header[:6]) != ('entry', 'unit', *BASE_COUNTERS):
        return None
    rest = list(header[6:])
    half = len(rest)//2
    reasons = tuple(rest[:half])
    if len(rest) % 2 or tuple(rest[half:]) != tuple(f'forced_{reason}' for reason in reasons) or len(set(reasons)) != half:
        return None
    return reasons


def read_census(path):
    """Returns (rows, reasons) of one checkpoint census."""
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        reasons = census_reasons(reader.fieldnames)
        if reasons is None:
            raise ValueError(f'Invalid census header: {path}')
        forced_reasons = tuple(f'forced_{reason}' for reason in reasons)
        counters = (*BASE_COUNTERS, *reasons, *forced_reasons)
        rows = {}
        for row in reader:
            entry = row['entry']
            if not isinstance(entry, str) or not re.fullmatch(ENTRY, entry) or entry in rows:
                raise ValueError(f'Invalid or repeated routine entry: {path}')
            if None in row or not re.fullmatch(ENTRY, row['unit'] or ''):
                raise ValueError(f'Invalid routine unit: {path}')
            if any(not re.fullmatch(r'0|[1-9][0-9]*', row[key] or '') for key in counters):
                raise ValueError(f'Invalid census counters: {path}')
            counts = {key: int(row[key]) for key in counters}
            counts['unit'] = row['unit']
            # Reasons can overlap on a call; they are not a partition.
            if any(counts[reason] > counts['fallback'] for reason in reasons):
                raise ValueError(f'Fallback reason exceeds fallback calls: {path}')
            # A forced body rejected before its core step falls back with no reason, and counts
            # as a forced failure instead.
            if counts['fallback'] and sum(counts[reason] for reason in reasons) + counts['forced_failed'] < counts['fallback']:
                raise ValueError(f'Fallback call without a reason: {path}')
            # Forced calls are a subset of C++ calls; a forced failure is a fallback call.
            if counts['forced'] > counts['cpp'] or counts['forced_failed'] > counts['fallback']:
                raise ValueError(f'Forced calls exceed their totals: {path}')
            if any(counts[reason] > counts['forced'] for reason in forced_reasons):
                raise ValueError(f'Forced reason exceeds forced calls: {path}')
            rows[entry] = counts
        if not rows:
            raise ValueError(f'Empty checkpoint census: {path}')
        return rows, reasons


def read_transition(path):
    """The transition census of milestone 14 (section, key, detail, count rows
    written by replay::write_transition_census) of one recording."""
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.reader(stream, delimiter='\t')
        if next(reader, None) != ['section', 'key', 'detail', 'count']:
            raise ValueError(f'Invalid transition census header: {path}')
        rows = []
        for number, row in enumerate(reader, 2):
            if len(row) != 4 or not re.fullmatch(r'0|[1-9][0-9]*', row[3]):
                raise ValueError(f'Invalid transition census row {number}: {path}')
            rows.append((row[0], row[1], row[2], int(row[3])))
        return rows


def transition_totals(rows):
    """Core finals, rendered finals, native steps and the per-file split of one transition census."""
    origin = {}
    for section, key, detail, _ in rows:
        if section == 'origin':
            origin[key] = detail
    total = {'final': 0, 'rendered': 0, 'native': 0, 'body_irq': 0, 'body_nmi': 0, 'fallback': 0, 'frames': 0}
    files = {}
    for section, key, detail, count in rows:
        if section in ('final', 'rendered'):
            total[section] += count
            files.setdefault(origin.get(key, '-'), {'final': 0, 'rendered': 0})[section] += count
        elif section in ('native', 'fallback'):
            total[section] += count
        elif section == 'summary' and key in ('body_irq', 'body_nmi', 'frames'):
            total[key] += count
    return total, files


def read_transitions(root, censuses):
    """The transition census beside each checkpoint census (<name>.transition.tsv),
    all of them or none: a partial set is an error."""
    paths = {key: root/key[0]/f'{key[1]}.transition.tsv' for key in censuses}
    present = [key for key in paths if paths[key].exists()]
    if not present:
        return None
    if len(present) != len(paths):
        missing = next(path for key, path in paths.items() if key not in present)
        raise ValueError(f'Transition census missing: {missing}')
    return {key: read_transition(path) for key, path in paths.items()}


def transition_lines(transitions):
    totals = [transition_totals(rows) for rows in transitions.values()]
    final = sum(t['final'] for t, _ in totals)
    rendered = sum(t['rendered'] for t, _ in totals)
    native = sum(t['native'] for t, _ in totals)
    lines = ['', '## Transition to rendered code', '',
             'Since milestone 14 the final instruction of a segment and the instructions that join',
             'segments are rendered C++; the Z80 core is only the test oracle, and the program cannot',
             'run it. The census of each recording counts (core finals, native steps and refusals must be zero):', '',
             f'- {final:,} segment finals executed by the core and {rendered:,} rendered by the segment bodies;',
             f'- {native:,} native core steps outside any segment;',
             f'- {sum(t["fallback"] for t, _ in totals):,} calls refused at registered entries;',
             f'- {sum(t["body_irq"] for t, _ in totals):,} interrupt requests and '
             f'{sum(t["body_nmi"] for t, _ in totals):,} non-maskable interrupts accepted inside segment bodies.']
    per_file = {}
    for _, files in totals:
        for name, counts in files.items():
            entry = per_file.setdefault(name, {'final': 0, 'rendered': 0})
            for section in ('final', 'rendered'):
                entry[section] += counts[section]
    if per_file:
        lines += [''] + table('| Source file | Core finals | Rendered finals |',
                              [f"| {name} | {c['final']:,} | {c['rendered']:,} |" for name, c in sorted(per_file.items())])
    return lines


# ---- Milestone 19: evidence categories ----------------------------------------
REASONS_FILE = Path(__file__).resolve().parent/'coverage-reasons.tsv'
REASON_GAPS = ('probe-only', 'us-only')
# The evidence label of a reason: the label of its weakest claim about why the
# segment has that gap (docs/AGENTS.md, "Evidence labels"), strongest first.
EVIDENCE = ('ROM run', 'ROM static', 'Inference', 'Unknown')
# Extracted location data (event numbers with town coordinates or dungeon
# cells) stays in the private study; a reason describes behaviour only.
LOCATION = re.compile(r'\bE\d+:|\brow \d+ col \d+|\bcell [0-9A-F]{2}\b|\(\s*[0-9A-F]{2}\s*,\s*[0-9A-F]{2}\s*\)|\blocation \d|\barea \d')
GROUPS = {'playthrough': 'Playthrough group (chain links and part 3 recordings)',
          'routes': 'Prepared-state routes'}
CATEGORY_HEADING = '## Evidence categories'
REACHED_HEADING = '### Reached by the {group} census ({version})'
# The census file each inventory line names, under its group's census root.
INVENTORY_KINDS = {'link': 'playthrough', 'extra': 'playthrough', 'route': 'routes'}


def read_inventory(path):
    """The committed inventory of expected recordings and routes: ASCII, LF,
    lines `<us|br> link <NN>`, `<us|br> extra <name>` or `<us|br> route <name>`,
    `#` comments and blank lines ignored. Returns, per group, the census paths
    (relative to the group's census root) that a complete run writes."""
    data = Path(path).read_bytes()
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        raise ValueError(f'Inventory is not ASCII: {path}') from None
    if '\r' in text or (text and not text.endswith('\n')):
        raise ValueError(f'Inventory needs LF line endings, including the last line: {path}')
    expected = {group: set() for group in GROUPS}
    for number, line in enumerate(text.split('\n')[:-1], 1):
        if not line.strip() or line.startswith('#'):
            continue
        fields = line.split(' ')
        if (len(fields) != 3 or fields[0] not in ('us', 'br') or fields[1] not in INVENTORY_KINDS or
                not re.fullmatch(r'[0-9]{2}' if fields[1] == 'link' else r'[a-z0-9]+(-[a-z0-9]+)*', fields[2])):
            raise ValueError(f'Invalid inventory line {number}: {path}')
        version, kind, name = fields
        relative = f'{version}/link-{name}.tsv' if kind == 'link' else \
            f'{version}/extra/{name}.tsv' if kind == 'extra' else f'{version}/{name}.tsv'
        if relative in expected[INVENTORY_KINDS[kind]]:
            raise ValueError(f'Repeated inventory line {number}: {path}')
        expected[INVENTORY_KINDS[kind]].add(relative)
    return expected


def read_group(root, entries, expected=None):
    """The segments each version's censuses under root/us and root/br reach
    (every *.tsv at any depth except the transition censuses). The censuses
    must name the checkpoints' segments and hold no refused call; finding
    none is an error. With expected (the inventory's census paths for this
    group), a census missing from the directory or not in the inventory is an
    error too."""
    reached = {'us': set(), 'br': set()}
    count = 0
    found = set()
    for version in ('us', 'br'):
        folder = root/version
        for path in sorted(folder.rglob('*.tsv')) if folder.is_dir() else ():
            if path.name.endswith('.transition.tsv'):
                continue
            found.add(path.relative_to(root).as_posix())
            rows, _ = read_census(path)
            if sorted(rows) != entries:
                raise ValueError(f'Census with another routine set: {path}')
            for entry, counts in rows.items():
                if counts['fallback']:
                    raise ValueError(f'Refused calls in {path}: {entry}')
                if counts['cpp']:
                    reached[version].add(entry)
            count += 1
    if not count:
        raise ValueError(f'No census found under {root}')
    if expected is not None:
        missing, extra = sorted(expected - found), sorted(found - expected)
        if missing:
            raise ValueError(f'Census missing under {root} for an inventory line: ' + ', '.join(missing))
        if extra:
            raise ValueError(f'Census under {root} not in the inventory: ' + ', '.join(extra))
    return reached, count


EXTRA_CLASSES_FILE = Path(__file__).resolve().parent/'extra-start-classes.tsv'
RECORDINGS_DIR = Path(__file__).resolve().parents[1]/'recordings'
# The start image of each part 3 recording (docs/development.md, "Part 3's recordings").
EXTRA_CLASSES = {'chain-save': 'unchanged chain saves',
                 'edited': 'chain saves edited to a state whose reachability by play was not verified',
                 'artificial': 'states known to be artificial'}


def read_extra_classes(path=EXTRA_CLASSES_FILE, recordings=RECORDINGS_DIR):
    """The class of each part 3 recording's start image: a header, then region,
    name, class and a one-line basis separated by tabs, one line per
    recordings/<us|br>/playthrough/extra/<name>.start and no other. A
    chain-save is exactly a .start with no byte line. Returns the count per class."""
    text = Path(path).read_bytes().decode('ascii')
    lines = text.split('\n')
    if lines[0] != 'region\tname\tclass\tbasis' or lines[-1] != '' or '\r' in text:
        raise ValueError(f'Invalid extra classes header or line endings: {path}')
    starts = {(version, start.stem): start for version in ('us', 'br')
              for start in sorted((Path(recordings)/version/'playthrough'/'extra').glob('*.start'))}
    seen = {}
    for number, line in enumerate(lines[1:-1], 2):
        fields = line.split('\t')
        if len(fields) != 4 or fields[0] not in ('us', 'br') or fields[2] not in EXTRA_CLASSES or not fields[3].strip():
            raise ValueError(f'Invalid extra class on line {number}: {path}')
        key = (fields[0], fields[1])
        if key in seen:
            raise ValueError(f'Two classes for {fields[0]} {fields[1]}')
        if key not in starts:
            raise ValueError(f'Class for an extra recording that has no .start file: {fields[0]} {fields[1]}')
        if LOCATION.search(fields[3]):
            raise ValueError(f'Location data in the basis on line {number}: keep it in the private study')
        edits = [entry for entry in starts[key].read_text(encoding='ascii').split('\n') if entry.startswith('byte ')]
        if (fields[2] == 'chain-save') != (not edits):
            raise ValueError(f'{fields[0]} {fields[1]}: chain-save is exactly a start with no edited byte')
        seen[key] = fields[2]
    missing = sorted(set(starts) - set(seen))
    if missing:
        raise ValueError('Extra recordings without a class: ' + ', '.join(f'{v} {n}' for v, n in missing))
    return {name: sum(1 for value in seen.values() if value == name) for name in EXTRA_CLASSES}


def previous_group(previous, group):
    """The reached sets a previous generated file recorded for a group, or None
    when it records none (an older file, or no such file)."""
    if previous is None or not Path(previous).exists():
        return None
    text = Path(previous).read_text(encoding='utf-8')
    if CATEGORY_HEADING not in text:
        return None
    reached = {}
    for version in ('us', 'br'):
        heading = REACHED_HEADING.format(group=group, version=version.upper()) + '\n'
        if heading not in text:
            return None
        block = text.split(heading, 1)[1].split('\n#', 1)[0]
        reached[version] = set(re.findall(ENTRY, block))
    return reached


def read_reasons(path, entries):
    """The reasons file: a header, then segment, gap (probe-only or us-only),
    evidence label and prose separated by tabs; at most one reason per segment
    and gap, for registered segments only. Addresses and prose only, never ROM
    data or extracted location data. Returns {gap: {segment: (evidence, reason)}}."""
    text = Path(path).read_bytes().decode('utf-8')
    lines = text.split('\n')
    if lines[0] != 'segment\tgap\tevidence\treason' or lines[-1] != '' or '\r' in text:
        raise ValueError(f'Invalid reasons file header or line endings: {path}')
    reasons = {gap: {} for gap in REASON_GAPS}
    for number, line in enumerate(lines[1:-1], 2):
        fields = line.split('\t')
        if len(fields) != 4 or not re.fullmatch(ENTRY, fields[0]) or fields[1] not in REASON_GAPS or not fields[3].strip():
            raise ValueError(f'Invalid reason on line {number}: {path}')
        segment, gap, evidence, reason = fields
        if evidence not in EVIDENCE:
            raise ValueError(f'Invalid evidence label on line {number}: {evidence!r} (one of {", ".join(EVIDENCE)})')
        if LOCATION.search(reason):
            raise ValueError(f'Location data in the reason on line {number}: keep it in the private study')
        if segment not in entries:
            raise ValueError(f'Reason for a segment that is not registered: {segment}')
        if segment in reasons[gap]:
            raise ValueError(f'Two {gap} reasons for {segment}')
        reasons[gap][segment] = (evidence, reason)
    return reasons


def categories(entries, checkpoints, groups, notes, reasons, extra_counts=None):
    """The evidence section, the segments still lacking a reason per gap, and
    the segments no recording reaches."""
    def either(reached):
        return reached['us'] | reached['br']
    recordings = {version: checkpoints[version] | groups['playthrough'][version] for version in ('us', 'br')}
    probe_only = set(entries) - either(recordings)
    by_route = either(groups['routes']) & probe_only
    us_only = recordings['us'] - recordings['br']
    need = {'probe-only': probe_only, 'us-only': us_only}
    stale = [f'{gap} {segment}' for gap in REASON_GAPS for segment in sorted(reasons[gap]) if segment not in need[gap]]
    if stale:
        raise ValueError('Stale reasons (the segment no longer has that gap): ' + ', '.join(stale))
    missing = {gap: sorted(need[gap] - set(reasons[gap])) for gap in REASON_GAPS}

    def row(name, reached):
        return f"| {name} | {len(reached['us']):,} | {len(reached['br']):,} | {len(either(reached)):,} |"
    lines = ['', CATEGORY_HEADING, '',
             f'Segments reached, of {len(entries):,} registered. Recordings are the checkpoints and the playthrough',
             'group; prepared-state routes and state probes are not recordings.', '',
             *table('| Evidence | US | Brazilian | Either |', [
                 row('Checkpoint recordings', checkpoints),
                 row(GROUPS['playthrough'], groups['playthrough']),
                 row('Any recording', recordings),
                 row(GROUPS['routes'], groups['routes'])]), '',
             f'- Reached by no recording: {len(probe_only):,} ({len(by_route):,} of them dispatched only by a '
             f'prepared-state route, {len(probe_only - by_route):,} by neither a recording nor a route).',
             f'- Reached by a US recording and by no Brazilian one: {len(us_only):,}.',
             f'- With a reason in `tools/coverage-reasons.tsv`: {len(probe_only) - len(missing["probe-only"]):,} of the '
             f'{len(probe_only):,} reached by no recording and {len(us_only) - len(missing["us-only"]):,} of the '
             f'{len(us_only):,} reached by US recordings only.']
    lines += [f'- {GROUPS[group]}: {notes[group]}.' for group in GROUPS]
    if extra_counts is not None:
        lines += ["- Start images of part 3's recordings (`tools/extra-start-classes.tsv`): " +
                  '; '.join(f'{extra_counts[name]} {text}' for name, text in EXTRA_CLASSES.items()) + '.']

    def label_counts(segments, gap):
        return [sum(1 for segment in segments if reasons[gap].get(segment, ('',))[0] == label) for label in EVIDENCE]
    route_counts = label_counts(by_route, 'probe-only')
    probe_counts = label_counts(probe_only - by_route, 'probe-only')
    us_counts = label_counts(us_only, 'us-only')
    lines += ['', 'The reasons by the evidence of their weakest claim (docs/AGENTS.md, "Evidence labels"). A reason',
              'existing is not itself evidence of a rare trigger or of unreachability: an Unknown row says that its',
              'trigger was not established.', '',
              *table('| Evidence | Route only | Neither recording nor route | US recordings only | Total |', [
                  f'| {label} | {r:,} | {p:,} | {u:,} | {r + p + u:,} |'
                  for label, r, p, u in zip(EVIDENCE, route_counts, probe_counts, us_counts)] +
                  [f'| No reason yet | {len(by_route) - sum(route_counts):,} | {len(probe_only - by_route) - sum(probe_counts):,} | '
                   f'{len(us_only) - sum(us_counts):,} | '
                   f'{len(probe_only) + len(us_only) - sum(route_counts) - sum(probe_counts) - sum(us_counts):,} |'])]
    for group in GROUPS:
        for version in ('us', 'br'):
            members = sorted(groups[group][version])
            lines += ['', REACHED_HEADING.format(group=group, version=version.upper()), '']
            lines += [' '.join(members[i:i + 12]) for i in range(0, len(members), 12)] or ['None.']
    return lines, missing, probe_only, by_route


def group_figures(groups, units, entries, inventory=None):
    """The reached sets and the source note of each group (see generate). With
    an inventory (read_inventory), a fresh census directory must hold exactly
    the censuses it lists, and a group stated to have no census yet must have
    no inventory line."""
    reached, notes = {}, {}
    for group in GROUPS:
        source = groups.get(group, 'none')
        expected = None if inventory is None else inventory[group]
        if source == 'none':
            if expected:
                raise ValueError(f'The inventory lists {group} censuses, but the run states that none exists yet')
            reached[group] = {'us': set(), 'br': set()}
            notes[group] = 'no census recorded yet'
        elif isinstance(source, tuple) and source[0] == 'keep':
            kept = previous_group(source[1], group)
            if kept is None:
                raise ValueError(f'No {group} figures to keep in {source[1]}: give its census, or state that none exists yet')
            if any(entry not in units for version in kept for entry in kept[version]):
                raise ValueError(f'The kept {group} figures name a segment that is not registered')
            reached[group] = kept
            notes[group] = (f'figures kept from the previous generated file ({Path(source[1]).name}); '
                            'this run was not given its census')
        else:
            reached[group], count = read_group(Path(source), entries, expected)
            notes[group] = f'from {count} censuses of this run' + (', as the inventory lists' if expected is not None else '')
    return reached, notes


def share(counts):
    total = counts['cpp'] + counts['fallback']
    return f"{100*counts['cpp']/total:.3f}%" if total else 'n/a'


def table(header, rows):
    columns = header.count('|') - 1
    return [header, '| ' + ' | '.join('---:' if i > 1 else '---' for i in range(columns)) + ' |', *rows]


def check_rules(censuses, totals, reached, allowed, allow_forced, allow_refusals):
    # Milestone 14 (review R3 L5): the program has no core, so a refused call stopped
    # the game; the recordings must make none, and forced calls no longer exist.
    for entry in reached:
        if not allow_refusals and totals[entry]['fallback']:
            raise ValueError(f'Refused calls in the recordings: {entry}')
    for entry in allowed:
        if not totals[entry]['cpp'] + totals[entry]['fallback']:
            raise ValueError(f'Allow-list entry is never called: {entry}')
    for entry in reached:
        if not allow_forced and (totals[entry]['forced'] or totals[entry]['forced_failed']):
            raise ValueError(f'Forced calls in original mode: {entry}')
        calls = totals[entry]['cpp'] + totals[entry]['fallback']
        if calls >= MIN_CALLS and totals[entry]['cpp'] < MIN_SHARE*calls:
            raise ValueError(f'C++ coverage below 90%: {entry}')
        # The checkpoint tests apply the same gate to each recording.
        for (version, name), rows in censuses.items():
            counts = rows[entry]
            total = counts['cpp'] + counts['fallback']
            if total >= MIN_CALLS and counts['cpp'] < MIN_SHARE*total:
                raise ValueError(f'C++ coverage below 90%: {version}/{name}/{entry}')
        if not totals[entry]['cpp'] and entry not in allowed:
            raise ValueError(f'Reached entry without a C++ call: {entry}')
        if totals[entry]['cpp'] and entry in allowed:
            raise ValueError(f'Allow-list entry has C++ calls: {entry}')


def generate(root, allowed=None, allow_forced=False, allow_refusals=False, groups=None, reasons_path=None,
             require_reasons=False, report=None, inventory=None, extra_counts=None):
    """groups (milestone 19) maps 'playthrough' and 'routes' each to a census
    root, to ('keep', previous generated file) or to 'none' (no census yet).
    Without groups the output is the checkpoints-only form of milestone 14.
    report, a dict, receives the segments still lacking a reason. inventory
    (a path) is the committed list of expected recordings and routes that a
    fresh group census must match exactly. extra_counts (read_extra_classes)
    adds the classes of the part 3 recordings' start images."""
    # allow_forced and allow_refusals are for the tests of the older rules only:
    # the real censuses run in original mode with no refusal.
    allowed = NO_CPP_ALLOWED if allowed is None else allowed
    read = {(version, name): read_census(root/version/f'{name}.tsv')
            for version in ('us', 'br') for name in checkpoints(version)}
    censuses = {key: rows for key, (rows, _) in read.items()}
    reason_sets = {reasons for _, reasons in read.values()}
    if len(reason_sets) != 1:
        raise ValueError('Checkpoint censuses have different fallback reason columns')
    reasons = next(iter(reason_sets))
    forced_reasons = tuple(f'forced_{reason}' for reason in reasons)
    counters = (*BASE_COUNTERS, *reasons, *forced_reasons)
    entries = sorted(next(iter(censuses.values())))
    if any(sorted(rows) != entries for rows in censuses.values()):
        raise ValueError('Checkpoint censuses contain different routine sets')
    units = {entry: next(iter(censuses.values()))[entry]['unit'] for entry in entries}
    if any(rows[entry]['unit'] != units[entry] for rows in censuses.values() for entry in entries):
        raise ValueError('Checkpoint censuses disagree on a routine unit')
    for entry, reason in allowed.items():
        if entry not in units or not reason.strip():
            raise ValueError(f'Invalid allow-list entry: {entry}')
    totals = {entry: {key: sum(rows[entry][key] for rows in censuses.values()) for key in counters}
              for entry in entries}
    reached = [entry for entry in entries if totals[entry]['cpp'] + totals[entry]['fallback']]
    check_rules(censuses, totals, reached, allowed, allow_forced, allow_refusals)
    transitions = read_transitions(root, censuses)
    cpp_calls = sum(row['cpp'] for row in totals.values())
    fallback_calls = sum(row['fallback'] for row in totals.values())
    lines = ['# Rewritten routine coverage', '',
             f'Generated by `tools/generate_coverage.py` from the {in_words(RECORDINGS)} checkpoint tests',
             f'({in_words(len(checkpoints("us")))} US recordings and {in_words(len(checkpoints("br")))} Brazilian recordings).',
             f'The recordings reach {len(reached):,} of {len(entries):,} registered segments.',
             f'They make {cpp_calls:,} C++ calls; {fallback_calls:,} calls were refused at registered entries.',
             'Since milestone 14 the program has no Z80 core: every instruction is rendered C++, and a',
             'refused call or an address where no segment begins stops the game with a message.', '',
             'A routine is registered as one or more segments, grouped here under the routine',
             '(the unit) they belong to. Each recording runs under the lockstep with the Z80 oracle',
             '(every instruction boundary, and the whole machine at every frame) and is compared with',
             'the seven components of the original reference.', '',
             'Refusal ("fallback") reasons can overlap, so their sum can exceed the refused calls.',
             'Forced calls no longer exist (milestone 14); their columns must stay 0.',
             'Every call the recordings make must run in C++ (no refused call: a refusal stops the game),',
             'so every segment the recordings reach has at least one C++ call' +
             (' unless it is listed under "Allowed without C++ calls".' if allowed else '.')]
    for unit in sorted({units[entry] for entry in reached}):
        members = [entry for entry in reached if units[entry] == unit]
        lines += ['', f'## {unit}']
        for entry in members:
            if members != [unit]:
                lines += ['', f'### Segment {entry}']
            lines += ['', *table('| ROM | Recording | C++ | Fallback | Forced | Forced failed | C++ share |',
                [f"| {version.upper()} | {name} | {c['cpp']:,} | {c['fallback']:,} | {c['forced']:,} | {c['forced_failed']:,} | {share(c)} |"
                 for (version, name), rows in censuses.items() for c in (rows[entry],)] +
                [f"| Total | {ALL} | {totals[entry]['cpp']:,} | {totals[entry]['fallback']:,} | "
                 f"{totals[entry]['forced']:,} | {totals[entry]['forced_failed']:,} | {share(totals[entry])} |"])]
            for group in (reasons, forced_reasons):
                if group is forced_reasons and not totals[entry]['forced']:
                    continue
                lines += ['', *table('| ROM | Recording | ' + ' | '.join(group) + ' |',
                    [f'| {version.upper()} | {name} | ' + ' | '.join(str(rows[entry][reason]) for reason in group) + ' |'
                     for (version, name), rows in censuses.items()] +
                    [f'| Total | {ALL} | ' + ' | '.join(str(totals[entry][reason]) for reason in group) + ' |'])]
    if allowed:
        lines += ['', '## Allowed without C++ calls', '',
                  *(f'- {entry}: {reason}' for entry, reason in sorted(allowed.items()))]
    if transitions:
        lines += transition_lines(transitions)
    unreached = [entry for entry in entries if entry not in reached]
    probe_text = 'No checkpoint recording reaches these segments; the ROM probe tests exercise them.'
    if groups is not None:
        checkpoint_reached = {version: {entry for entry in entries
                                        if any(rows[entry]['cpp'] for (v, _), rows in censuses.items() if v == version)}
                              for version in ('us', 'br')}
        group_reached, notes = group_figures(groups, units, entries,
                                             None if inventory is None else read_inventory(inventory))
        reasons = read_reasons(reasons_path or REASONS_FILE, set(entries))
        category_lines, missing, probe_only, by_route = categories(
            entries, checkpoint_reached, group_reached, notes, reasons, extra_counts)
        if report is not None:
            report['missing'] = missing
        if require_reasons and any(missing.values()):
            raise ValueError('Segments without a reason: ' + ', '.join(
                f'{gap} {segment}' for gap in REASON_GAPS for segment in missing[gap]))
        lines += category_lines

        def listed(entry):
            evidence = reasons['probe-only'].get(entry, ('no reason yet',))[0]
            return f'- {entry} (unit {units[entry]}; reason: {evidence})'
        if by_route:
            lines += ['', '## Reached by prepared-state routes only', '',
                      'No recording (checkpoint or playthrough group) reaches these segments; a prepared-state route',
                      'dispatches them at their own entry, and the ROM probe tests exercise them.', '',
                      *(listed(entry) for entry in sorted(by_route))]
        unreached = sorted(probe_only - by_route)
        probe_text = ['Neither a recording (checkpoint or playthrough group) nor a prepared-state route reaches these',
                      'segments at their own entry; the ROM probe tests exercise them. A reason labelled ROM run records',
                      "that a recording executes the address inside another segment's body."]
        if unreached:
            lines += ['', '## Covered by state probes only', '', *probe_text, '', *(listed(entry) for entry in unreached)]
        return '\n'.join(lines) + '\n'
    if unreached:
        lines += ['', '## Covered by state probes only', '', probe_text, '',
                  *(f'- {entry} (unit {units[entry]})' for entry in unreached)]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('censuses', type=Path, help='Root containing us/ and br/ checkpoint TSVs')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--previous', type=Path, help='generated file whose group figures are kept (default: --out)')
    for group, what in (('playthrough', 'the playthrough group'), ('routes', 'the prepared-state routes')):
        choice = parser.add_mutually_exclusive_group()
        choice.add_argument(f'--{group}', type=Path, metavar='DIR', help=f'census root (us/, br/) of {what}')
        choice.add_argument(f'--no-{group}-yet', action='store_true', help=f'state that {what} has no census yet')
    parser.add_argument('--reasons', type=Path, help=f'reasons file (default: {REASONS_FILE.name} beside this script)')
    parser.add_argument('--require-reasons', action='store_true',
                        help='fail when a segment no recording reaches, or only US recordings reach, has no reason')
    parser.add_argument('--missing-reasons', action='store_true',
                        help='print the segments still lacking a reason (gap, tab, segment) instead of writing --out')
    parser.add_argument('--inventory', type=Path,
                        help='committed inventory of expected recordings and routes; a fresh --playthrough or --routes '
                             'directory must hold exactly the censuses it lists')
    args = parser.parse_args()
    groups = {}
    for group in GROUPS:
        if getattr(args, group):
            groups[group] = getattr(args, group)
        elif getattr(args, f'no_{group}_yet'):
            groups[group] = 'none'
        else:
            groups[group] = ('keep', args.previous or args.out)
            print(f'{group}: no census given, keeping the figures recorded in {args.previous or args.out}', file=sys.stderr)
    report = {}
    result = generate(args.censuses, groups=groups, reasons_path=args.reasons, require_reasons=args.require_reasons,
                      report=report, inventory=args.inventory, extra_counts=read_extra_classes())
    if args.missing_reasons:
        sys.stdout.writelines(f'{gap}\t{segment}\n' for gap in REASON_GAPS for segment in report['missing'][gap])
        return
    args.out.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
