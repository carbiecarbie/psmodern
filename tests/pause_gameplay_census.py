"""Verify that a recorded gameplay pause reaches the translated NMI tail."""
import csv
from pathlib import Path
import sys
p = Path(sys.argv[1])
if not p.exists():
    raise SystemExit(77)
with p.open(encoding="utf-8", newline="") as stream:
    rows = {row["entry"]: row for row in csv.DictReader(stream, delimiter="\t")}
tail = rows.get("00:007A")
if tail is None or int(tail["cpp"]) < 2 or int(tail["forced"]):
    raise SystemExit("Gameplay pause did not reach the NMI toggle twice in C++.")
print("Gameplay pause reached the NMI toggle twice in C++.")
