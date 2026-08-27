# A tényleges sketch-et fordítja host gcc-vel, stub Arduino/ESP32 API-k felett.
# A suite KÉTSZER fut: az ESP32 Arduino 3.3.11 által használt IDF 5-ös ágon, és
# az IDF 6-os ágon is (a gomb-ébresztés API-ját ott átnevezték).
CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-overloaded-virtual -Istubs
SKETCH   := ../bazsi_router_reboot.ino
BUILD    := build
SRC      := $(BUILD)/sketch.cpp stubs/main_stub.cpp test_main.cpp

# A sanitizeres futtatas kulon cel: ugyanaz a 111 forgatokonyv, de ASan+UBSan
# alatt. Puffertulcsordulast, hasznalat-utani-felszabaditast es definialatlan
# viselkedest fog meg - olyasmit, amit egy CHECK() sosem latna.
.PHONY: test san cov clean
test: $(BUILD)/run-idf5 $(BUILD)/run-idf6
	@echo "=== IDF 5 (ESP32 Arduino 3.3.11) ==="
	@$(BUILD)/run-idf5
	@echo
	@echo "=== IDF 6 (előretekintő ág) ==="
	@$(BUILD)/run-idf6

$(BUILD)/sketch.cpp: $(SKETCH) | $(BUILD)
	@cp $(SKETCH) $@

$(BUILD)/run-idf5: $(BUILD)/sketch.cpp test_main.cpp stubs/main_stub.cpp $(wildcard stubs/*.h)
	$(CXX) $(CXXFLAGS) -DESP_IDF_VERSION_MAJOR=5 -o $@ $(SRC)

$(BUILD)/run-idf6: $(BUILD)/sketch.cpp test_main.cpp stubs/main_stub.cpp $(wildcard stubs/*.h)
	$(CXX) $(CXXFLAGS) -DESP_IDF_VERSION_MAJOR=6 -o $@ $(SRC)

$(BUILD)/run-san: $(BUILD)/sketch.cpp test_main.cpp stubs/main_stub.cpp $(wildcard stubs/*.h)
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -DESP_IDF_VERSION_MAJOR=5 -o $@ $(SRC)

san: $(BUILD)/run-san
	@echo "=== ASan + UBSan (IDF 5) ==="
	@ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 $(BUILD)/run-san

# Sorlefedettseg: megmutatja, mely agakat NEM futtatja egyetlen forgatokonyv
# sem. A gyerekprocesszek _exit()-tel lepnek ki, ami nem uritene a szamlalokat,
# ezert a COVERAGE_BUILD kezzel hivja a __gcov_dump()-ot.
$(BUILD)/run-cov: $(BUILD)/sketch.cpp test_main.cpp stubs/main_stub.cpp $(wildcard stubs/*.h)
	$(CXX) $(CXXFLAGS) -O0 --coverage -DESP_IDF_VERSION_MAJOR=5 -DCOVERAGE_BUILD -o $@ $(SRC)

cov: $(BUILD)/run-cov
	@rm -f *.gcov $(BUILD)/*.gcda
	@$(BUILD)/run-cov > /dev/null 2>&1 || true
	@gcov -o $(BUILD)/run-cov-sketch $(BUILD)/sketch.cpp 2>/dev/null | grep -A1 "sketch.cpp" | head -2
	@echo
	@echo "Egyetlen forgatokonyv altal sem erintett sorok:"
	@grep -n "#####" sketch.cpp.gcov | sed 's/^ *//' \
	  | awk -F: '{n=$$3; $$1=$$2=$$3=""; sub(/^ +/,""); print "  " n": " $$0}' \
	  | grep -v "^ *[0-9]*: }$$" || echo "  (nincs ilyen)"

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD) *.gcov
