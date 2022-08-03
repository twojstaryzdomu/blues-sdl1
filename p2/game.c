
#include <time.h>
#include "game.h"
#include "resource.h"
#include "sys.h"
#include "util.h"

struct vars_t g_vars;

void update_input() {
	g_sys.process_events();

	g_vars.input.key_left  = (g_sys.input.direction & INPUT_DIRECTION_LEFT) != 0  ? 0xFF : 0;
	g_vars.input.key_right = (g_sys.input.direction & INPUT_DIRECTION_RIGHT) != 0 ? 0xFF : 0;
	g_vars.input.key_up    = (g_sys.input.direction & INPUT_DIRECTION_UP) != 0    ? 0xFF : 0;
	g_vars.input.key_down  = (g_sys.input.direction & INPUT_DIRECTION_DOWN) != 0  ? 0xFF : 0;
	g_vars.input.key_space = g_sys.input.space ? 0xFF : 0;

	g_vars.input.keystate[2] = g_sys.input.digit1;
	g_vars.input.keystate[3] = g_sys.input.digit2;
	g_vars.input.keystate[4] = g_sys.input.digit3;

	if (g_sys.redraw_cache)
		g_vars.redraw_cache = g_sys.redraw_cache;
	if (g_sys.reset_cache_counters) {
		g_vars.redraw_counter = 0;
		g_vars.cache_counter = 0;
		g_sys.reset_cache_counters = false;
	}
	if (g_sys.animate_tiles != g_vars.animate_tiles)
		g_vars.animate_tiles = g_sys.animate_tiles;
}

static void wait_input(int timeout) {
	const uint32_t end = g_sys.get_timestamp() + timeout;
	while (g_sys.get_timestamp() < end) {
		g_sys.process_events();
		if (g_sys.input.quit || g_sys.input.space || g_sys.resize) {
			break;
		}
		g_sys.sleep(2);
	}
}

static void do_programmed_in_1992_screen() {
	time_t now;
	time(&now);
	struct tm *t = localtime(&now);
	if (t->tm_year + 1900 < 1996) { /* || t->tm_year + 1900 >= 2067 */
		return;
	}
	video_clear();
	g_sys.set_screen_palette(credits_palette_data, 0, 16, 6);
	do {
		video_resize();
		int offset = 0x960;
		video_draw_string(offset, 5, "YEAAA > > >");
		char str[64];
		snprintf(str, sizeof(str), "MY GAME IS STILL WORKING IN %04d <<", 1900 + t->tm_year);
		offset += 0x1E0;
		video_draw_string(offset, 0, str);
		offset = 0x1680;
		video_draw_string(offset, 1, "PROGRAMMED IN 1992 ON AT >286 12MHZ>");
		offset += 0x1E0;
		video_draw_string(offset, 3, "> > > ENJOY OLDIES<<");
		g_sys.update_screen(g_res.vga, 1);
		wait_input(1000);
	} while (g_sys.resize);
	video_clear();
}

static void do_credits() {
	do {
		video_resize();
		g_sys.set_screen_palette(credits_palette_data, 0, 16, 6);
		int offset = 0x140;
		video_draw_string(offset, 1, "CODER> DESIGNER AND ARTIST DIRECTOR>");
		offset += 0x230;
		video_draw_string(offset, 14, "ERIC ZMIRO");
		offset += 0x460;
		video_draw_string(offset, 4, ">MAIN GRAPHICS AND BACKGROUND>");
		offset += 0x230;
		video_draw_string(offset, 11, "FRANCIS FOURNIER");
		offset += 0x460;
		video_draw_string(offset, 9, ">MONSTERS AND HEROS>");
		offset += 0x230;
		video_draw_string(offset, 11, "LYES  BELAIDOUNI");
		offset = 0x1770;
		video_draw_string(offset, 15, "THANKS TO");
		offset = 0x1A40;
		video_draw_string(offset, 2, "CRISTELLE> GIL ESPECHE AND CORINNE>");
		offset += 0x1E0;
		video_draw_string(offset, 0, "SEBASTIEN BECHET AND OLIVIER AKA DELTA>");
		g_sys.update_screen(g_res.vga, 1);
		wait_input(1000);
	} while (g_sys.resize);
}

static void update_screen_img(uint8_t *src, int present) {
	int size = GAME_SCREEN_W * GAME_SCREEN_H;
	if (size < ORIG_W * ORIG_H) {
		return;
	} else if (size == ORIG_W * ORIG_H) {
		g_sys.update_screen(src, present);
	} else {
		if (!g_sys.resize) {
			memset(g_res.vga, 0, size);
		} else {
			video_resize();
		}
		video_copy_centred(src, ORIG_W, ORIG_H);
		g_sys.update_screen(g_res.vga, present);
	}
}

static void do_titus_screen() {
	uint8_t *data = load_file("TITUS.SQZ");
	if (data) {
		g_sys.set_screen_palette(data, 0, 256, 6);
		do {
			video_resize();
			update_screen_img(data + 768, 0);
			g_sys.fade_in_palette();
			wait_input(700);
			g_sys.fade_out_palette();
		} while (g_sys.resize);
		free(data);
	}
}

static void do_motif_screen() {
	uint8_t *data = load_file("MOTIF.SQZ");
	g_sys.sine = true;
	if (data) {
		g_sys.set_screen_palette(motif_palette_data, 0, 16, 6);
		do {
			video_resize();
			video_copy_img(data);
			video_copy_centred(g_res.background, ORIG_W, ORIG_H);
			if (g_sys.cycle_palette) {
				g_vars.palette = (g_vars.palette + g_sys.palette_offset + UNIQUE_PALETTES) % UNIQUE_PALETTES;
				g_sys.set_screen_palette(unique_palettes_tbl[g_vars.palette], 0, 16, 6);
				g_sys.cycle_palette = false;
			}
			g_sys.update_screen(g_res.vga, 0);
			video_load_sprites();
			video_draw_motif_string("MODE", 0, 0, 1);
			char *s = g_vars.expert_flag
				? "EXPERT"
				: "BEGINNER";
			video_draw_motif_string(s, 0, 32, 2);
			g_sys.update_screen_cached(g_res.vga, 1, 1);
			wait_input(1);
			if (g_sys.input.direction) {
				g_vars.expert_flag = !g_vars.expert_flag;
				g_sys.input.direction = 0;
			}
			while (g_sys.paused)
				wait_input(100);
			if (g_sys.input.space)
				break;
			g_sys.render_clear_sprites();
		} while (!g_vars.input.key_space || !g_sys.input.quit);
		g_vars.input.key_space = 0;
		g_sys.fade_out_palette();
		free(data);
	}
	g_sys.sine = false;
}

static unsigned char atoh(unsigned char data) {
	if (data > '9')
		data += 9;
	return (data &= 0x0F);
}

static void update_code(uint16_t *buf, uint8_t *nibble_index) {
	if (g_sys.input.hex) {
		int i = atoh(g_sys.input.hex);
		if (i < 0xF + 1) {
			if (*nibble_index < CODE_LEN) {
				int l = (CODE_LEN - *nibble_index - 1) * 4;
				*buf	+= i << l;
				++*nibble_index;
			} else {
				wait_input(100);
				memset(buf, 0, *nibble_index);
				*nibble_index = 0;
			}
		}
		g_sys.input.hex = 0;
	}
}

static void print_codes() {
	for (int l = 0; l < 8; l++)
		print_debug(DBG_GAME, "print_codes: level %d = %04X expert %04X", l, random_get_number3(l),  random_get_number3(l + 10));
}

static bool parse_code(uint16_t *buf, uint8_t *index) {
	bool rc = false;
	if (*index == CODE_LEN) {
		print_debug(DBG_GAME, "parse_code: %04X", *buf);
		uint8_t levels = 8;
		uint8_t expert = 10;
		for (uint8_t l = 0; l < levels; l++) {
			for (uint16_t r = l; r < levels + expert; r += expert)
				if (*buf == random_get_number3(r)) {
					g_vars.level_num = l;
					g_vars.expert_flag = r / expert;
					rc = true;
					break;
				}
			if (rc)
				break;
		}
		if (!rc) {
			if (*buf == level_code[g_vars.password_flag]) {
				print_debug(DBG_GAME, "parse_code: password matched %04X", level_code[g_vars.password_flag]);
				if (g_vars.password_flag == CODE_COUNT - 1)
					print_debug(DBG_GAME, "parse_code: now enter level");
				++g_vars.password_flag;
				memset(buf, 0, sizeof(uint16_t));
				*index = 0;
			} else {
				if (g_vars.password_flag == CODE_COUNT) {
					if (*buf < 14)
						g_vars.level_num = *buf;
					g_vars.expert_flag = *buf / expert;
				}
				g_vars.password_flag = 0;
				rc = true;
			}
		}
	}
	if (rc)
		print_debug(DBG_GAME, "parse_code: level %d, expert %d", g_vars.level_num, g_vars.expert_flag);
	return rc;
}

static void do_code_screen() {
	uint8_t *data = load_file("MOTIF.SQZ");
	g_sys.sine = true;
	if (data) {
		char str[CODE_LEN + 1];
		uint8_t nibble_index = 0;
		uint16_t *buf = calloc(1, sizeof(int16_t));
		g_sys.input.raw = true;
		g_sys.set_screen_palette(motif_palette_data, 0, 16, 6);
		do {
			video_resize();
			video_copy_img(data);
			video_copy_centred(g_res.background, ORIG_W, ORIG_H);
			if (g_sys.cycle_palette) {
				g_vars.palette = (g_vars.palette + g_sys.palette_offset + UNIQUE_PALETTES) % UNIQUE_PALETTES;
				g_sys.set_screen_palette(unique_palettes_tbl[g_vars.palette], 0, 16, 6);
				g_sys.cycle_palette = false;
			}
			g_sys.update_screen(g_res.vga, 0);
			video_load_sprites();
			video_draw_motif_string("ENTER CODE", 0, -10, 2);
			sprintf(str, "%04X", *buf);
			video_draw_motif_string(str, 0, 40, 1);
			g_sys.update_screen_cached(g_res.vga, 1, 1);
			if (g_sys.input.space || parse_code(buf, &nibble_index))
				break;
			do {
				wait_input(5);
			} while (g_sys.paused);
			update_code(buf, &nibble_index);
			g_sys.render_clear_sprites();
		} while (!g_vars.input.key_space || !g_sys.input.quit);
		g_vars.input.key_space = 0;
		g_sys.input.raw = false;
		g_sys.fade_out_palette();
		free(data);
		print_codes();
	}
	g_sys.sine = false;
}

static bool fade_palettes(const uint8_t *target, uint8_t *current) {
	bool flag = false;
	for (int i = 0; i < 768; ++i) {
		int al = current[i];
		const int diff = target[i] - al;
		if (diff != 0) {
			if (abs(diff) < 2) {
				flag = true;
				current[i] = target[i];
			} else {
				if (target[i] < al) {
					current[i] = al - 2;
				} else {
					current[i] = al + 2;
				}
			}
		}
	}
	return flag;
}

static void do_present_screen() {
	uint8_t *data = load_file("PRESENT.SQZ");
	if (data) {
		if (g_uncompressed_size == 65536 + 768) { /* demo version */
			g_sys.set_screen_palette(data, 0, 256, 6);
			update_screen_img(data + 768, 0);
			g_sys.fade_in_palette();
		} else {
			memmove(data + 768, data + 0x1030 * 16, 93 * ORIG_W);
			g_sys.set_screen_palette(data, 0, 256, 6);
			update_screen_img(data + 768, 0);
			g_sys.fade_in_palette();
			uint8_t palette[256 * 3];
			memcpy(palette, data, 256 * 3);
			while (fade_palettes(present_palette_data, palette) && !g_sys.input.quit) {
				g_sys.set_screen_palette(palette, 0, 256, 6);
				update_screen_img(data + 768, 1);
				wait_input(100);
			}
		}
		g_sys.fade_out_palette();
		free(data);
	}
}

static void do_demo_screen() {
	uint8_t *data = load_file("JOYSTICK.SQZ");
	if (data) {
		do {
			video_resize();
			video_copy_img(data);
			g_sys.set_screen_palette(joystick_palette_data, 0, 16, 6);
			update_screen_img(g_res.background, 0);
			g_sys.fade_in_palette();
			wait_input(10000);
		} while (g_sys.resize);
		free(data);
	}
}

static void do_castle_screen() {
	uint8_t *data = load_file("CASTLE.SQZ");
	if (data) {
		do {
			video_resize();
			g_sys.set_screen_palette(data, 0, 256, 6);
			update_screen_img(data + 768, 1);
			g_sys.fade_in_palette();
			wait_input(10000);
		} while (g_sys.resize);
		free(data);
	}
}

static void do_map(){
	static const uint16_t spr_pos[2][10] = {
		{ 53, 63,  95, 115, 106,  81,  74, 146,  76,  48 },
		{  0, 80, 120, 152, 232, 328, 424, 528, 600, 600 }
	};
	const uint8_t pal = 13;
	const uint16_t spr = 457;
	uint8_t *data = load_file("MAP.SQZ");
	int dst_offset, src_offset, blank_offset;
	if (data) {
		g_res.map = (uint8_t *)malloc(MAP_W*MAP_H);
		video_copy_map(data);
		video_clear();
		if (g_res.map) {
			const uint16_t pos_y = spr_pos[0][g_vars.level_num];
			const uint16_t pos_x = spr_pos[1][g_vars.level_num];
			g_sys.set_screen_palette(palettes_tbl[pal], 0, 16, 6);
			video_load_sprites();
			for (uint16_t x = 1; x <= MAP_W + (GAME_SCREEN_W < MAP_W ? 0 : (GAME_SCREEN_W - MAP_W) / 2); ++x) { /* 640*200*4bpp pic */
				if (g_sys.cycle_palette) {
					g_vars.palette = (g_vars.palette + g_sys.palette_offset + UNIQUE_PALETTES) % UNIQUE_PALETTES;
					g_sys.set_screen_palette(unique_palettes_tbl[g_vars.palette], 0, 16, 6);
					g_sys.cycle_palette = false;
				}
				video_resize();
				uint16_t y_offs = (GAME_SCREEN_H - ORIG_H) / 2;
				uint16_t pitch = MIN(x, GAME_SCREEN_W);
				uint16_t window_w = x < GAME_SCREEN_W ? 0 : x - GAME_SCREEN_W;
				g_sys.render_set_sprites_clipping_rect(0, 0, GAME_SCREEN_W, GAME_SCREEN_H);
				g_sys.render_clear_sprites();
				video_draw_sprite(spr, GAME_SCREEN_W - x + pos_x, pos_y + y_offs, 0, false);
				for (uint8_t y = 0; y < MAP_H; ++y) {
					dst_offset = (y_offs + y) * GAME_SCREEN_W + GAME_SCREEN_W - pitch,
					src_offset = y * MAP_W + window_w;
					blank_offset = dst_offset + MAP_W;
					memcpy(g_res.vga + dst_offset,
						g_res.map + src_offset,
						pitch);
					if (x > MAP_W) {
						memset(g_res.vga + blank_offset,
							0,
							x % MAP_W);
					}
				}
				g_sys.update_screen(g_res.vga, 1);
				wait_input(1);
				if (g_sys.input.quit || g_sys.input.space) {
					break;
				}
				do {
					wait_input(10);
				} while (g_sys.paused);
			}
			g_sys.sleep(1000);
			g_sys.fade_out_palette();
			free(g_res.map);
		}
		free(data);
	}
}

void do_gameover_animation();

void do_gameover_screen() {
	uint8_t *data = load_file("GAMEOVER.SQZ");
	if (data) {
		video_clear();
		video_copy_img(data);
		video_copy(g_res.background, ORIG_W, ORIG_H);
		g_sys.set_screen_palette(gameover_palette_data, 0, 16, 6);
		do_gameover_animation();
		video_clear();
		video_copy_centred(g_res.background, ORIG_W, ORIG_H);
		g_sys.update_screen(g_res.vga, 0);
		g_sys.fade_out_palette();
		free(data);
	}
}

void do_demo_animation();

static void do_menu2() {
	uint8_t *data = load_file("MENU2.SQZ");
	if (data) {
		video_copy_img(data);
		video_copy_centred(g_res.background, ORIG_W, ORIG_H);
		g_sys.set_screen_palette(data + 32000, 0, 16, 6);
		g_sys.update_screen(g_res.vga, 0);
		g_sys.fade_in_palette();
		do_demo_animation();
		g_sys.fade_out_palette();
		free(data);
	}
}

static int do_menu() {
	int rc = 0;
	uint8_t *data = load_file("MENU.SQZ");
	if (data) {
		g_sys.set_screen_palette(data, 0, 256, 6);
		do {
			video_resize();
			g_sys.centred = true;
			update_screen_img(data + 768, 0);
			g_sys.fade_in_palette();
			memset(g_vars.input.keystate, 0, sizeof(g_vars.input.keystate));
			const uint32_t start = g_sys.get_timestamp();
			while (!g_sys.input.quit) {
				update_input(300);
				if (g_vars.input.keystate[2] || g_vars.input.keystate[0x4F] || g_sys.input.space) {
					g_sys.input.space = 0;
					g_sys.fade_out_palette();
					rc = 1;
					break;
				}
				if (g_vars.input.keystate[3] || g_vars.input.keystate[0x50]) {
					g_sys.fade_out_palette();
					rc = 2;
					break;
				}
				if (g_sys.resize) {
					break;
				}
				if (!g_res.dos_demo && g_sys.get_timestamp() - start >= 15 * 1000) {
					g_sys.fade_out_palette();
					rc = 3;
					break;
				}
			}
		} while (g_sys.resize);
		free(data);
		g_sys.centred = false;
	}
	return rc;
}

static void do_photos_screen() {
}

void input_check_ctrl_alt_e() {
	if (g_vars.input.keystate[0x1D] && g_vars.input.keystate[0x38] && g_vars.input.keystate[0x12]) {
		do_photos_screen();
	}
}

void input_check_ctrl_alt_w() {
	if (g_vars.input.keystate[0x1D] && g_vars.input.keystate[0x38] && g_vars.input.keystate[0x11]) {
		do_credits();
		wait_input(600);
	}
}

void do_theend_screen() {
	uint8_t *data = load_file("THEEND.SQZ");
	if (data) {
		do {
			video_resize();
			g_sys.set_screen_palette(data, 0, 256, 6);
			update_screen_img(data + 768, 0);
			g_sys.fade_in_palette();
			wait_input(10000);
		} while (g_sys.resize);
		free(data);
	}
	time_t now;
	time(&now);
	struct tm *t = localtime(&now);
	if (t->tm_year + 1900 < 1994) {
		return;
	}
	do_photos_screen();
}

uint32_t timer_get_counter() {
	const uint32_t current = g_sys.get_timestamp();
	return ((current - g_vars.starttime) * 1193182 / 0x4000) / 1000;
}

void random_reset() {
	g_vars.random.a = 5;
	g_vars.random.b = 34;
	g_vars.random.c = 134;
	g_vars.random.d = 58765;
}

uint8_t random_get_number() {
	g_vars.random.d += g_vars.random.a;
	g_vars.random.a += 3 + (g_vars.random.d >> 8);

	g_vars.random.b += g_vars.random.c;
	g_vars.random.b *= 2;
	g_vars.random.b += g_vars.random.a;

	g_vars.random.c ^= g_vars.random.a;
	g_vars.random.c ^= g_vars.random.b;

	return g_vars.random.b;
}

static uint16_t ror16(uint16_t x, int c) {
	return (x >> c) | (x << (16 - c));
}

uint16_t random_get_number2() {
	const uint16_t x = g_vars.random.e + 0x9248;
	g_vars.random.e = ror16(x, 3);
	return g_vars.random.e;
}

static uint16_t rol16(uint16_t x, int c) {
	return (x << c) | (x >> (16 - c));
}

/* original code returns a machine specific value, based on BIOS and CPU */
uint16_t random_get_number3(uint16_t x) {
	x ^= 0x55a3;
	x *= 0xb297; /* to match dosbox */
	return rol16(x, 3);
}

static void game_run(const char *data_path) {
	res_init(data_path, GAME_SCREEN_W * GAME_SCREEN_H);
	sound_init();
	video_convert_tiles(g_res.uniondat, g_res.unionlen);
	g_vars.level_num = g_options.start_level;
	g_vars.animate_tiles = g_options.animate_tiles;
	do_programmed_in_1992_screen();
	if (!g_sys.input.space && !g_sys.input.quit) {
		do_titus_screen();
		play_music(3);
		do_present_screen();
	}
	g_vars.random.e = 0x1234;
	g_vars.expert_flag = false;
	g_vars.starttime = g_sys.get_timestamp();
	while (!g_sys.input.quit) {
		if (1) {
			g_vars.player_lifes = 2;
			g_vars.player_bonus_letters_mask = 0;
			g_vars.player_club_power = 20;
			g_vars.player_club_type = 0;
			if (g_res.dos_demo) {
				do_demo_screen();
			}
			int rc;
			while ((rc = do_menu())) {
				if (rc == 1)
					break;
				else if (rc == 2) {
					do_code_screen();
					if (g_vars.level_num || g_vars.expert_flag)
						break;
				} else
					do_menu2();
			}
			if (g_sys.input.quit) {
				break;
			}
			if (!(g_vars.level_num || g_vars.expert_flag))
				do_motif_screen();
			uint8_t level_num;
			do {
				g_sys.render_set_sprites_clipping_rect(0, 0, GAME_SCREEN_W, TILEMAP_SCREEN_H);
				level_num = g_vars.level_num;
				if (g_vars.level_num >= 8 && g_vars.level_num < 10 && 0 /* !g_vars.level_expert_flag */ ) {
					do_castle_screen();
					break;
				}
				if (g_vars.level_num < 10 && g_options.show_map)
					do_map();
				if (g_sys.input.quit)
					break;
				do_level();
				print_debug(DBG_GAME, "previous level %d current %d", level_num, g_vars.level_num);
			} while (!g_res.dos_demo && g_vars.level_num != level_num);
			g_vars.level_num = 0;
			g_vars.expert_flag = false;
		}
	}
	sound_fini();
	res_fini();
}

struct game_t game = {
	"Prehistorik 2",
	game_run
};

