test: test.cpp
	g++ -std=c++20 -g -O0 -o $@ $^ -I.

main: main.cpp
	g++ -std=c++20 -g -O0 -o $@ $^ -I.
