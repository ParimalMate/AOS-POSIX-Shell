CXX = g++
CXXFLAGS = -std=c++11 -Wall

SRCS = main.cpp builtins.cpp execution.cpp redirection.cpp history.cpp utils.cpp signals.cpp autocomplete.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = shell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp $(wildcard *.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)