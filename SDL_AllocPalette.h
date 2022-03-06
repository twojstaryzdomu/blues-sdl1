/* Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>,
                      2022 twojstaryzdomu <twojstaryzdomu@users.noreply.github.com>
*/

#include <SDL.h>
#include <strings.h>

SDL_Palette * SDL_AllocPalette(int ncolors) {
	SDL_Palette *palette;

	/* Input validation */
	if (ncolors < 1) {
		fprintf(stderr, "invalid ncolors %d", ncolors);
		return NULL;
	}

	palette = (SDL_Palette *) SDL_malloc(sizeof(*palette));
	if (!palette) {
		SDL_OutOfMemory();
		return NULL;
	}
	palette->colors =
		(SDL_Color *) SDL_malloc(ncolors * sizeof(*palette->colors));
	if (!palette->colors) {
		SDL_free(palette);
		return NULL;
	}
	palette->ncolors = ncolors;

	SDL_memset(palette->colors, 0xFF, ncolors * sizeof(*palette->colors));

	return palette;
}

