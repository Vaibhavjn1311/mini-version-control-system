CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
LDFLAGS = -lssl -lcrypto -lz

TARGET = mygit
SRCS = mygit.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)