.PHONY: clean

snake: snake.c
	cc -g -o $@ $^ -lnotcurses-core

clean:
	rm -f snake
