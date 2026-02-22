# 1. Compiler and Linker
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O3 -I./include -I/home/ubuntu/try/include
LDFLAGS = -luring -lpthread

# 2. Files - REMOVED THE COMMA HERE
SRCS = src/server.cpp src/utils.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = my_server

# 3. Default Rule
all: $(TARGET)

# 4. Linking the Target
# Ensure the line below starts with a TAB
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

# 5. Compiling Source Files
# Ensure the line below starts with a TAB
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 6. Clean up
clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean