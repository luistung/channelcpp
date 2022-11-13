test: test.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.

main: main.cpp channel.h
	g++ -std=c++20 -g -O0 -o $@ $< -I.
