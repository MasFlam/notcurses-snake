.PHONY: clean

snake: snake.c
	cc -o $@ $^ -lnotcurses-core

clean:
	rm -f snake
