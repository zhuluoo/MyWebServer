CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -pthread
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
TARGET = bin/server

SRCS = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/**/*.cpp)
OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(notdir $(SRCS)))

$(TARGET): $(SRC_DIR)/main.cpp $(SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -o $@ $^

# Optional: per-source object rule retained for convenience (unused by default)
$(OBJ_DIR)/%.o: $(SRC_DIR)/*/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

.PHONY: clean test http_test thread_test

# Run both tests
test: http_test thread_test
	./bin/http_test
	./bin/thread_test

# Discover tests and provide a generic rule: each tests/NAME.cpp -> bin/NAME
TEST_SRCS := $(wildcard tests/*.cpp)
TEST_BINS := $(patsubst tests/%.cpp, bin/%,$(TEST_SRCS))

# Individual test targets (convenience)
http_test: bin/http_conn_test
thread_test: bin/thread_pool_test

# Generic rule to build a test binary from its test source and project sources
bin/%: tests/%.cpp $(SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -o $@ $^

clean:
	rm -rf $(OBJ_DIR) bin/http_conn_test bin/thread_pool_test bin/server
