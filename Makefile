.PHONY: format
test: test.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

main: main.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

format: 
	astyle --style=google  channel.h main.cpp test.cpp