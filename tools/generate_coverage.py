"""Generate ROM-free routine coverage from the checkpoint censuses."""
import argparse
import csv
from pathlib import Path
import re

CHECKPOINTS = ('opening', 'first-battle', 'first-dungeon', 'later-town', 'boss', 'deep-dungeon')
# Milestone 7's dungeon recordings exist for the US version only.
US_ONLY = ('dungeon-walk-1', 'dungeon-walk-2', 'dungeon-continue')


def checkpoints(version):
    return CHECKPOINTS + (US_ONLY if version == 'us' else ())


RECORDINGS = len(checkpoints('us')) + len(checkpoints('br'))
ALL = f'All {RECORDINGS}'
NUMBER_WORDS = ('zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten',
                'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen',
                'nineteen', 'twenty')


def in_words(number):
    return NUMBER_WORDS[number] if number < len(NUMBER_WORDS) else str(number)
REASONS = ('disabled', 'irq', 'nmi', 'frame', 'ei_delay', 'interrupt_delay', 'halted', 'unmapped_stack')
FORCED_REASONS = tuple(f'forced_{reason}' for reason in REASONS)
COUNTERS = ('cpp', 'fallback', 'forced', 'forced_failed', *REASONS, *FORCED_REASONS)
COLUMNS = ('entry', 'unit', *COUNTERS)
ENTRY = r'[0-1][0-9A-F]:[0-9A-B][0-9A-F]{3}'
# An entry with at least MIN_CALLS calls needs MIN_SHARE of them in C++.
MIN_CALLS = 20
MIN_SHARE = 0.9
# Entries allowed to run without a single C++ call although the recordings
# reach them, each with its written reason. Empty on purpose: a segment the
# recordings reach and never run in C++ is a defect until proven otherwise.
NO_CPP_ALLOWED = {}


def read_census(path):
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if reader.fieldnames != list(COLUMNS):
            raise ValueError(f'Invalid census header: {path}')
        rows = {}
        for row in reader:
            entry = row['entry']
            if not isinstance(entry, str) or not re.fullmatch(ENTRY, entry) or entry in rows:
                raise ValueError(f'Invalid or repeated routine entry: {path}')
            if None in row or not re.fullmatch(ENTRY, row['unit'] or ''):
                raise ValueError(f'Invalid routine unit: {path}')
            if any(not re.fullmatch(r'0|[1-9][0-9]*', row[key] or '') for key in COUNTERS):
                raise ValueError(f'Invalid census counters: {path}')
            counts = {key: int(row[key]) for key in COUNTERS}
            counts['unit'] = row['unit']
            # Reasons can overlap on a call; they are not a partition.
            if any(counts[reason] > counts['fallback'] for reason in REASONS):
                raise ValueError(f'Fallback reason exceeds fallback calls: {path}')
            # A forced body rejected before its core step falls back with no reason, and counts
            # as a forced failure instead.
            if counts['fallback'] and sum(counts[reason] for reason in REASONS) + counts['forced_failed'] < counts['fallback']:
                raise ValueError(f'Fallback call without a reason: {path}')
            # Forced calls are a subset of C++ calls; a forced failure is a fallback call.
            if counts['forced'] > counts['cpp'] or counts['forced_failed'] > counts['fallback']:
                raise ValueError(f'Forced calls exceed their totals: {path}')
            if any(counts[reason] > counts['forced'] for reason in FORCED_REASONS):
                raise ValueError(f'Forced reason exceeds forced calls: {path}')
            rows[entry] = counts
        if not rows:
            raise ValueError(f'Empty checkpoint census: {path}')
        return rows


def share(counts):
    total = counts['cpp'] + counts['fallback']
    return f"{100*counts['cpp']/total:.3f}%" if total else 'n/a'


def table(header, rows):
    columns = header.count('|') - 1
    return [header, '| ' + ' | '.join('---:' if i > 1 else '---' for i in range(columns)) + ' |', *rows]


def check_rules(censuses, totals, reached, allowed, allow_forced):
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


def generate(root, allowed=None, allow_forced=False):
    # allow_forced is for tests only: the real censuses run in original mode.
    allowed = NO_CPP_ALLOWED if allowed is None else allowed
    censuses = {(version, name): read_census(root/version/f'{name}.tsv')
                for version in ('us', 'br') for name in checkpoints(version)}
    entries = sorted(next(iter(censuses.values())))
    if any(sorted(rows) != entries for rows in censuses.values()):
        raise ValueError('Checkpoint censuses contain different routine sets')
    units = {entry: next(iter(censuses.values()))[entry]['unit'] for entry in entries}
    if any(rows[entry]['unit'] != units[entry] for rows in censuses.values() for entry in entries):
        raise ValueError('Checkpoint censuses disagree on a routine unit')
    for entry, reason in allowed.items():
        if entry not in units or not reason.strip():
            raise ValueError(f'Invalid allow-list entry: {entry}')
    totals = {entry: {key: sum(rows[entry][key] for rows in censuses.values()) for key in COUNTERS}
              for entry in entries}
    reached = [entry for entry in entries if totals[entry]['cpp'] + totals[entry]['fallback']]
    check_rules(censuses, totals, reached, allowed, allow_forced)
    lines = ['# Rewritten routine coverage', '',
             f'Generated by `tools/generate_coverage.py` from the {in_words(RECORDINGS)} checkpoint tests',
             f'({in_words(len(CHECKPOINTS))} recordings of each version and {in_words(len(US_ONLY))} US-only dungeon recordings).',
             'Counts describe these recordings; they are not a census of all game code.',
             'The rest of the game still runs on the Z80.', '',
             'A routine is registered as one or more segments, grouped here under the routine',
             '(the unit) they belong to. Each C++ call passes the per-call full-machine comparison.',
             'Each recording also compares routines on/off and the seven original reference components.', '',
             'Fallback reasons can overlap, so their sum can exceed fallback calls. Forced calls',
             'are C++ calls of single-instruction segments admitted despite an interrupt or',
             'frame boundary; they must be 0 in these recordings, which run in original mode.',
             f'A segment with at least {MIN_CALLS} calls needs at least 90% C++ calls, and a segment',
             'the recordings reach needs at least one C++ call' +
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
            for group in (REASONS, FORCED_REASONS):
                if group is FORCED_REASONS and not totals[entry]['forced']:
                    continue
                lines += ['', *table('| ROM | Recording | ' + ' | '.join(group) + ' |',
                    [f'| {version.upper()} | {name} | ' + ' | '.join(str(rows[entry][reason]) for reason in group) + ' |'
                     for (version, name), rows in censuses.items()] +
                    [f'| Total | {ALL} | ' + ' | '.join(str(totals[entry][reason]) for reason in group) + ' |'])]
    if allowed:
        lines += ['', '## Allowed without C++ calls', '',
                  *(f'- {entry}: {reason}' for entry, reason in sorted(allowed.items()))]
    unreached = [entry for entry in entries if entry not in reached]
    if unreached:
        lines += ['', '## Covered by state probes only', '',
                  'No checkpoint recording reaches these segments; the ROM probe tests exercise them.', '',
                  *(f'- {entry} (unit {units[entry]})' for entry in unreached)]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('censuses', type=Path, help='Root containing us/ and br/ checkpoint TSVs')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.censuses)
    args.out.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
