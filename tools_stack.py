#!/usr/bin/env python3
"""Veremkoltsegvetes az async_tcp taskon futo HTTP kezelokre.

MIERT KELL? A webportal ket nagy kezeloje (handleConfigPost, sendDiagnosticLog)
NEM a loop taskon fut, hanem az AsyncTCP sajat taskjan, aminek a verme veges. A
kod tobb helyen EPP ERRE hivatkozva valaszt kozos puffert (egy "candidate" mind a
negy mezohoz) vagy ket lepest egy helyett (eloszor a naplo fejlece, csak aztan a
bejegyzesek) - de a tenyleges szamot eddig SEMMI nem merte.

A MERES HATARAI, kimondva: ez HOST (x86-64) veremkeret, nem a RISC-V-e. A ket
szam NEM egyenlo, tehat ez nem abszolut garancia. Iranymutatonak viszont pontos:
ha valaki egy 2 KB-os puffert tesz a POST kezelobe, az itt is azonnal latszik.
"""
import sys, pathlib, re

su = pathlib.Path(sys.argv[1])
limit = int(sys.argv[2])
if not su.exists():
    print(f"HIBA: nincs {su} - a -fstack-usage kimenete hianyzik.")
    sys.exit(1)

# Csak a SAJAT fuggvenyeinket nezzuk; a stubok es az STL nem a mi dolgunk.
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
