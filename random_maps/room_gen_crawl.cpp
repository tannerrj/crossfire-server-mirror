/**
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 2013-2026 The Crossfire Development Team
 * Copyright (c) 1999-2013 Mark Wedel and the Crossfire Development Team
 * Copyright (c) 1992 Frank Tore Johansen
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

/**
 * @file
 * Crawl room generator
 *
 * Maze sprawls out and have many dead-ends.
 * Uses an algorithm that differs from the classic maze generator.
 * Loosely inspired by the dungeon layouts of Dragon Warrior Monsters 1.
 *
 */

#include <queue>

#include <stdlib.h>
#include <global.h>
#include <random_map.h>


#define MAP_CRAWL_NONE  0
#define MAP_CRAWL_WEST  1
#define MAP_CRAWL_EAST  2
#define MAP_CRAWL_NORTH 4
#define MAP_CRAWL_SOUTH 8
// Mask of all the directions
#define MAP_CRAWL_ALL  15

// Define to dump the layout of the internal paths determination, for debug purposes
//#define DEBUG_CRAWL


#ifdef DEBUG_CRAWL
/**
 * Function for helping to ensure parity between the compartmentalized
 * and final versions of the map.
 * Prints the layout of the internal paths determination.
 *
 * @param paths
 * The array of path data to display.
 *
 * @param x_eff
 * The effective number of hallways we could have in the x direction.
 *
 * @param y_eff
 * The effective number of hallways we could have in the y direction.
 */
static void print_debug_paths(uint8_t *paths, int x_eff, int y_eff) {
	for (int y = 0; y < y_eff; ++y) {
		for (int x = 0; x < x_eff; ++x) {
			switch (paths[x * y_eff + y]) {
				case MAP_CRAWL_WEST:
					printf("╸");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_EAST:
					printf("━");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_NORTH:
					printf("┛");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_SOUTH:
					printf("┓");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_EAST | MAP_CRAWL_NORTH:
					printf("┻");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_EAST | MAP_CRAWL_SOUTH:
					printf("┳");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_NORTH | MAP_CRAWL_SOUTH:
					printf("┫");
					break;
				case MAP_CRAWL_WEST | MAP_CRAWL_EAST | MAP_CRAWL_NORTH | MAP_CRAWL_SOUTH:
					printf("╋");
					break;
				case MAP_CRAWL_EAST:
					printf("╺");
					break;
				case MAP_CRAWL_EAST | MAP_CRAWL_NORTH:
					printf("┗");
					break;
				case MAP_CRAWL_EAST | MAP_CRAWL_SOUTH:
					printf("┏");
					break;
				case MAP_CRAWL_EAST | MAP_CRAWL_NORTH | MAP_CRAWL_SOUTH:
					printf("┣");
					break;
				case MAP_CRAWL_NORTH:
					printf("╹");
					break;
				case MAP_CRAWL_NORTH | MAP_CRAWL_SOUTH:
					printf("┃");
					break;
				case MAP_CRAWL_SOUTH:
					printf("╻");
					break;
				case MAP_CRAWL_NONE:
					printf(".");
					break;
				default:
					printf("?");
			}
		}
		printf("\n");
	}
}
#endif

/**
 * Generator of the crawl maze
 *
 * @param xsize
 * @param ysize
 * layout size. Unused fractions from wide hallways will be evenly distributed at the edges.
 * For size 1 hallways, odd numbers are best for use of space.
 * For size 2 hallways, one more than a multiple of 3 works best (43, 46, 49, etc)
 * For size 3 hallways, one more than a multiple of 4 works best (41, 45, 49, etc)
 *
 * @param iter
 * Recursion flag. If fewer than 1/5 of the map is filled,
 * we try again once. If it fails again, just return the second try.
 * Set to nonzero on the retry to denote it as such.
 *
 * @param hallway
 * Width of the hallway path inside the maze.
 * Should be 1-3 under most circumstances. Larger values require
 * large layouts to compensate.
 *
 * @return
 * Layout, as a two-dimensional array of characters.
 * Must be freed by the caller.
 */
char **map_gen_crawl(int xsize, int ysize, int iter, int hallway) {
	// Create a two-dimensional array of zero-filled characters.
	char **crawl = (char **)malloc(xsize * sizeof(char *));
	for (int i = 0; i < xsize; ++i) {
		crawl[i] = (char *)calloc(ysize, sizeof(char));
	}

	// If no hallway size is provided, select at random in range [1,3].
	if (hallway == 0) {
		hallway = (RANDOM() % 3) + 1;
	}

	// Determine the functional x and y sizes for maze design.
	// This is affected by the hallway size.
	// We remove one from the size for the unaccounted part of the outer wall.
	// Also handle the upper-left of walls in each block.
	int x_eff = (xsize - 1) / (hallway + 1),
	    y_eff = (ysize - 1) / (hallway + 1);

	// If x_eff or y_eff are zero, bail entirely.
	if (!x_eff || !y_eff) {
		// Crawl is a blank dungeon.
		// I don't think this code path will happen without a defined hallway width that is far too large.
		return crawl;
	}

	// Allocate an array of int8s to hold the flags of where the map goes.
	uint8_t *paths = (uint8_t *)calloc(xsize * ysize, sizeof(uint8_t));

	// Determine where we start spidering from.
	// Make sure to avoid starting at the edge if we have enough room
	int startx, starty;
	if (x_eff <= 2) {
		startx = RANDOM() % x_eff;
	}
	else {
		startx = (RANDOM() % (x_eff - 2)) + 1;
	}
	if (y_eff <= 2) {
		starty = RANDOM() % y_eff;
	}
	else {
		starty = (RANDOM() % (y_eff - 2)) + 1;
	}

	// Count the sections we create.
	int sections = 0;

	// Add the start position to the queue
	std::queue<int> queue;
	queue.push(startx * y_eff + starty);

	// Set a flag for marking the first entry.
	// We need it to guarantee generating something.
	int8_t first = 1;

	while (!queue.empty()) {
		// Dequeue the point info from the queue
		// Why is this two separate calls? Really STL?
		int loc = queue.front();
		queue.pop();
		// Massage the point info out of the int
		int at_x = loc / y_eff;
		int at_y = loc % y_eff;

		++sections;

		// Sanity check for the tile. If it is already filled, skip.
		if (paths[at_x * y_eff + at_y] != MAP_CRAWL_NONE) {
			continue;
		}

		// Paths we can take. Start by assuming all directions are clear.
		uint8_t avail_paths = MAP_CRAWL_ALL;

		// Paths we must take to complete open paths. Start by assuming none.
		uint8_t req_paths = MAP_CRAWL_NONE;

		// Determine what directions are available.
		// We cannot leave the bounds of the map,
		// Nor can we trample existing path definitions.
		if (at_x == 0) {
			avail_paths &= ~MAP_CRAWL_WEST;
		}
		else if (paths[(at_x - 1) * y_eff + at_y] != MAP_CRAWL_NONE) {
			// If non-blank tile has no path to here, exclude it.
			if ((paths[(at_x - 1) * y_eff + at_y] & MAP_CRAWL_EAST) == 0) {
				avail_paths &= ~MAP_CRAWL_WEST;
			}
			// Otherwise, require it.
			else {
				req_paths |= MAP_CRAWL_WEST;
			}
		}
		if (at_x == x_eff - 1) {
			avail_paths &= ~MAP_CRAWL_EAST;
		}
		else if (paths[(at_x + 1) * y_eff + at_y] != MAP_CRAWL_NONE) {
			// If non-blank tile has no path to here, exclude it.
			if ((paths[(at_x + 1) * y_eff + at_y] & MAP_CRAWL_WEST) == 0) {
				avail_paths &= ~MAP_CRAWL_EAST;
			}
			// Otherwise, require it.
			else {
				req_paths |= MAP_CRAWL_EAST;
			}
		}
		if (at_y == 0) {
			avail_paths &= ~MAP_CRAWL_NORTH;
		}
		else if (paths[at_x * y_eff + at_y - 1] != MAP_CRAWL_NONE) {
			// If non-blank tile has no path to here, exclude it.
			if ((paths[at_x * y_eff + at_y - 1] & MAP_CRAWL_SOUTH) == 0) {
				avail_paths &= ~MAP_CRAWL_NORTH;
			}
			// Otherwise, require it.
			else {
				req_paths |= MAP_CRAWL_NORTH;
			}
		}
		if (at_y == y_eff - 1) {
			avail_paths &= ~MAP_CRAWL_SOUTH;
		}
		else if (paths[at_x * y_eff + at_y + 1] != MAP_CRAWL_NONE) {
			// If non-blank tile has no path to here, exclude it.
			if ((paths[at_x * y_eff + at_y + 1] & MAP_CRAWL_NORTH) == 0) {
				avail_paths &= ~MAP_CRAWL_SOUTH;
			}
			// Otherwise, require it.
			else {
				req_paths |= MAP_CRAWL_SOUTH;
			}
		}

		uint8_t path;
		// Make sure the first try of the attmept always generates at least one path.
		// Otherwise it is possible (albeit rare, 1/16 in each generation) to have an all-wall result.
		do {
			path = (RANDOM() & avail_paths) | req_paths;
		} while (first && !path);

		// Mask out the unvailable paths and required paths, and randomly generate remaining connections.
		paths[at_x * y_eff + at_y] = path;
		first = 0;

		// Now we enqueue any new paths into the queue
		// req_paths happens to tell us where some adjacent stuff is,
		// and checking them is cheap and easy.
		// If not from a required connection, we add to the queue.
		// We can assert there is a filled space when it was required, so we
		// don't need to process that tile again.
		if (paths[at_x * y_eff + at_y] & MAP_CRAWL_WEST & ~req_paths) {
			queue.push((at_x - 1) * y_eff + at_y);
		}
		if (paths[at_x * y_eff + at_y] & MAP_CRAWL_EAST & ~req_paths) {
			queue.push((at_x + 1) * y_eff + at_y);
		}
		if (paths[at_x * y_eff + at_y] & MAP_CRAWL_NORTH & ~req_paths) {
			queue.push(at_x * y_eff + at_y - 1);
		}
		if (paths[at_x * y_eff + at_y] & MAP_CRAWL_SOUTH & ~req_paths) {
			queue.push(at_x * y_eff + at_y + 1);
		}

	}

	// Sanity check on our sections.
	if (iter == 0 && sections < x_eff * y_eff / 5) {
		// Clean up this run and try again.
		for (int i = 0; i < xsize; ++i) {
			free(crawl[i]);
		}
		free(crawl);
		free(paths);
		// Single try at recursion.
		return map_gen_crawl(xsize, ysize, 1, hallway);
	}

#ifdef DEBUG_CRAWL
	print_debug_paths(paths, x_eff, y_eff);
#endif

	// Translate our generation onto the actual random map.

	// First, figure out the offset to center our generatable space
	// in the center of the map size
	int offset_x = (xsize - (hallway + 1) * x_eff - 1) / 2,
	    offset_y = (ysize - (hallway + 1) * y_eff - 1) / 2;

	// Fill in unused areas with wall
	// Left side
	for (int i = 0; i < offset_x; ++i) {
		for (int j = 0; j < ysize; ++j) {
			crawl[i][j] = '#';
		}
	}
	// Top side
	for (int j = 0; j < offset_y; ++j) {
		for (int i = 0; i < xsize; ++i) {
			crawl[i][j] = '#';
		}
	}
	// Right side
	for (int i = offset_x + (hallway + 1) * x_eff + 1; i < xsize; ++i) {
		for (int j = 0; j < ysize; ++j) {
			crawl[i][j] = '#';
		}
	}
	// Bottom side
	for (int j = offset_y + (hallway + 1) * y_eff + 1; j < ysize; ++j) {
		for (int i = 0; i < xsize; ++i) {
			crawl[i][j] = '#';
		}
	}

	// Handle the used area.
	for (int x = 0; x < x_eff; ++x) {
		for (int y = 0; y < y_eff; ++y) {
			// We affect hallway + 2 tiles in each direction.
			// But we shift hallway + 1 tiles for the next tile,
			// which will cause some repeated wall constructions,
			// but will prevent a mess of checks in this section.

			uint8_t cur_path = paths[x * y_eff + y];

			// If no paths, fill with walls
			if ((cur_path & MAP_CRAWL_ALL) == 0) {
				for (int tmpx = 0; tmpx < hallway + 2; ++tmpx) {
					for (int tmpy = 0; tmpy < hallway + 2; ++tmpy) {
						crawl[x * (hallway + 1) + offset_x + tmpx][y * (hallway + 1) + offset_y + tmpy] = '#';
					}
				}
			}
			// Otherwise, draw the necessary walls
			else {
				// If north is blocked, write walls
				if ((cur_path & MAP_CRAWL_NORTH) == 0) {
					for (int tmp = 0; tmp < hallway + 2; ++tmp) {
						crawl[x * (hallway + 1) + offset_x + tmp][y * (hallway + 1) + offset_y] = '#';
					}
				}
				// Do the same for each other direction.
				if ((cur_path & MAP_CRAWL_SOUTH) == 0) {
					for (int tmp = 0; tmp < hallway + 2; ++tmp) {
						crawl[x * (hallway + 1) + offset_x + tmp][(y + 1) * (hallway + 1) + offset_y] = '#';
					}
				}
				if ((cur_path & MAP_CRAWL_WEST) == 0) {
					for (int tmp = 0; tmp < hallway + 2; ++tmp) {
						crawl[x * (hallway + 1) + offset_x][y * (hallway + 1) + offset_y + tmp] = '#';
					}
				}
				if ((cur_path & MAP_CRAWL_EAST) == 0) {
					for (int tmp = 0; tmp < hallway + 2; ++tmp) {
						crawl[(x + 1) * (hallway + 1) + offset_x][y * (hallway + 1) + offset_y + tmp] = '#';
					}
				}
			}
		}
	}

	// Cleanup
	free(paths);

	return crawl;
}
