#pragma once
// A valódi RTC_NOINIT_ATTR resetet is túlélő RTC memóriába tesz. A tesztben
// sima globális; a persztenciát a coldBoot() paramétere modellezi.
#define RTC_NOINIT_ATTR

// A valódi IRAM_ATTR a függvényt IRAM-ba teszi, hogy megszakításból akkor is
// futhasson, ha a flash épp foglalt. A hoston nincs jelentése.
#define IRAM_ATTR
