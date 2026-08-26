# A tényleges sketch-et fordítja host gcc-vel, stub Arduino/ESP32 API-k felett.
# Használat:  make test
CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-overloaded-virtual -Istubs
SKETCH   := ../bazsi_router_reboot.ino
BUILD    := build

.PHONY: test clean
test: $(BUILD)/run
	@$(BUILD)/run

$(BUILD)/run: $(SKETCH) test_main.cpp stubs/main_stub.cpp $(wildcard stubs/*.h) | $(BUILD)
	@cp $(SKETCH) $(BUILD)/sketch.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/sketch.cpp stubs/main_stub.cpp test_main.cpp

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
