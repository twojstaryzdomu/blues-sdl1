/* Copyright (C) 2022 twojstaryzdomu <twojstaryzdomu@users.noreply.github.com>
*/
#include <SDL.h>

char *GetPixelAddress(SDL_Surface *surface, int x, int y) {
	return (char*)surface->pixels
	+ x * surface->format->BytesPerPixel
	+ y *surface->pitch;
}

void PrintSurface(SDL_Surface *surface) {
	char *l;
	for(int y = 0; y < surface->h; y++) {
		for(int x = 0; x < surface->w; x++) {
			l = GetPixelAddress( surface, x, y );
			fprintf(stdout, "%3s ", l);
		}
		fprintf(stdout, "\n");
	}
}

SDL_Surface *FlipSurface(SDL_Surface *surface) {
	char *l, *t;
	int s = surface->format->BytesPerPixel;
	char *buf = (char *)calloc(1, s);
	SDL_LockSurface(surface);
	for(int y = 0; y < surface->h; y++) {
		for(int x = 0; x < surface->w / 2; x++) {
		l = GetPixelAddress(surface, x, y );
		t = GetPixelAddress(surface, surface->w - x - 1, y );
		memcpy(buf, t, s);
		memcpy(t, l, s);
		memcpy(l, buf, s);
		}
	}
	SDL_UnlockSurface(surface);
	return surface;
}

SDL_Surface *FlipSurfaceRect(SDL_Surface *surface, int abs_x, int abs_y, int offset_w, int offset_h) {
	char *l, *t;
	int s = surface->format->BytesPerPixel;
	char *buf = (char *)calloc(1, s);
	offset_h = offset_h > 1 ? offset_h : surface->h;
	offset_w = offset_w > 1 ? offset_w : surface->w;
	SDL_LockSurface(surface);
	for(int y = 0; y < offset_h; y++) {
		for(int x = 0; x < offset_w / 2; x++) {
		l = GetPixelAddress(surface, x + abs_x, y + abs_y);
		t = GetPixelAddress(surface, abs_x + offset_w - x - 1, y + abs_y);
		memcpy(buf, t, s);
		memcpy(t, l, s);
		memcpy(l, buf, s);
		}
	}
	SDL_UnlockSurface(surface);
	return surface;
}
