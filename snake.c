/* Notcurses Snake - Snake game for the terminal, powered by the Notcurses library
 *
 * Copyright (C) 2021 Łukasz "MasFlam" Drukała
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see https://www.gnu.org/licenses/.
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <notcurses/notcurses.h>

#define KEYBIND_TURN_LEFT NCKEY_LEFT
#define KEYBIND_TURN_RIGHT NCKEY_RIGHT
#define KEYBIND_QUIT 'q'

#define FRAME_DELAY_MS 300

#define TEXT_CHANNELS CHANNELS_RGB_INITIALIZER(255, 255, 255, 20, 20, 20)

enum direction {
	NORTH,
	SOUTH,
	EAST,
	WEST
};

struct g_color {
	uint32_t snake;
	uint32_t food;
	uint32_t empty;
};

struct g {
	struct notcurses *nc;
	struct ncplane *stdp;
	ncblitter_e blitter;
	int termw, termh;
	int playw, playh;
	struct ncvisual *ncv;
	enum direction snakedir;
	int snakelen;
	struct segment *snake_head, *snake_tail;
	int foodcount;
	int max_food;
};

struct segment {
	int x, y;
	struct segment *prev, *next;
};

static void add_random_food();
static void cleanup();
static void game_over();
static void init();
static int iskeybind(char32_t c);
static void main_loop();
static void move_snake_head(int x, int y);
static void sleep_millis(long long millis);

struct g g;
struct g_color g_color;

void
add_random_food()
{
	int x, y;
	uint32_t pix = g_color.snake;
	do {
		x = rand() % g.playw, y = rand() % g.playh;
		ncvisual_at_yx(g.ncv, y, x, &pix);
	} while (pix != g_color.empty);
	ncvisual_set_yx(g.ncv, y, x, g_color.food);
	++g.foodcount;
}

void
cleanup()
{
	ncvisual_destroy(g.ncv);
	notcurses_stop(g.nc);
	struct segment *seg = g.snake_tail;
	while (seg->next) {
		seg = seg->next;
		free(seg->prev);
	}
	free(seg);
}

void
game_over()
{
	char buf[100]; // this is really plenty
	sprintf(buf, "Score: %d", g.snakelen * 10);
	int h = 3;
	struct ncplane *n = ncplane_create(g.stdp, &(struct ncplane_options) {
		.x = 0,
		.y = (g.termh - h) / 2,
		.cols = g.termw,
		.rows = h
	});
	ncplane_set_base(n, " ", 0, TEXT_CHANNELS);
	ncplane_set_channels(n, TEXT_CHANNELS);
	ncplane_home(n);
	ncplane_puttext(n, 0, NCALIGN_CENTER, "GAME OVER!", NULL);
	ncplane_home(n);
	ncplane_puttext(n, 1, NCALIGN_CENTER, buf, NULL);
	ncplane_home(n);
	ncplane_puttext(n, 2, NCALIGN_CENTER, "PRESS ANY KEY", NULL);
	notcurses_render(g.nc);
	notcurses_getc_blocking(g.nc, NULL);
	ncplane_destroy(n);
}

void
init()
{
	g_color.snake = ncpixel(0, 255, 0);
	g_color.food = ncpixel(255, 0, 0);
	g_color.empty = ncpixel(0, 0, 0);
	g.nc = notcurses_core_init(&(struct notcurses_options) {
		.flags = NCOPTION_SUPPRESS_BANNERS
	}, stdout);
	g.stdp = notcurses_stddim_yx(g.nc, &g.termh, &g.termw);
	// the comment above this must be wrong coz this gave me 3x2 :/
	g.blitter = ncvisual_media_defblitter(g.nc, NCSCALE_INFLATE);
	if (g.blitter != NCBLIT_1x1) g.blitter = NCBLIT_2x1;
	if (g.blitter == NCBLIT_1x1) {
		g.playw = g.termw;
		g.playh = g.termh;
	} else if (g.blitter == NCBLIT_2x1) {
		g.playw = g.termw;
		g.playh = g.termh * 2;
	}
	// why is there no ncvisual_filled_with()?
	uint8_t *buf = malloc(g.playh * g.playw * 4);
	for (int i = 0; i < g.playh * g.playw; ++i) {
		buf[4*i + 0] = 0;
		buf[4*i + 1] = 0;
		buf[4*i + 2] = 0;
		buf[4*i + 3] = 255;
	}
	g.ncv = ncvisual_from_rgba(buf, g.playh, g.playw * 4, g.playw);
	free(buf);
	g.snakelen = 1;
	g.snakedir = EAST;
	g.snake_head = g.snake_tail = malloc(sizeof(struct segment));
	g.snake_head->x = g.playw / 2;
	g.snake_head->y = g.playh / 2;
	g.snake_head->prev = g.snake_head->next = NULL;
	ncvisual_set_yx(g.ncv, g.snake_head->y, g.snake_head->x, g_color.snake);
	g.foodcount = 0;
	g.max_food = g.playw * g.playh / 200;
}

int
iskeybind(char32_t c)
{
	switch (c) {
	case KEYBIND_QUIT:
	case KEYBIND_TURN_LEFT:
	case KEYBIND_TURN_RIGHT:
		return 1;
	default:
		return 0;
	}
}

void
main_loop()
{
	struct ncinput ni = { .id = -1 };
	char32_t c = -1;
	do {
		// Render, but only if a bound key was pressed or when no key was pressed.
		// Minimizes the effect of holding down an unbound key
		if (c == -1 || iskeybind(c)) {
			ncplane_erase(g.stdp);
			ncvisual_render(g.nc, g.ncv, &(struct ncvisual_options) {
				.n = g.stdp,
				.scaling = NCSCALE_INFLATE,
				.blitter = g.blitter
			});
			ncplane_set_channels(g.stdp, TEXT_CHANNELS);
			ncplane_printf_yx(g.stdp, 0, 0, " Score: %d ", g.snakelen * 10);
			notcurses_render(g.nc);
		}
		// Process user input
		if (!ni.alt && !ni.ctrl && !ni.shift) {
			switch (c) {
			case KEYBIND_TURN_LEFT:
				switch (g.snakedir) {
				case NORTH: g.snakedir = WEST; break;
				case WEST: g.snakedir = SOUTH; break;
				case SOUTH: g.snakedir = EAST; break;
				case EAST: g.snakedir = NORTH; break;
				}
				break;
			case KEYBIND_TURN_RIGHT:
				switch (g.snakedir) {
				case NORTH: g.snakedir = EAST; break;
				case EAST: g.snakedir = SOUTH; break;
				case SOUTH: g.snakedir = WEST; break;
				case WEST: g.snakedir = NORTH; break;
				}
				break;
			case -1: break;
			default: continue;
			}
		} else {
			continue;
		}
		// Calculate next head position
		int headx = g.snake_head->x, heady = g.snake_head->y;
		int nextx, nexty;
		switch (g.snakedir) {
		case NORTH:
			nextx = headx;
			nexty = heady - 1;
			break;
		case SOUTH:
			nextx = headx;
			nexty = heady + 1;
			break;
		case EAST:
			nextx = headx + 1;
			nexty = heady;
			break;
		case WEST:
			nextx = headx - 1;
			nexty = heady;
			break;
		}
		// Loop around the play area
		if (nextx >= g.playw) {
			nextx = 0;
		} else if (nextx < 0) {
			nextx = g.playw - 1;
		} else if (nexty >= g.playh) {
			nexty = 0;
		} else if (nexty < 0) {
			nexty = g.playh - 1;
		}
		// Advance snake checking what it's moving onto
		uint32_t pix;
		ncvisual_at_yx(g.ncv, nexty, nextx, &pix);
		if (pix == g_color.empty || (nextx == g.snake_tail->x && nexty == g.snake_tail->y)) {
			// The snake chews on some air; nothing happens
			move_snake_head(nextx, nexty);
		} else if (pix == g_color.food) {
			// The snake eats food - elongate its tail!
			struct segment *seg = malloc(sizeof(struct segment));
			seg->next = g.snake_tail;
			g.snake_tail->prev = seg;
			g.snake_tail = seg;
			++g.snakelen;
			move_snake_head(nextx, nexty);
			--g.foodcount;
		} else if (pix == g_color.snake) {
			// The snake bites on its tail - game over!
			// edge case: if it's the tip of its tail, it slides next to it; covered above
			game_over();
			return;
		}
		// Maintain the food count
		if (g.foodcount < g.max_food) {
			add_random_food();
		}
		sleep_millis(FRAME_DELAY_MS);
	} while ((c = notcurses_getc_nblock(g.nc, &ni)) != KEYBIND_QUIT);
}

void
move_snake_head(int x, int y)
{
	if (g.snakelen > 1) {
		// Recycle the tail for it to become the head - saves malloc()s
		// (not that it matters for a snake game but it's always nice)
		struct segment *seg = g.snake_tail;
		ncvisual_set_yx(g.ncv, seg->y, seg->x, g_color.empty);
		g.snake_tail->next->prev = NULL;
		g.snake_tail = g.snake_tail->next;
		seg->prev = g.snake_head;
		g.snake_head->next = seg;
		g.snake_head = seg;
		seg->x = x;
		seg->y = y;
		ncvisual_set_yx(g.ncv, seg->y, seg->x, g_color.snake);
	} else {
		ncvisual_set_yx(g.ncv, g.snake_head->y, g.snake_head->x, g_color.empty);
		g.snake_head->x = x;
		g.snake_head->y = y;
		ncvisual_set_yx(g.ncv, g.snake_head->y, g.snake_head->x, g_color.snake);
	}
}

void
sleep_millis(long long millis)
{
	struct timespec tm = {
		.tv_sec = millis / 1000,
		.tv_nsec = millis * 1000000
	};
	while (nanosleep(&tm, &tm) < 0) {
		if (errno == EFAULT) {
			cleanup();
			exit(1);
		}
	}
}

int
main()
{
	init();
	main_loop();
	cleanup();
	return 0;
}
