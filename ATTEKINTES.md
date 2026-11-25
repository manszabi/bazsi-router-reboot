# Röviden: hogyan működik

Egy ESP32-C3 figyeli az internetet, és ha **a router** a hibás, egy relével
90 másodpercre áramtalanítja. Ha a hiba máshol van (szolgáltató, szűrő,
Wi-Fi-zavar), a routerhez nem vagy csak egyszer nyúl.

## Bekötés

| Láb | Mire |
|---|---|
| `D10` | relé – **HIGH = a router áram nélkül**; 22 kΩ lehúzó a GND felé (ajánlott: a router tápja a relé alaphelyzetben zárt, NC érintkezőjén) |
| `D1` | reset gomb (deep sleepből is ébreszt) |
| `D0` | Wi-Fi reset gomb – bekapcsolás közben ne legyen nyomva (strapping láb) |
| `D4` / `D3` | státusz LED / Wi-Fi LED |

## Első beállítás

1. Bekapcsolás után, mentett Wi-Fi nélkül, az eszköz saját hozzáférési pontot
   indít: `ESP-ESP32-C3-XXXX`.
2. A jelszava **eszközönként egyedi**: a soros monitoron jelenik meg
   (115200 baud). Írd fel egy címkére.
3. Csatlakozz, nyisd meg a `192.168.4.1` címet, add meg a Wi-Fi nevét és
   jelszavát (statikus IP-hez az IP-t és a gateway-t is).

## Normál működés

- Percenként egy HTTP teszt, öt független üzemeltető végpontja közül a
  soron következőre (Microsoft, Cloudflare, Mozilla, GNOME, Google).
- Egy bukott teszt után 12 mp múlva a következő végpont jön. Bármelyik siker
  mindent nulláz.

## Ha mind az öt teszt bukik

Az eszköz megnézi, hol a hiba:

| Az internet pingje (1.1.1.1 vagy 8.8.8.8) | A router pingje | Döntés |
|---|---|---|
| válaszol | – | csak a HTTP bukik → **nincs reset**, 10 perc múlva újra teszt |
| nem válaszol | válaszol | szolgáltatói hiba → 3 perc múlva újra 5 teszt, és ha az is bukik, **egy** reset kiesésenként |
| nem válaszol | nem válaszol | router hiba → **azonnali reset** |

A **reset**: 90 mp áramtalanítás, legfeljebb 10 perc várakozás a router
bootolására (korábban véget ér, ha a hálózat visszajött), majd újracsatlakozás.
Ha 4 reset sem segít, 1 óra alvás, utána ébredésenként 1 reset.

Ha a router pingre sosem válaszol, az eszköz ezt megtanulja, és ilyenkor csak
az internet pingje alapján dönt.

## Ha nincs Wi-Fi

- 3 csatlakozási próba, köztük 30 mp.
- Rossz jelszó → beállító mód (AP), router reset nélkül.
- A hálózat nem látszik → router reset, majd 1 óra alvás és új kör; kb.
  46 óra után beállító mód.

## Napló

- Soros porton: `LOG` (vagy `/log`) – a teljes eseménynapló.
- Beállító módban: a `192.168.4.1/log` oldal.
- A legfontosabb bejegyzések: `TEST FAIL` + `HTTP ERROR` (melyik teszt bukott
  és miért), `NET DIAG` (hol volt a hiba), `ROUTER RESET`, `RESET SKIPPED`
  (miért maradt el a reset).

## Tovább

| Dokumentum | Miről szól |
|---|---|
| [README.md](README.md) | telepítés, hardver, részletes működés |
| [MUKODES.md](MUKODES.md) | minden időzítés és döntés táblázatban |
| [FOLYAMATABRA.md](FOLYAMATABRA.md) | folyamatábrák |
| [test/README.md](test/README.md) | a tesztkészlet |
