struct pixel_t {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct pixel_t *_pixel = NULL;

struct pixel_t *SDL_SetRenderDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (!_pixel) {
		if (!(_pixel = SDL_malloc(sizeof(struct pixel_t)))) {
			SDL_OutOfMemory();
			return NULL;
		}
	}
	_pixel->r = r;
	_pixel->b = b;
	_pixel->g = g;
	_pixel->a = a;
	return _pixel;
}

void SDL_RenderDrawPixel(SDL_Surface* screen, int x, int y) {
	uint32_t* p_screen = (uint32_t*)screen->pixels;
	p_screen += y * screen->w + x;
	*p_screen = SDL_MapRGBA(screen->format, _pixel->r, _pixel->g, _pixel->b, _pixel->a);
}
