#!/usr/bin/env python3
"""Veremkoltsegvetes az async_tcp taskon futo HTTP kezelokre."""
import sys, pathlib, re

su = pathlib.Path(sys.argv[1])
limit = int(sys.argv[2])
if not su.exists():
    print(f"HIBA: nincs {su} - a -fstack-usage kimenete hianyzik.")
    sys.exit(1)

sajat = re.compile(r'/(webportal|eventlog|configstore|netprobe|secret|strutil|sync)\.cpp:')
sorok = []
for l in su.read_text(encoding='utf-8').split('\n'):
    r = l.split('\t')
    if len(r) < 2 or not sajat.search(r[0]):
        continue
    nev = r[0].split(':')[-1]
    sorok.append((int(r[1]), nev))

if not sorok:
    print("HIBA: egyetlen sajat fuggveny veremadata sem olvashato ki.")
    sys.exit(1)

sorok.sort(reverse=True)
print(f"{'bajt':>7}  fuggveny")
for meret, nev in sorok[:8]:
    print(f"{meret:7d}  {nev[:72]}")

legnagyobb, neve = sorok[0]
print(f"\nKoltsegvetes: {limit} bajt (host x86-64 keret)")
if legnagyobb > limit:
    print(f"MEGBUKOTT: {neve} {legnagyobb} bajtot hasznal.")
    sys.exit(1)
print("OK")
