# Host build: a valódi sketch és modulok, stub Arduino/ESP32 API-k felett; IDF 5 és IDF 6 ágon.
CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-overloaded-virtual -Istubs -I..
SKETCH   := ../bazsi_router_reboot.ino
MODULES  := ../sync.cpp ../webportal.cpp ../configstore.cpp ../eventlog.cpp ../strutil.cpp ../secret.cpp ../netprobe.cpp
BUILD    := build
SRC      := $(BUILD)/sketch.cpp $(MODULES) stubs/main_stub.cpp test_main.cpp
DEPS     := $(BUILD)/sketch.cpp $(MODULES) $(wildcard ../*.h) test_main.cpp \
            stubs/main_stub.cpp $(wildcard stubs/*.h) $(wildcard stubs/driver/*.h)

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

# ASan + UBSan alatt ugyanazok a forgatókönyvek.
$(BUILD)/run-san: $(DEPS)
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -DESP_IDF_VERSION_MAJOR=5 -o $@ $(SRC)

san: $(BUILD)/run-san
	@echo "=== ASan + UBSan (IDF 5) ==="
	@ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 $(BUILD)/run-san

# Sorlefedettség; a covgate küszöb alatt bukik.
$(BUILD)/run-cov: $(DEPS)
	$(CXX) $(CXXFLAGS) -O0 --coverage -DESP_IDF_VERSION_MAJOR=5 -DCOVERAGE_BUILD -o $@ $(SRC)

cov: $(BUILD)/run-cov
	@rm -f *.gcov $(BUILD)/*.gcda
	@$(BUILD)/run-cov > /dev/null 2>&1 || true
	@python3 covreport.py $(BUILD) $(BUILD)/run-cov

COV_MIN ?= 98.8

covgate: $(BUILD)/run-cov
	@rm -f *.gcov $(BUILD)/*.gcda
	@$(BUILD)/run-cov > /dev/null 2>&1 || true
	@python3 covreport.py $(BUILD) $(BUILD)/run-cov $(COV_MIN)

# Forrásszabályok: watchdog-etetés, nincs dinamikus foglalás, átfordulás-biztos idők.
lint:
	@python3 ../tools_lint.py

# Szigorú figyelmeztetések -O2-vel (csonkolás, túlcsordulás).
WARNFLAGS := -O2 -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-overloaded-virtual \
             -Wformat-truncation=2 -Wformat-overflow=2 \
             -Wstringop-truncation -Wstringop-overflow=4 -Werror

warn: $(BUILD)/sketch.cpp
	@for f in $(BUILD)/sketch.cpp $(MODULES); do \
	   $(CXX) -std=gnu++17 $(WARNFLAGS) -Istubs -I.. -DESP_IDF_VERSION_MAJOR=5 \
	          -c $$f -o /dev/null || exit 1; \
	 done
	@echo "Szigoru figyelmeztetesek (-O2, csonkolas/tulcsordulas): tiszta."

# A HTTP kezelők veremkerete az async_tcp taskon.
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

# libFuzzer a négy bemenet-elemzőre (POST, titok, naplófájl, konfig).
FUZZCXX  ?= clang++
FUZZSEC  ?= 30
FUZZFLAGS = -std=gnu++17 -g -O1 -Istubs -I.. -DESP_IDF_VERSION_MAJOR=5 \
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
            -Wno-unused-parameter -Wno-overloaded-virtual
FUZZSRC   = $(BUILD)/sketch.cpp $(MODULES) stubs/main_stub.cpp fuzz_main.cpp
FUZZBINS  = $(BUILD)/fuzz-post $(BUILD)/fuzz-secret $(BUILD)/fuzz-evlog $(BUILD)/fuzz-config

$(BUILD)/fuzz-post:   $(DEPS) fuzz_main.cpp
	$(FUZZCXX) $(FUZZFLAGS) -DFUZZ_TARGET=1 -o $@ $(FUZZSRC)
$(BUILD)/fuzz-secret: $(DEPS) fuzz_main.cpp
	$(FUZZCXX) $(FUZZFLAGS) -DFUZZ_TARGET=2 -o $@ $(FUZZSRC)
$(BUILD)/fuzz-evlog:  $(DEPS) fuzz_main.cpp
	$(FUZZCXX) $(FUZZFLAGS) -DFUZZ_TARGET=3 -o $@ $(FUZZSRC)
$(BUILD)/fuzz-config: $(DEPS) fuzz_main.cpp
	$(FUZZCXX) $(FUZZFLAGS) -DFUZZ_TARGET=4 -o $@ $(FUZZSRC)

.PHONY: fuzz
fuzz: $(FUZZBINS)
	@rc=0; for b in $(FUZZBINS); do \
	  echo "=== $$b ($(FUZZSEC) mp) ==="; \
	  mkdir -p corpus/$$(basename $$b); \
	  ASAN_OPTIONS=detect_leaks=0 $$b corpus/$$(basename $$b) \
	    -max_total_time=$(FUZZSEC) -print_final_stats=1 -rss_limit_mb=4096 \
	    2>&1 | grep -E "^#|Done|stat::|ERROR|SUMMARY|crash" | tail -8 || rc=1; \
	done; \
	if [ $$rc -ne 0 ]; then echo "FUZZING: TALALAT - lasd a crash-* fajlokat"; exit 1; fi; \
	echo "Fuzzing: mind a negy celpont tisztan futott."

# A stubok igazolása a valódi könyvtárak ellen: STUBROOTS="<gyökerek>" make stubcheck.
.PHONY: stubcheck
stubcheck:
	@if [ -z "$(STUBROOTS)" ]; then \
	  echo "stubcheck: adj meg gyokereket, pl. STUBROOTS=\"\$$HOME/.arduino15\" make stubcheck"; \
	  exit 2; \
	fi
	@python3 ../tools_stubcheck.py $(STUBROOTS)
