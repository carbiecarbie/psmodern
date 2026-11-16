"""Require the field pause route to execute both the pause loop and mute."""
import csv
from pathlib import Path
import sys
p=Path(sys.argv[1])
if not p.exists():
    raise SystemExit(77)
with p.open(encoding="utf-8",newline="") as stream:
    rows={row["entry"]:row for row in csv.DictReader(stream,delimiter="\t")}
expected=int(sys.argv[2]) if len(sys.argv)>2 else 2
required=[("00:0067",expected),("00:007A",expected)]
if expected==2:
    required += [("00:00F4",1),("00:02F5",1)]
for address,minimum in required:
    row=rows.get(address)
    if row is None or int(row["cpp"])<minimum or int(row["forced"]) or (address in ("00:0067","00:007A") and int(row["cpp"])!=expected):
        raise SystemExit(f"Field pause did not reach {address} in C++.")
print("Pause route reached the translated NMI continuation and tail in C++.")
