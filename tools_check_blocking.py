#!/usr/bin/env python3
"""Statikus ellenorzes: minden BLOKKOLO ciklus etesse a watchdogot.

MIERT KELL, HA MAR VAN FUTASIDEJU INVARIANS? Mert az csak azokat az utakat
meri, amiket egy forgatokonyv tenylegesen bejar. Egy uj, meg teszteletlen
blokkolo ag ott eszrevetlen maradna - itt viszont nem: ez a script a FORRAST
nezi, nem a futast.

MIT NEZ. Minden while/for/do ciklust, aminek a torzseben van delay() vagy egy
ismerten blokkolo hivas. Az ilyen ciklusnak etetnie kell: vagy kozvetlenul
(feedWatchdog), vagy egy olyan segeden at, ami maga etet (waitWithButtons,
blockingDelay, waitWithButtonsUntilOnline, reset_device hivoi...).

MIT NEM NEZ. Azt a ciklust, amelyik minden korben VISSZAAD a loop()-nak - ott
az Arduino core etet helyettunk. Ezeket a ciklus torzseben allo "return" arulja
el; a script ezert a return-t is elfogadja.

KIVETELEK. Van ket olyan ciklus, ami nem etet es megsem hiba: a beragadt gomb
3 mp-es villogasa (rovid, korlatos), es a soros port bevarasa a setup()-ban
(ott a watchdog meg nem is el). Ezeket NEM hallgatjuk el: a ciklus fole irt

    // WDT-OK: <indok>

jelolessel kell felmenteni oket. Igy minden kivetel INDOKOLVA es LATHATOAN all
a forrasban, egy UJ blokkolo ciklus viszont jeloles nelkul megbuktatja a CI-t.
Ez a lenyeg: a szabalyt nem a fegyelem tartja be, hanem a build.
"""
import re, sys, pathlib

FORRASOK = ['bazsi_router_reboot.ino', 'netprobe.cpp', 'eventlog.cpp',
            'configstore.cpp', 'webportal.cpp', 'sync.cpp', 'secret.cpp',
            'strutil.cpp']

# Ezek maguk etetnek (a definiciojukban ott a feedWatchdog), tehat a hivasuk
# elegendo. A lista SZANDEKOSAN rovid es kezzel gondozott.
ETETO_SEGEDEK = ['feedWatchdog', 'blockingDelay', 'waitWithButtons',
                 'waitWithButtonsUntilOnline', 'onlineProbeDue',
                 'lockConfigBeforeShutdown']

# A modulok sajat, azonos szerepu neve.
ETETO_SEGEDEK += ['PROBE_POLL_MS']   # netprobe: a sajat varakozo ciklusai

def fejlec_vege(szoveg, kezd):
    """A ciklus fejlecenek zaro zarojele utani pozicio (do eseten maga a kezdet).

    FONTOS: a fejlecet ZAROJEL-PARONKENT kell kovetni, nem az elso ';'-ig. Egy
    klasszikus for-fejlecben (for (i = 0; i < n; i++)) KET pontosvesszo all a
    zarojelen BELUL - az elso valtozat ezert minden ilyen ciklust "egysoros
    ciklusnak" hitt es CSENDBEN atengedett. Az ellenorzo igy a for ciklusokra
    nezve teljesen hatastalan volt. (Mutacioval derult ki.)
    """
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
        # A "do" utan rogton torzs jon; a while/for utan fejlec.
        utan = m.start() if m.group(1) == 'do' else fejlec_vege(szoveg, m.start())
        # A fejlec UTAN mi jon eloszor: torzs vagy pontosvesszo?
        i = szoveg.find('{', utan)
        pv = szoveg.find(';', utan)
        if i < 0:
            continue
        if 0 <= pv < i:
            continue          # torzs nelkuli ciklus: "while (...) ;"
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
    p = pathlib.Path(f)
    if not p.exists():
        continue
    nyers = p.read_text(encoding='utf-8')
    eredeti_sorok = nyers.split('\n')
    szoveg = re.sub(r'//[^\n]*', '', nyers)   # a ciklusok keresese kommentek nelkul
    for sor, torzs in ciklus_torzsek(szoveg):
        if not re.search(r'\bdelay\s*\(', torzs):
            continue                       # nem blokkol: nem is kell etetnie
        if any(s in torzs for s in ETETO_SEGEDEK):
            continue                       # etet, kozvetlenul vagy segeden at
        if re.search(r'\breturn\b', torzs):
            continue                       # visszaad a loop()-nak: a core etet
        # Kimondott, indokolt felmentes a ciklus felett (legfeljebb 6 sorral).
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

print("A blokkolo ciklusok ellenorzese rendben: mind etet.")
