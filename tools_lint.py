#!/usr/bin/env python3
"""Forras-ellenorzes: blokkolo ciklusok etetese, dinamikus foglalas, atfordulas-biztos idok."""
import re, sys, pathlib

GYOKER = pathlib.Path(__file__).resolve().parent

FORRASOK = ['bazsi_router_reboot.ino', 'netprobe.cpp', 'eventlog.cpp',
            'configstore.cpp', 'webportal.cpp', 'sync.cpp', 'secret.cpp',
            'strutil.cpp']

ETETO_SEGEDEK = ['feedWatchdog', 'blockingDelay', 'waitWithButtons',
                 'waitWithButtonsUntilOnline', 'onlineProbeDue',
                 'lockConfigBeforeShutdown']

ETETO_SEGEDEK += ['PROBE_POLL_MS']

def fejlec_vege(szoveg, kezd):
    """A ciklus fejlecenek zaro zarojele utani pozicio (do eseten maga a kezdet)."""
    i = szoveg.find('(', kezd)
    if i < 0:
        return kezd
    d = 0
    for j in range(i, len(szoveg)):
        if szoveg[j] == '(': d += 1
        elif szoveg[j] == ')':
            d -= 1
            if d == 0:
                return j + 1
    return kezd

def ciklus_torzsek(szoveg):
    """(kezdo sorszam, torzs) parok minden while/for/do ciklusra."""
    ki = []
    for m in re.finditer(r'\b(while|for|do)\s*[\(\{]', szoveg):
        utan = m.start() if m.group(1) == 'do' else fejlec_vege(szoveg, m.start())
        i = szoveg.find('{', utan)
        pv = szoveg.find(';', utan)
        if i < 0:
            continue
        if 0 <= pv < i:
            continue
        d = 0
        for j in range(i, len(szoveg)):
            if szoveg[j] == '{': d += 1
            elif szoveg[j] == '}':
                d -= 1
                if d == 0:
                    ki.append((szoveg.count('\n', 0, m.start()) + 1, szoveg[i:j+1]))
                    break
    return ki

hibak = []
for f in FORRASOK:
    p = GYOKER / f
    if not p.exists():
        print(f"tools_lint: HIANYZO FORRAS: {p}")
        sys.exit(2)
    nyers = p.read_text(encoding='utf-8')
    eredeti_sorok = nyers.split('\n')
    szoveg = re.sub(r'//[^\n]*', '', nyers)
    for sor, torzs in ciklus_torzsek(szoveg):
        if not re.search(r'\bdelay\s*\(', torzs):
            continue
        if any(s in torzs for s in ETETO_SEGEDEK):
            continue
        if re.search(r'\breturn\b', torzs):
            continue
        elozo = eredeti_sorok[max(0, sor - 7):sor]
        if any('WDT-OK:' in l for l in elozo):
            continue
        hibak.append(f"{f}:{sor}: blokkolo ciklus etetes nelkul es WDT-OK jeloles nelkul")

if hibak:
    print("A blokkolo ciklusok ellenorzese MEGBUKOTT:")
    for h in hibak:
        print("  " + h)
    print("\nMinden delay()-t tartalmazo ciklusnak etetnie kell a watchdogot:")
    print("  feedWatchdog(), vagy egy eteto seged (" + ", ".join(ETETO_SEGEDEK[:4]) + ", ...),")
    print("  vagy a ciklus minden korben terjen vissza a loop()-ba,")
    print("  vagy - ha bizonyithatoan rovid es korlatos - kapjon a ciklus fole")
    print("  egy \"// WDT-OK: <indok>\" sort, ami kimondja, MIERT nem kell etetnie.")
    sys.exit(1)

print("1. Blokkolo ciklusok: rendben, mind etet.")

TILTOTT = [
    (r'\b(malloc|calloc|realloc|strdup|strndup)\s*\(', 'C heap-foglalas'),
    (r'\bnew\s+[A-Za-z_]',                            'operator new'),
    (r'\bstd::(vector|string|map|set|list|deque)\b',   'dinamikusan foglalo STL tarolo'),
    (r'^\s*String\s+\w+\s*[=;]',                     'ertek szerinti String (masolat = foglalas)'),
]

alloc_hibak = []
for f in FORRASOK:
    p2 = GYOKER / f
    if not p2.exists():
        print(f"tools_lint: HIANYZO FORRAS: {p2}")
        sys.exit(2)
    for n, sor in enumerate(p2.read_text(encoding='utf-8').split('\n'), 1):
        mag = re.sub(r'//.*', '', sor)
        if 'LINT-OK:' in sor:
            continue
        for minta, mit in TILTOTT:
            if re.search(minta, mag):
                alloc_hibak.append(f"{f}:{n}: {mit} -> {sor.strip()[:70]}")

if alloc_hibak:
    print("\nA dinamikus foglalas ellenorzese MEGBUKOTT:")
    for h in alloc_hibak:
        print("  " + h)
    print("\nA program fix meretu pufferekkel dolgozik - a meret egyben validacio is.")
    print("Ha egy eset MEGIS indokolt, irj a sor vegere \"// LINT-OK: <indok>\".")
    sys.exit(1)

print("2. Dinamikus foglalas: rendben, egy sincs.")

IDO_KEZI = {'millis', 'now', 'currentMillis', 'apDeadline', 'restartAt',
            'lastProbe', 'startMillis', 'stateStart', 'resetPulseStart',
            'fatalStart', 'blinkLast', 'heapCheckLast', 'heapLogLast',
            'resetDelayProbeLast', 'firstStartProbeLast', 'btnResetDownAt',
            'btnWifiResetDownAt', 'resetBtnDownSince', 'wifiResetBtnDownSince'}

ido_nevek = set(IDO_KEZI)
for f in FORRASOK:
    p3 = GYOKER / f
    if not p3.exists():
        print(f"tools_lint: HIANYZO FORRAS: {p3}")
        sys.exit(2)
    szoveg3 = p3.read_text(encoding='utf-8')
    for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*millis\s*\(\s*\)', szoveg3):
        ido_nevek.add(m.group(1))

IDO_MINTA = re.compile(r'\b(' + '|'.join(sorted(ido_nevek)) + r')\b')

def ido_erteku(kif):
    """Ido-erteku-e a reszkifejezes (a kivonas mar idotartam, nem idopont)?"""
    mag = kif.strip()
    if not mag:
        return False
    if re.search(r'[A-Za-z0-9_\)\]]\s*-\s*[A-Za-z_(]', mag):
        return False
    return bool(IDO_MINTA.search(mag))

ido_hibak = []
for f in FORRASOK:
    p3 = GYOKER / f
    if not p3.exists():
        print(f"tools_lint: HIANYZO FORRAS: {p3}")
        sys.exit(2)
    for n, sor in enumerate(p3.read_text(encoding='utf-8').split('\n'), 1):
        mag = re.sub(r'//.*', '', sor)
        if 'int32_t' in mag or 'TIME-OK:' in sor:
            continue
        for m in re.finditer(r'([^<>=!&|]+?)\s*(<=|>=|<|>)\s*([^<>=&|;\)]+)', mag):
            if ido_erteku(m.group(1)) and ido_erteku(m.group(3)):
                ido_hibak.append(f"{f}:{n}: ket abszolut idopont kozvetlen "
                                 f"osszehasonlitasa -> {sor.strip()[:70]}")
                break

if ido_hibak:
    print("\nAz ido-osszehasonlitas ellenorzese MEGBUKOTT:")
    for h in ido_hibak:
        print("  " + h)
    print("\nA millis() 49,7 nap utan korbefordul. Ket abszolut idopontot NEM")
    print("szabad kozvetlenul osszehasonlitani - hasznald a hatarido-alakot:")
    print("    if ((int32_t)(most - hatarido) >= 0) { ... }")
    print("vagy az eltelt-alakot:  if (most - kezdet >= idotartam) { ... }")
    print("Ha egy eset MEGIS helyes, irj a sorra \"// TIME-OK: <indok>\".")
    sys.exit(1)

print(f"3. Ido-osszehasonlitas: rendben, mind atfordulas-biztos "
      f"({len(ido_nevek)} ido-valtozot figyelve).")

# 4. A firmware szoveg-literaljaiban (soros uzenetek, weboldal) nincs ekezetes betu.
EKEZET = set('áéíóöőúüűÁÉÍÓÖŐÚÜŰ')
ekezet_hibak = []
for f in FORRASOK:
    for n, sor in enumerate((GYOKER / f).read_text(encoding='utf-8').split('\n'), 1):
        if sor.strip().startswith('//'):
            continue
        for m in re.finditer(r'"(\\.|[^"\\])*"', sor):
            if EKEZET & set(m.group(0)):
                ekezet_hibak.append(f"{f}:{n}: {m.group(0)[:70]}")
if ekezet_hibak:
    print("\nAz ekezetek ellenorzese MEGBUKOTT (a soros uzenetek ekezet nelkuliek):")
    for h in ekezet_hibak:
        print("  " + h)
    sys.exit(1)
print("4. Ekezetes betu a szoveg-literalokban: nincs.")
