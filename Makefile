.PHONY: format test

test_bin: test.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

test: test_bin
	sh test.sh

main: main.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

format: 
	astyle --style=google  channel.h main.cpp test.cpp