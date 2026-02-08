# 1. Compiler and Linker
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O3 -I./include -I/home/ubuntu/try/include
# The Linker Flags: -luring links the io_uring library, -lpthread links thread support
LDFLAGS = -luring -lpthread

# 2. Files
TARGET = my_server
SRCS = src/server.cpp 
OBJS = $(SRCS:.cpp=.o)

# 3. Default Rule
all: $(TARGET)

# 4. Linking the Target
# This takes your compiled .o files and links them with the libraries
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $(TARGET)

# 5. Compiling Source Files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 6. Clean up
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean