CXX = g++
CXXFLAGS = -std=c++17 -g -O0 -w -I/usr/include/boost -Wno-deprecated-declarations
LDFLAGS = -lboost_system -lpthread -lssl -lcrypto

# 定义目标文件
TARGET = client

# 定义源文件
SRC = $(wildcard *.cpp)

# 定义目标文件
OBJS = $(SRC:.cpp=.o)

# 默认目标
all: $(TARGET)

# 目标文件
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 通用规则
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理目标
clean:
	rm -f $(TARGET) $(OBJS)
