
#include <SDL.h>
#include "sys.h"
#include "SDL_surface.h"
#include "SDL_AllocPalette.h"
#include "util.h"

#define COPPER_BARS_H 80
#define MAX_SPRITES 512
#define MAX_SPRITESHEETS 3

static const int FADE_STEPS = 16;

struct spritesheet_t {
	int count;
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
};

static struct sprite_t _sprites[MAX_SPRITES];
static int _sprites_count;
static SDL_Rect _sprites_cliprect;

static int _window_w, _window_h;
#if defined(HAVE_X11)
static int _fullscreen_w, _fullscreen_h;
#endif
static int _shake_dx, _shake_dy;
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
static bool _hybrid_color;
static const char *_caption;
static bool _fullscreen;
static bool _hybrid_color;

static int _orig_w, _orig_h;
static bool _size_lock;
static bool _orig_fullscreen;
static bool _orig_color;

static SDL_Joystick *_joystick;

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
	SDL_Quit();
}

static void sdl2_set_screen_size(int w, int h, const char *caption, bool fullscreen, bool hybrid_color) {
	_caption = caption;
	_fullscreen = fullscreen;
	int screen_w = MAX(w, 320);
	int screen_h = MAX(h, 200);
	if (!_size_lock) {
		g_sys.w = screen_w;
		g_sys.h = screen_h;
	}
	if (!_orig_w) {
		_orig_w = screen_w;
		_orig_h = screen_h;
		_orig_fullscreen = fullscreen;
		_orig_color = hybrid_color;
		fprintf(stderr, "Original window size: %dx%d Fullscreen: %s Hybrid colour: %s\n", _orig_w, _orig_h, _orig_fullscreen ? "on" : "off", _orig_color ? "on" : "off");
	}
	flags = fullscreen ? SDL_FULLSCREEN : 0;
	flags |= SDL_HWPALETTE;
	flags |= SDL_RESIZABLE;
	flags |= SDL_DOUBLEBUF;
	flags |= SDL_ASYNCBLIT;
	flags |= SDL_ANYFORMAT;
	if (_renderer)
		SDL_FreeSurface(_renderer);
	_renderer = SDL_SetVideoMode(screen_w, screen_h, 8, flags);
	if(!_renderer){
		printf("Couldn't set video mode: %s\n", SDL_GetError());
		exit(-1);
	}
	SDL_WM_SetCaption(caption, 0);
	fprintf(stderr, "Size: %dx%d", g_sys.w, g_sys.h);
	if (g_sys.w != _window_w && g_sys.h != _window_h)
		fprintf(stderr, " Window: %dx%d", screen_w, screen_w);
	fprintf(stderr, "\n");
	print_debug(DBG_SYSTEM, "set_screen_size %d,%d", g_sys.w, g_sys.h);
	if (_screen_buffer)
		free(_screen_buffer);
	_screen_buffer = (uint32_t *)calloc(g_sys.w * g_sys.h, sizeof(uint32_t));
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
	_sprites_cliprect.w = g_sys.w;
	_sprites_cliprect.h = g_sys.h;
	_hybrid_color = hybrid_color;
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
		if(_hybrid_color && i < 2){
			g = 0;
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
			SDL_FreeSurface(sheet->texture);
			SDL_SetColors(sheet->surface, _palette->colors, 0, 256);
			sheet->texture = SDL_DisplayFormatAlpha(sheet->surface);
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

static void fade_palette_helper(int in) {
	int component = 0;
	SDL_Surface* surface = SDL_CreateRGBSurface(0, g_sys.w, g_sys.h, 32, rmask, gmask, bmask, amask);
	for (int i = 0; i <= FADE_STEPS; ++i) {
		int alpha = 255 * i / FADE_STEPS;
		if (in) {
			alpha = 255 - alpha;
		}
		Uint32 color = SDL_MapRGBA(surface->format, component, component, component, alpha);
		SDL_FillRect(surface, 0, color);
		SDL_BlitSurface(_texture, 0, _renderer, 0);
		SDL_BlitSurface(surface, 0, _renderer, 0);
		SDL_Flip(_renderer);
		SDL_Delay(30);
	}
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

static void sdl2_transition_screen(const struct sys_rect_t *s, enum sys_transition_e type, bool open) {
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

static void sdl2_resize_screen() {
	sdl2_set_screen_size(_window_w, _window_h, _caption, _fullscreen, _hybrid_color);
}

static void sdl2_update_screen(const uint8_t *p, int present) {
	if (_copper_color_key != -1) {
		for (int j = 0; j < g_sys.h; ++j) {
			if (j / 2 < COPPER_BARS_H) {
				const uint32_t line_color = _copper_palette[j / 2];
				for (int i = 0; i < g_sys.w; ++i) {
					_screen_buffer[j * g_sys.w + i] = (p[i] == _copper_color_key) ? line_color : _screen_palette[p[i]];
				}
			} else {
				for (int i = 0; i < g_sys.w; ++i) {
					_screen_buffer[j * g_sys.w + i] = _screen_palette[p[i]];
				}
			}
			p += g_sys.w;
		}
	} else {
		for (int i = 0; i < g_sys.w * g_sys.h; ++i) {
			_screen_buffer[i] = _screen_palette[p[i]];
		}
	}
	SDL_FreeSurface(_texture);
	_texture = SDL_ConvertSurface(SDL_CreateRGBSurfaceFrom(_screen_buffer, g_sys.w, g_sys.h, 32, g_sys.w * sizeof(uint32_t), rmask, gmask, bmask, amask), _fmt, 0);
	if (present) {
		SDL_Rect r={_shake_dx, _shake_dy, g_sys.w, g_sys.h};
		SDL_FillRect(_renderer, &r, -1);
		SDL_BlitSurface(_texture, &r, _renderer, 0);

		// sprites
		SDL_SetClipRect(_renderer, &_sprites_cliprect);
		for (int i = 0; i < _sprites_count; ++i) {
			const struct sprite_t *spr = &_sprites[i];
			struct spritesheet_t *sheet = &_spritesheets[spr->sheet];
			if (spr->num >= sheet->count) {
				continue;
			}
			SDL_Rect r;
			r.x = spr->x + _shake_dx;
			r.y = spr->y + _shake_dy;
			r.w = sheet->r[spr->num].w;
			r.h = sheet->r[spr->num].h;
			SDL_Rect t;
			t.x = sheet->r[spr->num].x;
			t.w = sheet->r[spr->num].w;
			t.h = sheet->r[spr->num].h;
			t.y = sheet->r[spr->num].y;
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

		SDL_Flip(_renderer);
	}
}

static void sdl2_shake_screen(int dx, int dy) {
	_shake_dx = dx;
	_shake_dy = dy;
}

static void handle_keyevent(const SDL_keysym *keysym, bool keydown, struct input_t *input, bool *paused) {
	switch (keysym->sym) {
	case SDLK_LEFT:
		if (keydown) {
			input->direction |= INPUT_DIRECTION_LEFT;
		} else {
			input->direction &= ~INPUT_DIRECTION_LEFT;
		}
		break;
	case SDLK_RIGHT:
		if (keydown) {
			input->direction |= INPUT_DIRECTION_RIGHT;
		} else {
			input->direction &= ~INPUT_DIRECTION_RIGHT;
		}
		break;
	case SDLK_UP:
		if (keydown) {
			input->direction |= INPUT_DIRECTION_UP;
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		break;
	case SDLK_DOWN:
		if (keydown) {
			input->direction |= INPUT_DIRECTION_DOWN;
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
						if (g_sys.w == 320 && g_sys.h == 200) {
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
	case SDLK_1:
		input->digit1 = keydown;
		break;
	case SDLK_2:
		input->digit2 = keydown;
		break;
	case SDLK_3:
		input->digit3 = keydown;
		break;
	case SDLK_o:
		if (keydown) {
			fprintf(stderr, "Restoring original window size %dx%d, fullscreen %d\n", _orig_w, _orig_h, _orig_fullscreen);
			g_sys.resize = true;
			_size_lock = false;
			sdl2_set_screen_size(_orig_w, _orig_h, _caption, _orig_fullscreen, _orig_color);
		}
		break;
	case SDLK_p:
		if (keydown) {
			*paused = (bool)(*paused ? false : true);
			if (g_sys.audio)
				SDL_PauseAudio(*paused);
		}
		break;
	case SDLK_s:
		if (keydown) {
			_size_lock = !_size_lock;
			fprintf(stderr, "Screen size is %s\n", _size_lock ? "locked" : "unlocked");
		}
		break;
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
		_window_w = ev->resize.w;
		_window_h = ev->resize.h;
		g_sys.resize = true;
		g_sys.resize_screen();
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
	for (int y = 0; y < h; ++y) {
		memcpy(((uint8_t *)surface->pixels) + y * surface->pitch, data + y * w, w);
	}
	SDL_UnlockSurface(surface);
	sheet->texture = SDL_DisplayFormatAlpha(surface);
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

static void render_add_sprite(int spr_type, int frame, int x, int y, int xflip) {
	assert(_sprites_count < MAX_SPRITES);
	struct sprite_t *spr = &_sprites[_sprites_count];
	spr->sheet = spr_type;
	spr->num = frame;
	spr->x = x;
	spr->y = y;
	spr->xflip = xflip;
	++_sprites_count;
}

static void render_clear_sprites() {
	_sprites_count = 0;
}

static void render_set_sprites_clipping_rect(int x, int y, int w, int h) {
	_sprites_cliprect.x = x;
	_sprites_cliprect.y = y;
	_sprites_cliprect.w = w;
	_sprites_cliprect.h = h;
}

struct sys_t g_sys = {
	.init = sdl2_init,
	.fini = sdl2_fini,
	.set_screen_size = sdl2_set_screen_size,
	.set_screen_palette = sdl2_set_screen_palette,
	.set_palette_amiga = sdl2_set_palette_amiga,
	.set_copper_bars = sdl2_set_copper_bars,
	.set_palette_color = sdl2_set_palette_color,
	.fade_in_palette = sdl2_fade_in_palette,
	.fade_out_palette = sdl2_fade_out_palette,
	.resize_screen = sdl2_resize_screen,
	.update_screen = sdl2_update_screen,
	.shake_screen = sdl2_shake_screen,
	.transition_screen = sdl2_transition_screen,
	.process_events = sdl2_process_events,
	.sleep = sdl2_sleep,
	.get_timestamp = sdl2_get_timestamp,
	.start_audio = sdl2_start_audio,
	.stop_audio = sdl2_stop_audio,
	.lock_audio = sdl2_lock_audio,
	.unlock_audio = sdl2_unlock_audio,
	.render_load_sprites = render_load_sprites,
	.render_unload_sprites = render_unload_sprites,
	.render_add_sprite = render_add_sprite,
	.render_clear_sprites = render_clear_sprites,
	.render_set_sprites_clipping_rect = render_set_sprites_clipping_rect
};
