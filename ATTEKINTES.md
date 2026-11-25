# Röviden: hogyan működik

Egy ESP32-C3 percenként megnézi, van-e internet. Ha egy ideje nincs, egy
relével 90 másodpercre áramtalanítja a routert: 2 perc után, ha a router sem
válaszol, 15 perc után, ha csak a szolgáltató felé szakadt meg. Ha ez nem
segít, egyre ritkábban próbálja újra.

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

Percenként megnézi, van-e internet: ping az `1.1.1.1`-re, ha nem válaszol, a
`8.8.8.8`-ra, és ha az sem, egy HTTP teszt (öt üzemeltető közül a soron
következő). Bármelyik siker elég, és mindent nulláz.

## Ha nincs internet

Minden körben a router pingjét is megnézi:

| Helyzet | Router reset |
|---|---|
| a router **sem** válaszol (vagy nincs Wi-Fi) | **2 perc** után |
| a router válaszol, az internet nem (szolgáltatói hiba) | **15 perc** után |
| a reset nem segített | a következő **30 perc → 1 óra → 2 óra → 4 óra** múlva, onnan 4 óránként |

A **reset**: 90 mp áramtalanítás, legfeljebb 10 perc várakozás a router
bootolására (korábban véget ér, ha a hálózat visszajött), majd újracsatlakozás.
Alvás ezen az ágon nincs: az eszköz végig figyel.

Ha a router pingre sosem válaszol, a szolgáltatói kiesés is 2 perc után kap
resetet; reset-hurok ebből sem lesz, mert a lépcső ritkítja.

Ismert korlát: ha a routernek csak a DNS-e fagy le, a ping megy, és nincs reset.

## Ha nincs Wi-Fi

- 3 csatlakozási próba, köztük 30 mp.
- Rossz jelszó → beállító mód (AP), router reset nélkül.
- A hálózat nem látszik → router reset, majd 1 óra alvás és új kör; kb.
  46 óra után beállító mód.

## Napló

- Soros porton: `LOG` (vagy `/log`) – a teljes eseménynapló.
- Beállító módban: a `192.168.4.1/log` oldal.
- A legfontosabb bejegyzések: `TEST FAIL` + `HTTP ERROR` (a kiesés kezdete),
  `NET DIAG` (1 = a router sem válaszolt, 2 = szolgáltatói hiba),
  `ROUTER RESET` (hányadik ebben a kiesésben).

## Tovább

| Dokumentum | Miről szól |
|---|---|
| [README.md](README.md) | telepítés, hardver, részletes működés |
| [MUKODES.md](MUKODES.md) | minden időzítés és döntés táblázatban |
| [FOLYAMATABRA.md](FOLYAMATABRA.md) | folyamatábrák |
| [test/README.md](test/README.md) | a tesztkészlet |
