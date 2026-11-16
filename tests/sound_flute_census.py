"""Assert the rare flute route reaches its rewritten sound command."""
import csv
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
if not path.exists():
    raise SystemExit(77)
with path.open(encoding="utf-8", newline="") as stream:
    rows = {row["entry"]: row for row in csv.DictReader(stream, delimiter="\t")}
row = rows.get("0C:8215")
if row is None or int(row["cpp"]) < 1 or int(row["fallback"]) or int(row["forced"]):
    raise SystemExit("The flute route did not execute the shared-voice stop in C++.")
print("The flute route reached the shared-voice stop in C++.")
