# A tényleges sketch-et fordítja host gcc-vel, stub Arduino/ESP32 API-k felett.
# A suite KÉTSZER fut: az ESP32 Arduino 3.3.11 által használt IDF 5-ös ágon, és
# az IDF 6-os ágon is (a gomb-ébresztés API-ját ott átnevezték).
CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-overloaded-virtual -Istubs -I..
SKETCH   := ../bazsi_router_reboot.ino
# A sketch melletti modulok. Az Arduino ezeket kulon forditasi egysegkent
# forditja es linkeli - a host build UGYANIGY, tehat itt derul ki, ha egy
# deklaracio hianyzik vagy egy szimbolum nem talalhato. Uj modul hozzaadasakor
# EZT a listat kell bovíteni (a -I.. a headereket teszi elerhetove).
MODULES  := ../sync.cpp ../webportal.cpp ../configstore.cpp ../eventlog.cpp ../strutil.cpp ../secret.cpp ../netprobe.cpp
BUILD    := build
SRC      := $(BUILD)/sketch.cpp $(MODULES) stubs/main_stub.cpp test_main.cpp
DEPS     := $(BUILD)/sketch.cpp $(MODULES) $(wildcard ../*.h) test_main.cpp \
            stubs/main_stub.cpp $(wildcard stubs/*.h) $(wildcard stubs/driver/*.h)

# A sanitizeres futtatas kulon cel: ugyanazok a forgatokonyvek, de ASan+UBSan
# alatt. Puffertulcsordulast, hasznalat-utani-felszabaditast es definialatlan
# viselkedest fog meg - olyasmit, amit egy CHECK() sosem latna.
.PHONY: test san cov covgate lint warn stack clean
test: $(BUILD)/run-idf5 $(BUILD)/run-idf6
	@echo "=== IDF 5 (ESP32 Arduino 3.3.11) ==="
	@$(BUILD)/run-idf5
	@echo
	@echo "=== IDF 6 (előretekintő ág) ==="
	@$(BUILD)/run-idf6

$(BUILD)/sketch.cpp: $(SKETCH) | $(BUILD)
	@cp $(SKETCH) $@

$(BUILD)/run-idf5: $(DEPS)
	$(CXX) $(CXXFLAGS) -DESP_IDF_VERSION_MAJOR=5 -o $@ $(SRC)

$(BUILD)/run-idf6: $(DEPS)
	$(CXX) $(CXXFLAGS) -DESP_IDF_VERSION_MAJOR=6 -o $@ $(SRC)

$(BUILD)/run-san: $(DEPS)
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -DESP_IDF_VERSION_MAJOR=5 -o $@ $(SRC)

san: $(BUILD)/run-san
	@echo "=== ASan + UBSan (IDF 5) ==="
	@ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 $(BUILD)/run-san

# Sorlefedettseg: megmutatja, mely agakat NEM futtatja egyetlen forgatokonyv
# sem. A gyerekprocesszek _exit()-tel lepnek ki, ami nem uritene a szamlalokat,
# ezert a COVERAGE_BUILD kezzel hivja a __gcov_dump()-ot.
$(BUILD)/run-cov: $(DEPS)
	$(CXX) $(CXXFLAGS) -O0 --coverage -DESP_IDF_VERSION_MAJOR=5 -DCOVERAGE_BUILD -o $@ $(SRC)

cov: $(BUILD)/run-cov
	@rm -f *.gcov $(BUILD)/*.gcda
	@$(BUILD)/run-cov > /dev/null 2>&1 || true
	@python3 covreport.py $(BUILD) $(BUILD)/run-cov

# Lefedettsegi kapu a CI-nak. Ugyanazt meri, mint a "cov", de EMBERI OLVASAS
# helyett kilepesi kodot ad: a kuszob alatt megbukik. A szam nem oncel - azt
# vedi, hogy egy uj, egyetlen forgatokonyv altal sem erintett ag ne csusszon be
# eszrevetlenul. A kuszob a jelenlegi szint alatt all egy kicsivel, hogy egy
# jogos, atmeneti visszaeses ne blokkoljon - de a szandekos elhanyagolas igen.
COV_MIN ?= 97.0

covgate: $(BUILD)/run-cov
	@rm -f *.gcov $(BUILD)/*.gcda
	@$(BUILD)/run-cov > /dev/null 2>&1 || true
	@python3 covreport.py $(BUILD) $(BUILD)/run-cov $(COV_MIN)

# Statikus ellenorzes: minden blokkolo ciklus etesse a watchdogot. Ez a
# futasideju res-invarianst egesziti ki: az csak a bejart utakat meri, ez
# viszont a FORRAST nezi, tehat a meg teszteletlen uj agakat is elkapja.
lint:
	@python3 ../tools_lint.py

# SZIGORU FIGYELMEZTETESEK, KULON CELBAN.
#
# MIERT NEM A FO BUILDBEN? Mert a -Wformat-truncation es a -Wstringop-* csak
# OPTIMALIZALAS mellett fut le (a fordito a konstansokat elobb terjeszti). A fo
# teszt-build -O0, hogy a lefedettseg es a hibakereses pontos legyen - ott ezek
# a figyelmeztetesek CSENDBEN nem szolalnanak meg. Ezert kulon, -O2-vel.
#
# Amit fog: egy snprintf(), aminek a puffere kicsi a lehetseges kimenethez; egy
# strlcpy/memcpy, ami tul irhat. Ez ma tiszta - a puffermeretek helyesek -, de
# eddig SEMMI nem tartotta be: egy uj mezo vagy egy hosszabb hibauzenet
# eszrevetlenul csonkolt volna.
WARNFLAGS := -O2 -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-overloaded-virtual \
             -Wformat-truncation=2 -Wformat-overflow=2 \
             -Wstringop-truncation -Wstringop-overflow=4 -Werror

warn: $(BUILD)/sketch.cpp
	@for f in $(BUILD)/sketch.cpp $(MODULES); do \
	   $(CXX) -std=gnu++17 $(WARNFLAGS) -Istubs -I.. -DESP_IDF_VERSION_MAJOR=5 \
	          -c $$f -o /dev/null || exit 1; \
	 done
	@echo "Szigoru figyelmeztetesek (-O2, csonkolas/tulcsordulas): tiszta."

# VEREMKOLTSEGVETES az async_tcp taskra.
#
# A ket HTTP kezelo NEM a loop taskon fut, hanem az AsyncTCP sajat taskjan,
# aminek a verme veges. A kod tobb helyen EPP ERRE hivatkozva valaszt kozos
# puffert vagy ket lepest egy helyett - de a szamot eddig semmi nem merte.
#
# OSZINTEN A MERES HATARAIROL: ez HOST (x86-64) veremkeret, nem a RISC-V-e. A
# ket szam nem egyenlo. Iranymutatonak viszont pontos: ha valaki egy 2 KB-os
# puffert tesz a POST kezelobe, az ITT is azonnal latszik.
STACK_LIMIT ?= 1200

stack:
	@rm -f $(BUILD)/*.su
	@$(CXX) -std=gnu++17 -O2 -fstack-usage -Istubs -I.. -DESP_IDF_VERSION_MAJOR=5 \
	        -c ../webportal.cpp -o $(BUILD)/stack.o
	@python3 ../tools_stack.py $(BUILD)/stack.su $(STACK_LIMIT)

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD) *.gcov
