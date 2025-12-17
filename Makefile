CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -pthread
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
TARGET = bin/server

SRCS = $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/**/*.cpp)
OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(notdir $(SRCS)))

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/*/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

.PHONY: clean test

test: bin/test
	./bin/test

bin/test: tests/thread_pool_test.cpp $(SRC_DIR)/pool/thread_pool.cpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -o $@ $^

clean:
	rm -rf $(OBJ_DIR) bin/server bin/test
