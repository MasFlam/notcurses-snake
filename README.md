# Notcurses Snake
Terminal snake game made using [Notcurses](https://github.com/dankamongmen/notcurses). A practical
example use of `ncvisual`. The code is only 300 lines long and aims to be easy to read, so hack on
it as much as you'd like.

# Controls
Use the left and right arrow keys to turn the snake, and `q` to quit. You can set the seed used for
spawning food by setting the `SNAKE_FOOD_SEED` environmental variable to an integer. Otherwise the
seed is set to whatever the unix time happens to be during the init phase.

# Building
Make sure Notcurses is installed (on Debian it's `libnotcurses-dev`). Then run `make`
to compile and link the `snake` executable.
