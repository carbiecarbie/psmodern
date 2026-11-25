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
GROUPS = {'playthrough': 'Playthrough group (chain links and part 3 recordings)',
          'routes': 'Prepared-state routes'}
CATEGORY_HEADING = '## Evidence categories'
REACHED_HEADING = '### Reached by the {group} census ({version})'


def read_group(root, entries):
    """The segments each version's censuses under root/us and root/br reach
    (every *.tsv at any depth except the transition censuses). The censuses
    must name the checkpoints' segments and hold no refused call; finding
    none is an error."""
    reached = {'us': set(), 'br': set()}
    count = 0
    for version in ('us', 'br'):
        folder = root/version
        for path in sorted(folder.rglob('*.tsv')) if folder.is_dir() else ():
            if path.name.endswith('.transition.tsv'):
                continue
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
    return reached, count


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
    """The reasons file: a header, then segment, gap (probe-only or us-only) and
    prose separated by tabs; at most one reason per segment and gap, for
    registered segments only. Addresses and prose only, never ROM data."""
    text = Path(path).read_bytes().decode('utf-8')
    lines = text.split('\n')
    if lines[0] != 'segment\tgap\treason' or lines[-1] != '' or '\r' in text:
        raise ValueError(f'Invalid reasons file header or line endings: {path}')
    reasons = {gap: {} for gap in REASON_GAPS}
    for number, line in enumerate(lines[1:-1], 2):
        fields = line.split('\t')
        if len(fields) != 3 or not re.fullmatch(ENTRY, fields[0]) or fields[1] not in REASON_GAPS or not fields[2].strip():
            raise ValueError(f'Invalid reason on line {number}: {path}')
        segment, gap, reason = fields
        if segment not in entries:
            raise ValueError(f'Reason for a segment that is not registered: {segment}')
        if segment in reasons[gap]:
            raise ValueError(f'Two {gap} reasons for {segment}')
        reasons[gap][segment] = reason
    return reasons


def categories(entries, checkpoints, groups, notes, reasons):
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
             f'- Reached by no recording: {len(probe_only):,} ({len(by_route):,} of them by a prepared-state route, '
             f'{len(probe_only - by_route):,} by state probes only).',
             f'- Reached by a US recording and by no Brazilian one: {len(us_only):,}.',
             f'- With a reason in `tools/coverage-reasons.tsv`: {len(probe_only) - len(missing["probe-only"]):,} of the '
             f'{len(probe_only):,} reached by no recording and {len(us_only) - len(missing["us-only"]):,} of the '
             f'{len(us_only):,} reached by US recordings only.']
    lines += [f'- {GROUPS[group]}: {notes[group]}.' for group in GROUPS]
    for group in GROUPS:
        for version in ('us', 'br'):
            members = sorted(groups[group][version])
            lines += ['', REACHED_HEADING.format(group=group, version=version.upper()), '']
            lines += [' '.join(members[i:i + 12]) for i in range(0, len(members), 12)] or ['None.']
    return lines, missing, probe_only


def group_figures(groups, units, entries):
    """The reached sets and the source note of each group (see generate)."""
    reached, notes = {}, {}
    for group in GROUPS:
        source = groups.get(group, 'none')
        if source == 'none':
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
            reached[group], count = read_group(Path(source), entries)
            notes[group] = f'from {count} censuses of this run'
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
             require_reasons=False, report=None):
    """groups (milestone 19) maps 'playthrough' and 'routes' each to a census
    root, to ('keep', previous generated file) or to 'none' (no census yet).
    Without groups the output is the checkpoints-only form of milestone 14.
    report, a dict, receives the segments still lacking a reason."""
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
        group_reached, notes = group_figures(groups, units, entries)
        category_lines, missing, probe_only = categories(
            entries, checkpoint_reached, group_reached, notes, read_reasons(reasons_path or REASONS_FILE, set(entries)))
        if report is not None:
            report['missing'] = missing
        if require_reasons and any(missing.values()):
            raise ValueError('Segments without a reason: ' + ', '.join(
                f'{gap} {segment}' for gap in REASON_GAPS for segment in missing[gap]))
        lines += category_lines
        unreached = sorted(probe_only)
        probe_text = 'No recording (checkpoint or playthrough group) reaches these segments; the ROM probe tests exercise them.'
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
    result = generate(args.censuses, groups=groups, reasons_path=args.reasons, require_reasons=args.require_reasons, report=report)
    if args.missing_reasons:
        sys.stdout.writelines(f'{gap}\t{segment}\n' for gap in REASON_GAPS for segment in report['missing'][gap])
        return
    args.out.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
