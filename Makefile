test: test.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

main: main.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

format: channel.h main.cpp test.cpp
	astyle --style=google  $^