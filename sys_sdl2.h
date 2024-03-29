
#include <SDL.h>
#include "sys.h"
#include "SDL_surface.h"
#include "SDL_AllocPalette.h"
#include "SDL_Render.h"
#include "sys_sine.h"
#include "util.h"

#define COPPER_BARS_H 80
#define MAX_SPRITES 512
#define MAX_SPRITESHEETS 3

static const int FADE_STEPS = 16;

struct spritesheet_t {
	int count;
	int scale;
	SDL_Rect *r;
	SDL_Surface *surface;
	SDL_Surface *texture;
};

static struct spritesheet_t _spritesheets[MAX_SPRITESHEETS];

struct sprite_t {
	int sheet;
	int num;
	int x, y;
	bool xflip;
	bool centred;
};

static struct sprite_t _sprites[MAX_SPRITES];
static int _sprites_count;
static SDL_Rect _sprites_cliprect;

struct slide_t {
	SDL_Rect *b;
	SDL_Rect *br;
	SDL_Rect *c;
	SDL_Rect *f;
	int16_t *pos;
	int end;
	int last_pos;
	int step;
};

static struct slide_t _slide;

static int _window_w, _window_h;
#if defined(HAVE_X11)
static int _fullscreen_w, _fullscreen_h;
#endif
static int _shake_dx, _shake_dy;
static int _centred_x_offset, _centred_y_offset;
static int _palette_x_offset, _palette_y_offset;
static uint32_t flags, rmask, gmask, bmask, amask;
static SDL_Surface *_renderer;
static SDL_Surface *_texture;
static SDL_Surface *_flipped_cache[MAX_SPRITESHEETS][MAX_SPRITES];
static SDL_PixelFormat *_fmt;
static SDL_Palette *_palette;
static uint32_t _screen_palette[256];
static uint32_t *_screen_buffer;
static int _copper_color_key;
static uint32_t _copper_palette[COPPER_BARS_H];
static const char *_caption;
static int8_t _scale;
static bool _fullscreen;

static int _orig_w, _orig_h;
static int8_t _orig_scale;
static bool _size_lock;
static bool _orig_fullscreen;
static bool _orig_color;
static bool _print_palette;

static SDL_Joystick *_joystick;
static bool _joystick_up_setup;

#if defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
static void x11_set_fullscreen_size(int *w, int *h) {
	Display *display = XOpenDisplay(NULL);
	XRRScreenResources *screens = XRRGetScreenResources(display, DefaultRootWindow(display));
	XRRCrtcInfo *info = NULL;
	for (int i = 0; i < screens->ncrtc; i++) {
		info = XRRGetCrtcInfo(display, screens, screens->crtcs[i]);
		if (*w < info->width) {
			*w = info->width;
			*h = info->height;
		}
		XRRFreeCrtcInfo(info);
	}
	XRRFreeScreenResources(screens);
}
#endif

static int16_t _sine_index;
static int _sine_direction;
static bool _sine_plot;
static int8_t _sine_offset_y;
static uint16_t _sine_scale_x;
static uint16_t _sine_scale_y, _orig_sine_scale_y;

static int sdl2_init() {
	print_debug(DBG_SYSTEM, "Byte order is %s endian", SDL_BYTEORDER == SDL_BIG_ENDIAN ? "big" : "little");
	/* SDL interprets each pixel as a 32-bit number, so our masks must depend
	on the endianness (byte order) of the machine */
	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		rmask = 0xff000000;
		gmask = 0x00ff0000;
		bmask = 0x0000ff00;
		amask = 0x000000ff;
	#else
		// rmask & gmask had to be reversed to fix colours
		rmask = 0x00ff0000;
		bmask = 0x000000ff;
		gmask = 0x0000ff00;
		amask = 0xff000000;
	#endif
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | (g_sys.audio ? SDL_INIT_AUDIO : 0));
	SDL_ShowCursor(SDL_DISABLE);
	g_sys.w = g_sys.h = 0;
	memset(_screen_palette, 0, sizeof(_screen_palette));
	_palette = SDL_AllocPalette(256);
	_screen_buffer = 0;
	_copper_color_key = -1;
	g_sys.input.raw = false;
	const int count = SDL_NumJoysticks();
	if (count > 0) {
		for (int i = 0; i < count; ++i) {
			_joystick = SDL_JoystickOpen(i);
			if (_joystick) {
				fprintf(stdout, "Using joystick '%s'\n", SDL_JoystickName(i));
				fprintf(stdout, "Jump button: %d\n", g_sys.input.jump_button);
			}
		}
	}
	#if defined(HAVE_X11)
		x11_set_fullscreen_size(&_fullscreen_w, &_fullscreen_h);
		fprintf(stderr, "Fullscreen size: %dx%d\n", _fullscreen_w, _fullscreen_h);
	#endif
	return 0;
}

static void sdl2_fini() {
	if (_fullscreen && _renderer->flags & SDL_FULLSCREEN) {
		SDL_WM_ToggleFullScreen(_renderer);
	}
	if (_texture) {
		SDL_FreeSurface(_texture);
		_texture = 0;
	}
	if (_renderer) {
		SDL_FreeSurface(_renderer);
		_renderer = 0;
	}
	free(_screen_buffer);
	if (_joystick) {
		SDL_JoystickClose(_joystick);
		_joystick = 0;
	}
	if (_pixel) {
		SDL_free(_pixel);
		_pixel = 0;
	}
	SDL_Quit();
}

static void sdl2_set_screen_size(int w, int h, const char *caption, int scale, bool fullscreen, bool hybrid_color) {
	_window_w = w;
	_window_h = h;
	_caption = caption;
	_fullscreen = fullscreen;
	const int screen_w = MAX(w / scale, ORIG_W);
	const int screen_h = MAX(h / scale, ORIG_H);
	if (screen_w * scale <= _window_w && screen_h * scale <= _window_h) {
		_scale = MAX(scale, 1);
	} else {
		_scale = MAX(_scale - 1, 1);
		print_warning("Unable to fit %dx scaled %dx%d screen within %dx%d window bounds", scale, screen_w, screen_h, _window_w, _window_h, _scale);
		g_message.add("Unable to scale to %d", scale);
		return; // refuse to resize when not possible to fit window within bounds
	}
	if (!_size_lock) {
		g_sys.w = screen_w;
		g_sys.h = screen_h;
	}
	_centred_x_offset = (g_sys.w - ORIG_W) / 2 * _scale;
	_centred_y_offset = (g_sys.h - ORIG_H) / 2 * _scale;
	if (!_orig_w) {
		_orig_w = screen_w;
		_orig_h = screen_h;
		_orig_scale = scale;
		_orig_fullscreen = fullscreen;
		_orig_color = hybrid_color;
		fprintf(stderr, "Original window size: %dx%d, scale %d, fullscreen: %s, hybrid colour: %s\n", _orig_w, _orig_h, _orig_scale, _orig_fullscreen ? "on" : "off", _orig_color ? "on" : "off");
	}
	flags = fullscreen ? SDL_FULLSCREEN : 0;
	flags |= SDL_HWPALETTE;
	flags |= SDL_RESIZABLE;
	flags |= SDL_DOUBLEBUF;
	flags |= SDL_ASYNCBLIT;
	flags |= SDL_ANYFORMAT;
	if (_renderer)
		SDL_FreeSurface(_renderer);
	_renderer = SDL_SetVideoMode(_window_w, _window_h, 32, flags);
	if(!_renderer){
		printf("Couldn't set video mode: %s\n", SDL_GetError());
		exit(-1);
	}
	SDL_WM_SetCaption(caption, 0);
	fprintf(stderr, "Size: %dx%d", g_sys.w, g_sys.h);
	if (g_sys.w != _window_w && g_sys.h != _window_h)
		fprintf(stderr, " Window: %dx%d", _window_w, _window_h);
	fprintf(stderr, "\n");
	print_debug(DBG_SYSTEM, "set_screen_size %d,%d", g_sys.w, g_sys.h);
	if (_screen_buffer)
		free(_screen_buffer);
	_screen_buffer = (uint32_t *)calloc(g_sys.w * _scale * g_sys.h * _scale, sizeof(uint32_t));
	if (!_screen_buffer) {
		print_error("Failed to allocate screen buffer");
	}
	if (_texture) {
		SDL_FreeSurface(_texture);
	}
	_texture = SDL_DisplayFormatAlpha(_renderer);
	_fmt = _renderer->format;
	/* Check the bitdepth of the surface */
	if(_fmt->BitsPerPixel!=8){
		print_debug(DBG_SYSTEM, "Not an 8-bit surface.");
	}
	_sprites_cliprect.x = 0;
	_sprites_cliprect.y = 0;
	_sprites_cliprect.w = g_sys.w * _scale;
	_sprites_cliprect.h = g_sys.h * _scale;
	_sine_index = 0;
	_sine_direction = 1;
	_sine_offset_y = ORIG_H / 2 - sine_tbl[0];
	_sine_scale_x = 2;
	_sine_scale_y = 10;
	_orig_sine_scale_y = _sine_scale_y;
	g_sys.hybrid_color = hybrid_color;
}

static uint32_t convert_amiga_color(uint16_t color) {
	uint8_t r = (color >> 8) & 15;
	r |= r << 4;
	uint8_t g = (color >> 4) & 15;
	g |= g << 4;
	uint8_t b =  color       & 15;
	b |= b << 4;
	return SDL_MapRGB(_fmt, r, g, b);
}

static void set_amiga_color(uint16_t color, SDL_Color *p) {
	const uint8_t r = (color >> 8) & 15;
	p->r = (r << 4) | r;
	const uint8_t g = (color >> 4) & 15;
	p->g = (g << 4) | g;
	const uint8_t b =  color       & 15;
	p->b = (b << 4) | b;
}

static void sdl2_set_palette_amiga(const uint16_t *colors, int offset) {
	SDL_Color *palette_colors = &_palette->colors[offset];
	for (int i = 0; i < 16; ++i) {
		_screen_palette[offset + i] = convert_amiga_color(colors[i]);
		set_amiga_color(colors[i], &palette_colors[i]);
	}
}

static void sdl2_set_copper_bars(const uint16_t *data) {
	if (!data) {
		_copper_color_key = -1;
	} else {
		_copper_color_key = (data[0] - 0x180) / 2;
		const uint16_t *src = data + 1;
		uint32_t *dst = _copper_palette;
		for (int i = 0; i < COPPER_BARS_H / 5; ++i) {
			const int j = i + 1;
			*dst++ = convert_amiga_color(src[j]);
			*dst++ = convert_amiga_color(src[i]);
			*dst++ = convert_amiga_color(src[j]);
			*dst++ = convert_amiga_color(src[i]);
			*dst++ = convert_amiga_color(src[j]);
		}
	}
}

static uint8_t *sdl2_rescale_pixels(const uint8_t *p, int w, int h, int pitch, uint8_t scale) {
	print_debug(DBG_SYSTEM, "sdl2_rescale: rescaling to %d w = %d, h = %d, pitch = %d", scale, w, h, pitch);
	int buffer_size = w * scale * h * scale;
	uint8_t *buffer = malloc(buffer_size * sizeof(uint8_t));
	if (scale == 1)
		return memcpy(buffer, p, w * h * sizeof(uint8_t));
	for (int i = 0; i < w * scale * h * scale; ++i) {
		uint8_t x_scale = scale;
		uint8_t y_scale = scale;
		int x = i % (pitch * scale);
		int y = i / (pitch * scale);
		int index	= (scale > 1)
				? x / x_scale + y / y_scale * w
				: i;
		if (index < buffer_size) {
			buffer[i] = p[index];
		} else {
			print_debug(DBG_SYSTEM, "sdl2_rescale: index %d exceeded buffer size %d for w = %d, h = %d, pitch = %d", index, i, scale, w, h, pitch);
		}
	}
	return buffer;
}

static SDL_Surface *sdl2_rescale_surface(SDL_Surface *surface, int scale) {
	SDL_PixelFormat *fmt = surface->format;
	uint8_t *pixels = sdl2_rescale_pixels(surface->pixels, surface->w, surface->h, surface->pitch, scale);
	SDL_Surface *temp = SDL_CreateRGBSurfaceFrom(pixels, surface->w * scale, surface->h * scale, fmt->BitsPerPixel * fmt->BytesPerPixel, surface->w * scale, fmt->Rmask, fmt->Bmask, fmt->Gmask, fmt->Amask);
	SDL_SetColors(temp, fmt->palette->colors, 0, fmt->palette->ncolors);
	SDL_SetColorKey(temp, surface->flags, fmt->colorkey);
	SDL_Surface *rescaled = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);
	free(pixels);
	return rescaled;
}

static void sdl2_rescale_spritesheets(int scale) {
	for (int i = 0; i < ARRAYSIZE(_spritesheets); ++i) {
		struct spritesheet_t *sheet = &_spritesheets[i];
		if (sheet->scale == scale) {
			print_debug(DBG_SYSTEM, "Spritesheet %d already at scale %d", i, scale);
			continue;
		}
		print_debug(DBG_SYSTEM, "Scaling spritesheet %d by %d", i, scale);
		if (sheet->surface) {
			if (sheet->texture)
				SDL_FreeSurface(sheet->texture);
			sheet->texture = sdl2_rescale_surface(sheet->surface, scale);
			sheet->scale = _scale;
		} else {
			print_debug(DBG_SYSTEM, "No surface found for rescaling in spritesheet %d", i);
		}
		for (int s = 0; s < MAX_SPRITES; ++s) {
			if (_flipped_cache[i][s]) {
				print_debug(DBG_SYSTEM, "Freeing %d", s);
				SDL_FreeSurface(_flipped_cache[i][s]);
				_flipped_cache[i][s] = NULL;
			}
		}
	}
}

static void sdl2_set_screen_palette(const uint8_t *colors, int offset, int count, int depth) {
	SDL_Color *palette_colors = &_palette->colors[offset];
	const int shift = 8 - depth;
	for (int i = 0; i < count; ++i) {
		int r = colors[0];
		int g = colors[1];
		int b = colors[2];
		if (depth != 8) {
			r = (r << shift) | (r >> (depth - shift));
			g = (g << shift) | (g >> (depth - shift));
			b = (b << shift) | (b >> (depth - shift));
		}
		_screen_palette[offset + i] = SDL_MapRGB(_fmt, r, g, b);
		palette_colors[i].r = r;
		palette_colors[i].g = g;
		palette_colors[i].b = b;
		colors += 3;
	}
	for (int i = 0; i < ARRAYSIZE(_spritesheets); ++i) {
		print_debug(DBG_SYSTEM, "Setting palette for spritesheet %d", i);
		struct spritesheet_t *sheet = &_spritesheets[i];
		if (sheet->surface) {
			SDL_SetColors(sheet->surface, _palette->colors, 0, 256);
			if (sheet->texture)
				SDL_FreeSurface(sheet->texture);
			sheet->texture 	= (_scale > 1)
					? sdl2_rescale_surface(sheet->surface, _scale)
					: SDL_DisplayFormatAlpha(sheet->surface);
			sheet->scale = _scale;
		} else {
			print_debug(DBG_SYSTEM, "No surface found for palette %d", i);
		}
		for (int s = 0; s < MAX_SPRITES; ++s) {
			if (_flipped_cache[i][s]) {
				print_debug(DBG_SYSTEM, "Freeing %d", s);
				SDL_FreeSurface(_flipped_cache[i][s]);
				_flipped_cache[i][s] = NULL;
			}
		}
	}
}

static void sdl2_set_palette_color(int i, const uint8_t *colors) {
	int r = colors[0];
	r = (r << 2) | (r >> 4);
	int g = colors[1];
	g = (g << 2) | (g >> 4);
	int b = colors[2];
	b = (b << 2) | (b >> 4);
	_screen_palette[i] = SDL_MapRGB(_fmt, r, g, b);
}

static void sdl2_update_sprites_screen() {
	if (!g_sys.centred) {
		SDL_SetClipRect(_renderer, !_slide.c? &_sprites_cliprect : _slide.c);
	} else {
		SDL_Rect c = { .x = _centred_x_offset, .y = _centred_y_offset, .w = ORIG_W * _scale, .h = ORIG_H * _scale };
		SDL_SetClipRect(_renderer, &c);
	}
	for (int i = 0; i < _sprites_count; ++i) {
		const struct sprite_t *spr = &_sprites[i];
		struct spritesheet_t *sheet = &_spritesheets[spr->sheet];
		if (spr->num >= sheet->count) {
			continue;
		}
		if (g_sys.paused)
			if (spr->num < 241 || spr->num > 241 + 32)
				continue;
		SDL_Rect r;
		r.x = spr->x + _shake_dx;
		r.y = spr->y + _shake_dy;
		r.w = sheet->r[spr->num].w;
		r.h = sheet->r[spr->num].h;
		if (_scale > 1) {
			r.y *= _scale;
			r.x *= _scale;
			r.w *= _scale;
			r.h *= _scale;
		}
		if (_slide.br) {
			r.x += _slide.br->x - _centred_x_offset;
			r.y += _slide.br->y - _centred_y_offset;
		}
		if (spr->centred) {
			r.x += _centred_x_offset;
			r.y += _centred_y_offset;
		}
		SDL_Rect t;
		t.x = sheet->r[spr->num].x * _scale;
		t.w = sheet->r[spr->num].w * _scale;
		t.h = sheet->r[spr->num].h * _scale;
		t.y = sheet->r[spr->num].y * _scale;
		print_debug(DBG_SYSTEM, "rendering spr %d at %d,%d-%d,%d,size=%d,%d", spr->num, t.x, t.y, t.x + t.w, t.y + t.h, t.w, t.h);
		if (spr->xflip) {
			if (_flipped_cache[spr->sheet][spr->num]) {
				print_debug(DBG_SYSTEM, "Cache hit for sprite %d from sheet %d", spr->num, spr->sheet);
			} else {
				if (t.w && t.h) { // only sprites with w & h
					print_debug(DBG_SYSTEM, "Rendering sprite %d from sheet %d", spr->num, spr->sheet);
					_flipped_cache[spr->sheet][spr->num] = CopyRectFlipped(sheet->texture, &t);
				}
			}
			SDL_BlitSurface(_flipped_cache[spr->sheet][spr->num], 0, _renderer, &r);
		} else {
			SDL_BlitSurface(sheet->texture, &t, _renderer, &r);
		}
	}
	SDL_SetClipRect(_renderer, 0);
}

static void fade_palette_helper(int in) {
	int component = 0;
	SDL_Surface* surface = SDL_CreateRGBSurface(0, g_sys.w * _scale, g_sys.h * _scale, 32, rmask, gmask, bmask, amask);
	for (int i = 0; i <= FADE_STEPS; ++i) {
		int alpha = 255 * i / FADE_STEPS;
		if (in) {
			alpha = 255 - alpha;
		}
		Uint32 color = SDL_MapRGBA(surface->format, component, component, component, alpha);
		SDL_FillRect(surface, 0, color);
		SDL_BlitSurface(_texture, 0, _renderer, 0);
		sdl2_update_sprites_screen();
		if (_slide.f) {
			SDL_BlitSurface(_texture, _slide.f, _renderer, _slide.f);
		}
		SDL_BlitSurface(surface, 0, _renderer, 0);
		SDL_Flip(_renderer);
		SDL_Delay(30);
	}
	g_sys.render_clear_sprites();
	SDL_FreeSurface(surface);
}

static void sdl2_fade_in_palette() {
	if (!g_sys.input.quit) {
		fade_palette_helper(1);
	}
}

static void sdl2_fade_out_palette() {
	if (!g_sys.input.quit) {
		fade_palette_helper(0);
	}
}

static void sdl2_transition_screen(struct sys_rect_t *s, enum sys_transition_e type, bool open) {
	s->w *= _scale;
	s->h *= _scale;
	const int step_w = s->w / (FADE_STEPS + 1);
	const int step_h = s->h / (FADE_STEPS + 1) * s->w / s->h;
	print_debug(DBG_SYSTEM, "sdl2_transition_screen: FADE_STEPS = %d; s->w = %d, s->h = %d, step_w = %d; step_h = %d", FADE_STEPS, s->w, s->h, step_w, step_h);
	SDL_Rect b = { .x = 0, .y = 0, .h = s->h, .w = s->w };
	SDL_Rect r;
	r.x = 0;
	r.w = 0;
	r.y = 0;
	int step = 0;
	r.h = (type == TRANSITION_CURTAIN) ? s->h : 0;
	do {
		if (open) {
			r.w += step_w;
			r.x = (s->w - r.w) / 2;
		} else {
			r.w = s->w;
			r.y += step_h / 2 + step_h % 2;
			r.h -= MIN(step_h, r.h);
		}
		SDL_FillRect(_renderer, &b, 0);
		if (type == TRANSITION_SQUARE) {
			r.y = (s->h - r.h) / 2;
			if (r.y < 0) {
				r.y = 0;
			}
			r.h += step_h;
			if (r.y + r.h > s->h) {
				r.h = s->h - r.y;
			}
		}
		step += 1;
		print_debug(DBG_SYSTEM, "sdl2_transition_screen: %d,%d-%d,%d, r.w = %d, r.h = %d, open = %d, step = %d", r.x, r.y, r.x + r.w, r.y + r.h, r.w, r.h, open, step);
		SDL_BlitSurface(_texture, &r, _renderer, &r);
		SDL_Flip(_renderer);
		SDL_Delay(30);
	} while (((r.x > r.x % step_w && open) || (r.y < s->h / 2 && !open)) && (type == TRANSITION_CURTAIN || r.y > r.y % step_h));
}

static SDL_Rect *set_rect(int x, int y, int w, int h){
	SDL_Rect *r = malloc(sizeof(SDL_Rect));
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
	return r;
}

static void init_slide(){
	if (!_slide.br) {
		enum sys_slide_e type	= g_sys.slide_type;
		struct sys_rect_t *s	= &g_sys.slide_rect;
		SDL_Rect *f		= set_rect(0, s->y ? s->y : g_sys.h * _scale - s->h * _scale, g_sys.w * _scale, s->h * _scale);
		SDL_Rect *b		= set_rect(0, 0, ORIG_W * _scale, ORIG_H * _scale);
		SDL_Rect *br		= set_rect(b->x, b->y, b->w, b->h);
		switch (type) {
		case SLIDE_BOTTOM:
			br->y		= g_sys.h * _scale;
			_slide.end	= g_sys.slide_end
					? g_sys.slide_end
					: b->y > _centred_y_offset
					? f->y + f->h
					: _centred_y_offset;
			_slide.pos	= &(br->y);
			*_slide.pos	= MAX(0, *_slide.pos - _slide.last_pos);
			_slide.step	= -1;
			if (!g_sys.centred) {
				br->x	+= _centred_x_offset;
			}
		}
		SDL_Rect *c		= set_rect(_centred_x_offset, 0, ORIG_W * _scale, f->y * _scale);
		_slide.b		= b;
		_slide.br		= br;
		_slide.c		= c;
		_slide.f		= f;
	}
}

static void clear_slide(){
	g_sys.slide_type = 0;
	free(_slide.b);
	free(_slide.br);
	free(_slide.c);
	free(_slide.f);
	_slide.b = 0;
	_slide.br = 0;
	_slide.c = 0;
	_slide.f = 0;
	_slide.last_pos = 0;
}

static void update_slide() {
	if (g_sys.slide_type) {
		if (*_slide.pos != _slide.end) {
			*_slide.pos += _slide.step;
			++_slide.last_pos;
		}
	}
}

static void reinit_slide() {
	if (g_sys.slide_type) {
		uint8_t slide_type = g_sys.slide_type;
		uint8_t last_pos = _slide.last_pos;
		clear_slide();
		g_sys.slide_type = slide_type;
		_slide.last_pos = last_pos;
		init_slide();
	}
}

static void sdl2_resize_screen() {
	sdl2_set_screen_size(_window_w, _window_h, _caption, _scale, _fullscreen, g_sys.hybrid_color);
	sdl2_rescale_spritesheets(_scale);
	reinit_slide();
}

static void sdl2_rescale_screen(int n) {
	if (_scale > 1 || n > 0) {
		fprintf(stderr, "Scale %d, ", _scale);
		_scale += n;
		fprintf(stderr, "%screasing to %d\n", n > 0 ? "in" : "de", abs(_scale));
		g_sys.resize = true;
		g_sys.resize_screen();
	}
}

static void sdl2_print_palette() {
	if (!_print_palette)
		return;
	int x_scale = 2 * _scale;
	int y_scale = 2 * _scale;
	int x_ncolors = 16;
	int y_ncolors = 16;
	for (int i = 0; i < g_sys.w * _scale * g_sys.h * _scale; ++i) {
		/*
		 *  Breakdown:
		 *  p = pitch = g_sys.w
		 *  i         -> (x,y) = index ; i+        -> (x,y) = index++
		 *  0         -> (0,0) =       ; 1         -> (1,0) =
		 *  0 + p * 1 -> (1,0) =       ; 1 + p * 1 -> (1,1) =
		 *  0 + p * 2 -> (2,0) =       ; 1 + p * 2 -> (2,1) =
		 *  |       |                    |       |
		 *  |       \--> i / p           |       \--> i / p
		 *  \----------> i % p           \----------> i % p
		 *
		 */
		int x = i % (g_sys.w * _scale);
		int y = i / (g_sys.w * _scale);
		if (x < x_ncolors * x_scale && y < y_ncolors * y_scale) {
			int index = x / x_scale + y / y_scale * x_ncolors;
			_screen_buffer[i + _palette_x_offset + _palette_y_offset * g_sys.w] = _screen_palette[index];
		}
	}
}

static void sdl2_sine_screen() {
	uint16_t sine_x = _sine_index * _scale;
	int16_t sine_y = ((sine_tbl[_sine_index]) + _sine_offset_y) * _sine_scale_y / 10 * _sine_scale_x * _scale;
	SDL_Rect s1 = { .x = _centred_x_offset * _scale + MAX(0, -sine_x), .y =  _centred_y_offset * _scale + MAX(0, -sine_y), .w = ORIG_W * _scale - abs(sine_x), .h = ORIG_H * _scale - abs(sine_y) };
	SDL_Rect d1 = { .x = _centred_x_offset * _scale + MAX(0, sine_x), .y = _centred_y_offset * _scale + MAX(0, sine_y), .w = s1.w, .h = s1.h };
	SDL_Rect s2 = { .x = sine_x > 0 ? s1.x + s1.w : _centred_x_offset * _scale, .y = sine_y > 0 ? s1.y + s1.h : _centred_y_offset * _scale, .w = ORIG_W * _scale - s1.w, .h = ORIG_H * _scale - s1.h };
	SDL_Rect d2 = { .x = _centred_x_offset * _scale, .y = sine_y < 0 ? d1.h + d1.y : _centred_y_offset * _scale, .w = sine_x, .h = s2.h };
	SDL_Rect s3 = { .x = s1.x, .y = sine_y > 0 ? s1.y + s1.h : _centred_y_offset * _scale, .w = s1.w, .h = ORIG_H * _scale - s1.h };
	SDL_Rect d3 = { .x = _centred_x_offset * _scale + MAX(0, sine_x), .y = d2.y, .w = d1.w, .h = ORIG_H * _scale - d1.h };
	SDL_Rect s4 = { .x = s1.x + s1.w, .y = s1.y, .w = ORIG_W * _scale - s1.w, .h = s1.h };
	SDL_Rect d4 = { .x = _centred_x_offset * _scale, .y = _centred_y_offset * _scale + MAX(0, sine_y), .w = sine_x, .h = d1.h };
	print_debug(DBG_SYSTEM, "Sine wave: %d, %d @ %d "
				"s1: %d,%d - %d,%d "
				"d1: %d,%d - %d,%d "
				"s1.h = %d; s1.y = %d; d1.h = %d; d1.y = %d; dir = %d",
				sine_x, sine_y, _sine_index,
				s1.x, s1.y, s1.w + s1.x, s1.h + s1.y,
				d1.x, d1.y, d1.w + d1.x, d1.h + d1.y,
				s1.h, s1.y, d1.h, d1.y, _sine_direction);
	SDL_BlitSurface(_texture, &s1, _renderer, &d1);
	SDL_BlitSurface(_texture, &s2, _renderer, &d2);
	SDL_BlitSurface(_texture, &s3, _renderer, &d3);
	SDL_BlitSurface(_texture, &s4, _renderer, &d4);
	if (_sine_plot) {
		if (!_pixel)
			_pixel = SDL_SetRenderDrawColor(255, 0, 0, 255);
		uint16_t x;
		int16_t sine_plot_y;
		for (x = 0; x <= _sine_index; x++) {
			sine_plot_y = ((sine_tbl[x]) + _sine_offset_y) * _sine_scale_y / 10 * _sine_scale_x * _scale;
			if (x > 0 && x < ORIG_W && sine_plot_y > 0 && sine_plot_y < ORIG_H * _scale)
				SDL_RenderDrawPixel(_renderer, x * _scale + _centred_x_offset, sine_plot_y + _centred_y_offset);
		}
		print_debug(DBG_SYSTEM, "Sine plot: %d, %d _sine_scale_x %d _sine_scale_y %d _sine_offset_y %d sine_tbl(x) %d",
					x, sine_plot_y, _sine_scale_x, _sine_scale_y / 10, _sine_offset_y, sine_tbl[x]);
	}
	_sine_index = (_sine_index + _sine_direction + ORIG_W) % ORIG_W;
}

static void sdl2_update_screen_cached(const uint8_t *p, int present, bool cache_redraw) {
	if (!cache_redraw || g_sys.resize) {
		uint8_t *r	= (_scale > 1)
				? sdl2_rescale_pixels(p, g_sys.w, g_sys.h, g_sys.w, _scale)
				: (uint8_t*)p;
		uint8_t *q	= r;
		if (_copper_color_key != -1) {
			for (int j = 0; j < g_sys.h * _scale; ++j) {
				if (j / 2 < COPPER_BARS_H) {
					const uint32_t line_color = _copper_palette[j / 2];
					for (int i = 0; i < g_sys.w * _scale; ++i) {
						_screen_buffer[j * g_sys.w * _scale + i]	= (r[i] == _copper_color_key)
												? line_color
												: _screen_palette[r[i]];
					}
				} else {
					for (int i = 0; i < g_sys.w * _scale; ++i) {
						_screen_buffer[j * g_sys.w * _scale + i] = _screen_palette[r[i]];
					}
				}
				r += g_sys.w * _scale;
			}
		} else {
			for (int i = 0; i < g_sys.w * _scale * g_sys.h * _scale; ++i) {
				_screen_buffer[i] = _screen_palette[r[i]];
			}
			sdl2_print_palette();
		}
		if (p != q)
			free(q);
		SDL_FreeSurface(_texture);
		_texture = SDL_ConvertSurface(SDL_CreateRGBSurfaceFrom(_screen_buffer, g_sys.w * _scale, g_sys.h * _scale, 32, g_sys.w * sizeof(uint32_t) * _scale, rmask, gmask, bmask, amask), _fmt, 0);
	}
	if (present) {
		SDL_Rect *src, *dst;
		SDL_Rect r={_shake_dx, _shake_dy, g_sys.w * _scale, g_sys.h * _scale};
		if (g_sys.slide_type) {
			init_slide();
			src = _slide.b;
			dst = _slide.br;
			SDL_SetClipRect(_renderer, _slide.c);
		} else {
			src = 0;
			dst = &r;
		}
		SDL_FillRect(_renderer, &r, 0);
		if (g_sys.sine) {
			sdl2_sine_screen();
		} else
			SDL_BlitSurface(_texture, src, _renderer, dst);
		sdl2_update_sprites_screen();
		if (_slide.f)
			SDL_BlitSurface(_texture, _slide.f, _renderer, _slide.f);
		SDL_Flip(_renderer);
		if (_slide.br)
			update_slide();
	}
}

static void sdl2_update_screen(const uint8_t *p, int present) {
	sdl2_update_screen_cached(p, present, false);
}

static void sdl2_shake_screen(int dx, int dy) {
	_shake_dx = dx;
	_shake_dy = dy;
}

static void handle_keyevent(const SDL_keysym *keysym, bool keydown, struct input_t *input, bool *paused) {
	uint8_t debug_channel;
	uint16_t debug_level;
	bool debug_enabled;
	if (g_sys.input.raw && keysym->sym >= '0' && keysym->sym < 'g') {
		if (keydown) {
			g_sys.input.hex = keysym->sym;
			return;
		}
	}
	switch (keysym->sym) {
	case SDLK_LEFT:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				--_palette_x_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_LEFT;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_LEFT;
		}
		break;
	case SDLK_RIGHT:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				++_palette_x_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_RIGHT;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_RIGHT;
		}
		break;
	case SDLK_UP:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				--_palette_y_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_UP;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		break;
	case SDLK_DOWN:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				++_palette_y_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_DOWN;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_DOWN;
		}
		break;
	case SDLK_RETURN:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				if (!_size_lock) {
					_fullscreen = !_fullscreen;
					#if defined(HAVE_X11)
						_window_w = _fullscreen ? _fullscreen_w : _orig_w;
						_window_h = _fullscreen ? _fullscreen_h : _orig_h;
					#else
						if (g_sys.w == ORIG_W && g_sys.h == ORIG_H) {
							_window_w = _fullscreen ? FULLSCREEN_W : _orig_w;
							_window_h = _fullscreen ? FULLSCREEN_H : _orig_h;
						}
					#endif
					g_sys.resize = true;
					g_sys.resize_screen();
				} else {
					SDL_WM_ToggleFullScreen(_renderer);
				}
				fprintf(stderr, "Toggling fullscreen %s\n", _fullscreen ? "on" : "off");
				_fullscreen = _renderer->flags & SDL_FULLSCREEN;
				print_debug(DBG_SYSTEM, "Fullscreen is %s", _fullscreen ? "on" : "off");
			default:
				break;
			}
		}
		break;
	case SDLK_SPACE:
		input->space = keydown;
		break;
	case SDLK_ESCAPE:
		if (keydown) {
			g_sys.input.quit = true;
		}
		break;
	case SDLK_F1:
	case SDLK_F2:
	case SDLK_F3:
	case SDLK_F4:
	case SDLK_F5:
	case SDLK_F6:
	case SDLK_F7:
	case SDLK_F8:
	case SDLK_F9:
	case SDLK_F10:
	case SDLK_F11:
	case SDLK_F12:
		if (keydown) {
			debug_channel = keysym->sym - SDLK_F1;
			debug_level = 1 << debug_channel;
			debug_enabled = g_debug_mask & debug_level;
			if (debug_enabled)
				g_debug_mask &= ~debug_level;
			else
				g_debug_mask |= debug_level;
			g_message.clear("%sabled debug %lu", debug_enabled  ? "En" : "Dis", debug_level);
			g_message.add("%sabled debug %lu", debug_enabled ? "Dis" : "En", debug_level);
		}
		break;
	case SDLK_KP2:
		if (keydown)
			_sine_offset_y++;
		break;
	case SDLK_KP4:
		if (keydown) {
			_sine_scale_y += _orig_sine_scale_y / 10;
			_sine_offset_y -= 2 * _orig_sine_scale_y / 10;
		}
		break;
	case SDLK_KP6:
		if (keydown) {
			_sine_scale_y -= _orig_sine_scale_y / 10;
			_sine_offset_y += 2 * _orig_sine_scale_y / 10;
		}
		break;
	case SDLK_KP8:
		if (keydown)
			_sine_offset_y--;
		break;
	case SDLK_MINUS:
		if (keydown) {
			g_sys.palette_offset = -1;
			g_sys.cycle_palette = true;
		}
		break;
	case SDLK_EQUALS:
	case SDLK_PLUS:
		if (keydown) {
			g_sys.palette_offset = 1;
			g_sys.cycle_palette = true;
		}
		break;
	case SDLK_1:
		input->digit1 = keydown;
		break;
	case SDLK_2:
		input->digit2 = keydown;
		break;
	case SDLK_3:
		input->digit3 = keydown;
		break;
	case SDLK_a:
		if (keydown) {
			g_sys.audio = !g_sys.audio;
			SDL_PauseAudio(!g_sys.audio);
			g_message.clear("Sound %s", g_sys.audio ? "off" : "on");
			g_message.add("Sound %s", g_sys.audio ? "on" : "off");
		}
		break;
	case SDLK_c:
		if (keydown) {
			g_sys.reset_cache_counters = true;
		}
		break;
	case SDLK_d:
		if (keydown)
			sdl2_rescale_screen(-1);
		break;
	case SDLK_e:
		if (keydown) {
			g_sys.redraw_cache = true;
			_print_palette = !_print_palette;
		}
		break;
	case SDLK_g:
		if (keydown) {
			g_sys.animate_tiles = !g_sys.animate_tiles;
			g_message.clear("Animated tiles %s", g_sys.animate_tiles ? "off" : "on");
			g_message.add("Animated tiles %s", g_sys.animate_tiles ? "on" : "off");
		}
		break;
	case SDLK_h:
		if (keydown) {
			g_sys.hybrid_color = !g_sys.hybrid_color;
			g_sys.cycle_palette = true;
			g_message.clear("Hybrid colour %s", g_sys.hybrid_color ? "off" : "on");
			g_message.add("Hybrid colour %s", g_sys.hybrid_color ? "on" : "off");
		}
		break;
	case SDLK_i:
		if (keydown)
			sdl2_rescale_screen(1);
		break;
	case SDLK_j:
		if (keydown) {
			g_message.add("Press jump button");
			_joystick_up_setup = true;
		}
		break;
	case SDLK_k:
		if (keydown)
			_sine_plot = !_sine_plot;
		break;
	case SDLK_o:
		if (keydown) {
			fprintf(stderr, "Restoring original window size %dx%d, scale %d, fullscreen %d\n", _orig_w, _orig_h, _orig_scale, _orig_fullscreen);
			g_sys.resize = true;
			_size_lock = false;
			sdl2_set_screen_size(_orig_w * _orig_scale, _orig_h * _orig_scale, _caption, _orig_scale, _orig_fullscreen, _orig_color);
			sdl2_rescale_spritesheets(_scale);
			reinit_slide();
		}
		break;
	case SDLK_p:
		if (keydown) {
			*paused = (bool)(*paused ? false : true);
			if (g_sys.audio)
				SDL_PauseAudio(*paused);
		}
		break;
	case SDLK_q:
		if (keydown)
			_sine_direction = -_sine_direction;
		break;
	case SDLK_s:
		if (keydown) {
			_size_lock = !_size_lock;
			g_message.clear("Size %s", _size_lock ? "unlocked" : "locked");
			g_message.add("Size %s", _size_lock ? "locked" : "unlocked");
		}
		break;
	case SDLK_t:
		if (keydown) {
			_scale = (_scale == _orig_scale ? 1 : _orig_scale);
			fprintf(stderr, "Toggling scale to %d\n", _scale);
			_window_w = g_sys.w * _scale;
			_window_h = g_sys.h * _scale;
			g_sys.resize = true;
			g_sys.resize_screen();
		}
	default:
		break;
	}
}

static void handle_joystickhatmotion(int value, struct input_t *input) {
	input->direction = 0;
	if (value & SDL_HAT_UP) {
		input->direction |= INPUT_DIRECTION_UP;
	}
	if (value & SDL_HAT_DOWN) {
		input->direction |= INPUT_DIRECTION_DOWN;
	}
	if (value & SDL_HAT_LEFT) {
		input->direction |= INPUT_DIRECTION_LEFT;
	}
	if (value & SDL_HAT_RIGHT) {
		input->direction |= INPUT_DIRECTION_RIGHT;
	}
}

static void handle_joystickaxismotion(int axis, int value, struct input_t *input) {
	static const int THRESHOLD = 3200;
	switch (axis) {
	case 0:
		input->direction &= ~(INPUT_DIRECTION_RIGHT | INPUT_DIRECTION_LEFT);
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_RIGHT;
		} else if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_LEFT;
		}
		break;
	case 1:
		input->direction &= ~(INPUT_DIRECTION_UP | INPUT_DIRECTION_DOWN);
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_DOWN;
		} else if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_UP;
		}
		break;
	}
}

static void handle_joystickbutton(int button, int pressed, struct input_t *input) {
	if (_joystick_up_setup) {
		if (pressed) {
			g_sys.input.jump_button = button;
			_joystick_up_setup = false;
			g_message.add("Jump on %d key", g_sys.input.jump_button);
			return;
		}
	}
	if (button == g_sys.input.jump_button) {
		if (pressed) {
			input->direction |= INPUT_DIRECTION_UP;
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		return;
	}
	switch(button) {
	case 0:
	case 1:
	case 2:
	case 3:
		input->space = pressed;
		break;
	case 4:
		if (pressed)
			sdl2_rescale_screen(-1);
		break;
	case 5:
		if (pressed)
			sdl2_rescale_screen(1);
		break;
	case 8:
		g_sys.input.quit = true;
		break;
	case 9:
		if (pressed) {
			g_sys.paused = (bool)(g_sys.paused ? false : true);
			if (g_sys.audio)
				SDL_PauseAudio(g_sys.paused);
		}
	}
}

static int handle_event(const SDL_Event *ev) {
	switch (ev->type) {
	case SDL_QUIT:
		g_sys.input.quit = true;
		break;
	case SDL_ACTIVEEVENT:
		switch (ev->active.state) {
		case SDL_APPINPUTFOCUS:
		case SDL_APPACTIVE:
			g_sys.paused = (ev->active.gain == 0);
			if (g_sys.audio)
				SDL_PauseAudio(g_sys.paused);
		}
		break;
	case SDL_VIDEORESIZE:
		fprintf(stderr, "Resizing to %dx%d\n", ev->resize.w, ev->resize.h);
		if (!_size_lock) {
			_window_w = ev->resize.w;
			_window_h = ev->resize.h;
			g_sys.resize = true;
			g_sys.resize_screen();
		} else {
			fprintf(stderr, "Scaling is locked\n");
		}
		break;
	case SDL_KEYUP:
		handle_keyevent(&ev->key.keysym, 0, &g_sys.input, &g_sys.paused);
		break;
	case SDL_KEYDOWN:
		handle_keyevent(&ev->key.keysym, 1, &g_sys.input, &g_sys.paused);
		break;
	case SDL_JOYHATMOTION:
		if (_joystick) {
			handle_joystickhatmotion(ev->jhat.value, &g_sys.input);
		}
		break;
	case SDL_JOYAXISMOTION:
		if (_joystick) {
			handle_joystickaxismotion(ev->jaxis.axis, ev->jaxis.value, &g_sys.input);
		}
		break;
	case SDL_JOYBUTTONUP:
		if (_joystick) {
			handle_joystickbutton(ev->jbutton.button, 0, &g_sys.input);
		}
		break;
	case SDL_JOYBUTTONDOWN:
		if (_joystick) {
			handle_joystickbutton(ev->jbutton.button, 1, &g_sys.input);
		}
		break;
	default:
		return -1;
	}
	return 0;
}

static void sdl2_process_events() {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		handle_event(&ev);
		if (g_sys.input.quit) {
			break;
		}
	}
}

static void sdl2_sleep(int duration) {
	SDL_Delay(duration);
}

static uint32_t sdl2_get_timestamp() {
	return SDL_GetTicks();
}

static void sdl2_start_audio(sys_audio_cb callback, void *param) {
	if (!g_sys.audio)
		return;
	SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));
	desired.freq = SYS_AUDIO_FREQ;
	desired.format = AUDIO_S16;
	desired.channels = 1;
	desired.samples = 2048;
	desired.callback = callback;
	desired.userdata = param;
	if (SDL_OpenAudio(&desired, 0) == 0) {
		SDL_PauseAudio(0);
	}
}

static void sdl2_stop_audio() {
	if (g_sys.audio)
		SDL_CloseAudio();
}

static void sdl2_lock_audio() {
	if (g_sys.audio)
		SDL_LockAudio();
}

static void sdl2_unlock_audio() {
	if (g_sys.audio)
		SDL_UnlockAudio();
}

static void render_load_sprites(int spr_type, int count, const struct sys_rect_t *r, const uint8_t *data, int w, int h, uint8_t color_key, bool update_pal) {
	assert(spr_type < ARRAYSIZE(_spritesheets));
	struct spritesheet_t *sheet = &_spritesheets[spr_type];
	sheet->count = count;
	sheet->r = (SDL_Rect *)malloc(count * sizeof(SDL_Rect));
	for (int i = 0; i < count; ++i) {
		SDL_Rect *rect = &sheet->r[i];
		rect->x = r[i].x;
		rect->y = r[i].y;
		rect->w = r[i].w;
		rect->h = r[i].h;
	}
	SDL_Surface *surface = SDL_CreateRGBSurface(0, w, h, 8, 0x0, 0x0, 0x0, 0x0);
	SDL_SetColors(surface, _palette->colors, 0, 256);
	SDL_SetColorKey(surface, SDL_SRCCOLORKEY | SDL_RLEACCEL, color_key);
	SDL_LockSurface(surface);
	memcpy(surface->pixels, data, w * h);
	SDL_UnlockSurface(surface);
	if (sheet->scale != _scale) {
		print_debug(DBG_SYSTEM, "Rescaling spritesheet %d during load", spr_type);
		sheet->texture = sdl2_rescale_surface(surface, _scale);
		sheet->scale = _scale;
	} else {
		sheet->texture = SDL_DisplayFormatAlpha(surface);
	}
	if (update_pal) { /* update texture on palette change */
		sheet->surface = surface;
	} else {
		SDL_FreeSurface(surface);
	}
}

static void render_unload_sprites(int spr_type) {
	for (int s = 0; s < MAX_SPRITES; s++) {
		if (_flipped_cache[spr_type][s]) {
			print_debug(DBG_SYSTEM, "Freeing cached sprite #%d from sheet %d %d", s, spr_type);
			SDL_FreeSurface(_flipped_cache[spr_type][s]);
			_flipped_cache[spr_type][s] = NULL;
		}
	}
	struct spritesheet_t *sheet = &_spritesheets[spr_type];
	free(sheet->r);
	if (sheet->surface) {
		SDL_FreeSurface(sheet->surface);
	}
	if (sheet->texture) {
		SDL_FreeSurface(sheet->texture);
	}
	memset(sheet, 0, sizeof(struct spritesheet_t));
}

static void render_add_sprite(int spr_type, int frame, int x, int y, int xflip, bool centred) {
	assert(_sprites_count < MAX_SPRITES);
	struct sprite_t *spr = &_sprites[_sprites_count];
	spr->sheet = spr_type;
	spr->num = frame;
	spr->x = x;
	spr->y = y;
	spr->xflip = xflip;
	spr->centred = centred;
	++_sprites_count;
}

static void render_clear_sprites() {
	_sprites_count = 0;
}

static void render_set_sprites_clipping_rect(int x, int y, int w, int h) {
	_sprites_cliprect.x = x * _scale;
	_sprites_cliprect.y = y * _scale;
	_sprites_cliprect.w = w * _scale;
	_sprites_cliprect.h = h * _scale;
}
