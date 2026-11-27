"""Check that every ROM test skipped in a CTest run with no ROM configured.

CI registers the ROM tests (PSMODERN_ROM_TESTS=ON) without giving them a ROM
and runs CTest with --output-junit. Every test labelled rom must then have
skipped through its own exit code 77 (SKIP_RETURN_CODE). Passing, failing, or
not running for another reason (a failed fixture, a missing file) means its
registration or its argument parsing is wrong: for example an empty ROM path
dropped from the command line, which moves every later argument up one place.
"""
import argparse
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def check(junit):
    """Return the number of ROM tests and one line per test that did not skip."""
    count = 0
    problems = []
    for case in ET.parse(junit).getroot().iter('testcase'):
        labels = set()
        for prop in case.iter('property'):
            if prop.get('name') == 'cmake_labels':
                labels.update(label for label in prop.get('value', '').split(';') if label)
        if 'rom' not in labels:
            continue
        count += 1
        skipped = case.find('skipped')
        message = skipped.get('message', '') if skipped is not None else ''
        if case.get('status') != 'notrun' or not message.startswith('SKIP_RETURN_CODE='):
            detail = f'status {case.get("status")}' + (f', {message}' if message else '')
            problems.append(f'{case.get("name")}: {detail}')
    return count, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('junit', type=Path, help='JUnit file written by ctest --output-junit')
    args = parser.parse_args()
    count, problems = check(args.junit)
    if count == 0:
        print('No test labelled rom: configure with PSMODERN_ROM_TESTS=ON.', file=sys.stderr)
        return 1
    if problems:
        print(f'{len(problems)} of {count} ROM tests did not skip without a ROM:', file=sys.stderr)
        for problem in problems:
            print(f'  {problem}', file=sys.stderr)
        return 1
    print(f'All {count} ROM tests skipped without a ROM.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
