#!/usr/bin/env python3
"""Forras-szintu ellenorzesek: olyan szabalyok, amiket eddig csak a fegyelem
tartott be.

1. Minden BLOKKOLO ciklus etesse a watchdogot.
2. A program NE allokaljon dinamikusan.
3. Az IDO-OSSZEHASONLITAS legyen atfordulas-biztos (a millis() 49,7 naponta
   korbefordul).

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

print("1. Blokkolo ciklusok: rendben, mind etet.")

# ---------------------------------------------------------------------------
# 2. NINCS DINAMIKUS FOGLALAS.
#
# A README es a MUKODES.md is ALLITJA ezt ("a sketch maga semmit nem allokal
# dinamikusan"), es igaz is - de eddig SEMMI nem tartotta be. Egy heap-foglalas
# egy honapokig futo eszkozon toredezettseget okoz, es epp azt a hibat hozza be,
# ami ellen a heap-felugyelet vedeni probal.
#
# A String NEM tiltott onmagaban: a webszerver a parametereket String-ben adja,
# es azokat REFERENCIAKENT vesszuk at (const String&) - masolas, tehat foglalas
# nelkul. Egy ERTEK szerinti String valtozo viszont mar foglal, ezert azt is
# nezzuk.
# ---------------------------------------------------------------------------
TILTOTT = [
    (r'\b(malloc|calloc|realloc|strdup|strndup)\s*\(', 'C heap-foglalas'),
    (r'\bnew\s+[A-Za-z_]',                            'operator new'),
    (r'\bstd::(vector|string|map|set|list|deque)\b',   'dinamikusan foglalo STL tarolo'),
    (r'^\s*String\s+\w+\s*[=;]',                     'ertek szerinti String (masolat = foglalas)'),
]

alloc_hibak = []
for f in FORRASOK:
    p2 = pathlib.Path(f)
    if not p2.exists():
        continue
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


# ---------------------------------------------------------------------------
# 3. IDO-OSSZEHASONLITAS: CSAK ATFORDULAS-BIZTOS ALAKBAN
#
# A millis() 32 bites, es 49,7 nap utan KORBEFORDUL. Ket helyes alak van:
#
#   a) ELTELT-ALAK:    (most - kezdet) < idotartam
#      Elojel nelkuli kivonas, ez maga atfordulas-biztos: a kulonbseg akkor is
#      helyes, ha a "most" mar korbefordult, a "kezdet" pedig meg nem.
#
#   b) HATARIDO-ALAK:  (int32_t)(most - hatarido) >= 0
#      Amikor egy ABSZOLUT idopontot kell elerni. Az elojeles kulonbseg
#      helyesen mondja meg, melyik van elorebb.
#
# A HIBAS alak ket abszolut idopont KOZVETLEN osszehasonlitasa:
#
#      if (most >= hatarido)          // HIBAS
#
# Ez 49,7 naponta EGYSZER hibazik, kiszamithatatlan pillanatban, es a hiba
# semmilyen teszten nem latszik, ami rovidebb ideig fut. Egy ilyen sor
# eszrevetlenul bekerulhet - ma ket hatarido-osszehasonlitas van a programban,
# es MINDKETTO helyes, de eddig ezt is csak a fegyelem tartotta be.
#
# MIT NEZ. Minden <, >, <=, >= osszehasonlitast, aminek MINDKET oldala
# ido-erteku (millis()-bol szarmazo valtozo vagy maga a millis()). Ha ilyenkor
# nincs (int32_t) jelolés, az hiba. Az eltelt-alak igy nem akad fenn: ott a
# jobb oldal idotartam (konstans), nem idopont.
#
# Az == es a != SZANDEKOSAN nincs benne: azok atfordulastol fuggetlenul
# helyesek, mert nem rendezest kerdeznek.
# ---------------------------------------------------------------------------

# Az ido-valtozok. A lista nagy resze GEPILEG all elo (minden X, amire valahol
# "X = millis()" vagy "X = millis() + ..." all), a curated resz azokat fogja
# meg, amik struktura-tagkent vagy parameterkent kapjak az erteket.
IDO_KEZI = {'millis', 'now', 'currentMillis', 'apDeadline', 'restartAt',
            'lastProbe', 'startMillis', 'stateStart', 'resetPulseStart',
            'fatalStart', 'blinkLast', 'heapCheckLast', 'heapLogLast',
            'resetDelayProbeLast', 'firstStartProbeLast', 'btnResetDownAt',
            'btnWifiResetDownAt', 'resetBtnDownSince', 'wifiResetBtnDownSince'}

ido_nevek = set(IDO_KEZI)
for f in FORRASOK:
    p3 = pathlib.Path(f)
    if not p3.exists():
        continue
    szoveg3 = p3.read_text(encoding='utf-8')
    for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*millis\s*\(\s*\)', szoveg3):
        ido_nevek.add(m.group(1))

IDO_MINTA = re.compile(r'\b(' + '|'.join(sorted(ido_nevek)) + r')\b')

def ido_erteku(kif):
    """Ido-ERTEKU-e ez a reszkifejezes? A kivonas (a - b) mar IDOTARTAM, nem
    idopont, ezert az nem szamit annak - epp ez teszi az eltelt-alakot
    szabalyossa."""
    mag = kif.strip()
    if not mag:
        return False
    if re.search(r'[A-Za-z0-9_\)\]]\s*-\s*[A-Za-z_(]', mag):
        return False          # kulonbseg -> idotartam
    return bool(IDO_MINTA.search(mag))

ido_hibak = []
for f in FORRASOK:
    p3 = pathlib.Path(f)
    if not p3.exists():
        continue
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
