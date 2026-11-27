# 🔄 Bazsi Router Reboot

ESP32-C3 alapú automatikus router-újraindító. Percenként megnézi, van-e
internet, és ha a hiba a routerben van, relével áramtalanítja a routert –
egyre ritkábban, ha az újraindítás nem segít.

## Dokumentáció

| | Miről szól |
|---|---|
| **[ATTEKINTES.md](ATTEKINTES.md)** | egyoldalas összefoglaló: bekötés, első beállítás, működés, napló |
| **[MUKODES.md](MUKODES.md)** | a teljes leírás: minden időzítés és döntés, hardver (17.), telepítés (18.), biztonság (19.), időzítések (20.), folyamatábrák (21.) |
| [test/README.md](test/README.md) | a host tesztkészlet |

## Gyors kezdés

1. Hardver és bekötés: [MUKODES.md 17. fejezet](MUKODES.md#17-hardver-és-bekötés).
2. Fordítás és feltöltés (Arduino IDE vagy PlatformIO, a pontos
   könyvtárverziókkal): [MUKODES.md 18. fejezet](MUKODES.md#18-telepítés).
3. Első indításkor az eszköz saját Wi-Fi-t indít (`ESP-ESP32-C3-XXXX`, a jelszó
   a soros porton); a `192.168.4.1` oldalon add meg az otthoni Wi-Fi adatait.
4. Normál üzemben az állapot a böngészőből is látszik:
   `http://<az eszköz IP-je>/` (csak olvasható, csak a helyi hálózatról).

## Tesztek

```bash
cd test && make test
```

## Licenc

MIT License – lásd a [LICENSE](LICENSE) fájlt.
