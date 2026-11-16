"""The Brazilian name route must format, erase and commit the new slot."""
import csv
from pathlib import Path
import sys

path = Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(77)
with path.open(encoding="utf-8", newline="") as stream:
    rows = {row["entry"]: row for row in csv.DictReader(stream, delimiter="\t")}
for entry in ("01:4166", "00:3F11", "00:3F26"):
    row = rows.get(entry)
    if not row or int(row["cpp"]) != 1 or int(row["fallback"]) or int(row["forced"]):
        raise SystemExit(f"Expected one translated call to {entry} in the Brazilian editor")
