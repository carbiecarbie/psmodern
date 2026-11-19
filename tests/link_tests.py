"""The shipped program must not contain the Z80 core or the test oracle (milestone 14).

Usage: link_tests.py <program> <nm> <link-closure> <archive>...

The program (psmodern.exe) and the game libraries it links are inspected:
 - the link closure of the program target (comma-separated target names,
   computed by CMake) must not name the pinned core (redcode_z80) or the test
   oracle (psmodern_oracle);
 - the program must define no symbol of the core (z80_*) and none of the
   oracle (psmodern::oracle::);
 - the archives of the game libraries must not reference them either.
"""
import re
import subprocess
import sys

CORE = re.compile(r'(^|\s)_?z80_\w+')
ORACLE = re.compile(r'psmodern::oracle::')


def symbols(nm, path, *options):
    done = subprocess.run([nm, '-C', *options, path], capture_output=True, text=True, check=True)
    return done.stdout.splitlines()


def main():
    program, nm, closure, *archives = sys.argv[1:]
    problems = []
    if not archives:
        problems.append('No library of the program was given to scan.')
    defined = symbols(nm, program, '--defined-only')
    # A stripped program would pass vacuously: its own symbols must be listed.
    if not any('psmodern::' in line for line in defined):
        problems.append('The program has no symbol table to inspect (stripped?).')
    for target in closure.split(','):
        if target in ('redcode_z80', 'psmodern_oracle'):
            problems.append(f'The program links {target}.')
    for line in defined:
        if CORE.search(line):
            problems.append(f'The program contains a core symbol: {line.strip()}')
            break
    for line in defined:
        if ORACLE.search(line):
            problems.append(f'The program contains an oracle symbol: {line.strip()}')
            break
    for archive in archives:
        for line in symbols(nm, archive, '--undefined-only'):
            if CORE.search(line) or ORACLE.search(line):
                problems.append(f'{archive} references: {line.strip()}')
                break
    if problems:
        raise SystemExit('\n'.join(problems))
    print('The program contains no core and no oracle.')


main()
