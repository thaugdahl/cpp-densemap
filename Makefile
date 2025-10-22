.PHONY: clean FORCE

main: main.cpp FORCE
	clang++ main.cpp -g -O0 -std=c++17 -o main
