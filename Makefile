# UltraHFT Makefile
# Manages building tests, benchmarks, and library components

.PHONY: all clean test bench help

# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O3 -march=native
DEBUG_FLAGS := -g -O0
INCLUDE_DIR := include
SRC_DIR := src
TEST_DIR := tests/unit
BENCH_DIR := bench
BUILD_DIR := build
# DPDK support
DPDK_CFLAGS := $(shell pkg-config --cflags libdpdk 2>/dev/null)
DPDK_LIBS := $(shell pkg-config --libs libdpdk 2>/dev/null)
DPDK_AVAILABLE := $(if $(DPDK_CFLAGS),yes,no)

# Add DPDK flags if available
ifeq ($(DPDK_AVAILABLE),yes)
CXXFLAGS += $(DPDK_CFLAGS)
LDFLAGS += $(DPDK_LIBS) -lnuma
endif

# Source files
CORE_SOURCES :=
COMMON_SOURCES :=

ALL_SOURCES := $(CORE_SOURCES) $(COMMON_SOURCES)

# Test targets
TEST_TARGETS := memory_pool_test order_book_test timer_wheel_test market_data_handler_test

# Benchmark targets
BENCH_TARGETS := order_book_bench ring_buffer_bench timer_bench

# Example targets
EXAMPLE_TARGETS := market_data_pipeline_demo

# All executables (with build directory prefix)
ALL_TARGETS := $(addprefix $(BUILD_DIR)/, $(TEST_TARGETS) $(BENCH_TARGETS) $(EXAMPLE_TARGETS))

# Default target
all: $(ALL_TARGETS)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Test targets
$(BUILD_DIR)/memory_pool_test: $(TEST_DIR)/memory_pool_test.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

$(BUILD_DIR)/order_book_test: $(TEST_DIR)/order_book_test.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

$(BUILD_DIR)/timer_wheel_test: $(TEST_DIR)/timer_wheel_test.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

$(BUILD_DIR)/market_data_handler_test: $(TEST_DIR)/market_data_handler_test.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

# Benchmark targets
$(BUILD_DIR)/order_book_bench: $(BENCH_DIR)/order_book_bench.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

$(BUILD_DIR)/ring_buffer_bench: $(BENCH_DIR)/ring_buffer_bench.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

$(BUILD_DIR)/timer_bench: $(BENCH_DIR)/timer_bench.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

# Example targets
$(BUILD_DIR)/market_data_pipeline_demo: examples/market_data_pipeline_demo.cpp $(ALL_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $< $(ALL_SOURCES) $(LDFLAGS)

# Run all tests
test: $(addprefix $(BUILD_DIR)/, $(TEST_TARGETS))
	@echo "Running tests..."
	@for test in $(TEST_TARGETS); do \
		echo "\n--- Running $$test ---"; \
		./$(BUILD_DIR)/$$test || exit 1; \
	done
	@echo "\nAll tests passed!"

# Run all benchmarks
bench: $(addprefix $(BUILD_DIR)/, $(BENCH_TARGETS))
	@echo "Running benchmarks..."
	@for bench in $(BENCH_TARGETS); do \
		echo "\n--- Running $$bench ---"; \
		./$(BUILD_DIR)/$$bench; \
	done

# Debug builds with symbols and no optimization
debug: CXXFLAGS = -std=c++20 -Wall -Wextra $(DEBUG_FLAGS)
debug: clean
	$(MAKE) CXXFLAGS="$(CXXFLAGS)" all
	@echo "Debug build complete"

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# Help target
help:
	@echo "UltraHFT Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make all        - Build all tests and benchmarks (default)"
	@echo "  make test       - Build and run all tests"
	@echo "  make bench      - Build and run all benchmarks"
	@echo "  make build/market_data_pipeline_demo - Build DPDK ITCH receiver demo"
	@echo "  make debug      - Build with debug symbols and no optimization"
	@echo "  make clean      - Remove all build artifacts"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build everything"
	@echo "  make test               # Build and run tests"
	@echo "  make order_book_bench   # Build only order_book_bench"
	@echo "  make clean && make test # Clean and rebuild tests"

.PHONY: all clean test bench debug help
