#pragma once
// A valódi RTC_NOINIT_ATTR resetet is túlélő RTC memóriába tesz. A tesztben
// sima globális; a persztenciát a coldBoot() paramétere modellezi.
#define RTC_NOINIT_ATTR
