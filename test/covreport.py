#!/usr/bin/env python3
"""Osszesitett sorlefedettseg a sketch-re ES minden modulra.

MIERT KELL SAJAT SCRIPT? Amig a program egyetlen forditasi egyseg volt, eleg
volt a gcov egy sorat kiolvasni. Tobb modul mellett a gcov fajlonkent szamol,
es a "legelso szazalek" mar nem a program lefedettsege - hanem veletlenul
kivalasztott egyetlen fajle. Egy ilyen mereshatar-hiba csendben hazudna: a
kapu vagy feleslegesen bukna, vagy - ami rosszabb - atengedne egy valodi
visszaesest.

Hasznalat:  covreport.py <build-konyvtar> <objektum-elotag> [kuszob]
"""
import re, subprocess, sys, pathlib, glob, os

build = pathlib.Path(sys.argv[1])
prefix = sys.argv[2]                      # pl. build/run-cov
kuszob = float(sys.argv[3]) if len(sys.argv) > 3 else None

# A .gcno fajlok mellol tudjuk, mely forrasokat forditottuk le.
for gcno in sorted(glob.glob(f"{prefix}-*.gcno")):
    subprocess.run(["gcov", "-o", gcno[:-5], gcno[:-5] + ".gcda"],
                   capture_output=True)

# Csak a SAJAT forrasaink erdekelnek: a stubok es a rendszer-headerek nem.
sajat = re.compile(r'^(sketch|secret|strutil|netprobe|sync|eventlog|configstore|webportal)\.(cpp|h)\.gcov$')
osszes_vegrehajtott = osszes_sor = 0
sorok = []
for f in sorted(os.listdir('.')):
    if not sajat.match(f):
        continue
    v = n = 0
    hianyzo = []
    for ln in open(f, encoding='utf-8', errors='replace'):
        cnt = ln.split(':', 1)[0].strip()
        if cnt == '-':
            continue
        n += 1
        if cnt == '#####':
            resz = ln.split(':', 2)
            hianyzo.append((resz[1].strip(), resz[2].rstrip()))
        else:
            v += 1
    if n == 0:
        continue
    osszes_vegrehajtott += v; osszes_sor += n
    sorok.append((f[:-5], v, n, hianyzo))

if osszes_sor == 0:
    print("HIBA: egyetlen lefedettsegi adat sem olvashato ki.")
    sys.exit(1)

print(f"{'fajl':22s} {'lefedett':>10s} {'sor':>7s}   %")
for nev, v, n, _ in sorok:
    print(f"{nev:22s} {v:10d} {n:7d}   {100.0*v/n:6.2f}")
pct = 100.0 * osszes_vegrehajtott / osszes_sor
print(f"{'-'*22} {'-'*10} {'-'*7}   {'-'*6}")
print(f"{'OSSZESEN':22s} {osszes_vegrehajtott:10d} {osszes_sor:7d}   {pct:6.2f}")

if kuszob is None:
    print("\nEgyetlen forgatokonyv altal sem erintett sorok:")
    ures = True
    for nev, _, _, hianyzo in sorok:
        for szam, szoveg in hianyzo:
            if szoveg.strip() == '}':
                continue
            ures = False
            print(f"  {nev}:{szam}: {szoveg.strip()}")
    if ures:
        print("  (nincs ilyen)")
    sys.exit(0)

print(f"\nKuszob: {kuszob:.1f}%")
if pct < kuszob:
    print("MEGBUKOTT: a lefedettseg a kuszob ala esett.")
    sys.exit(1)
print("OK")
