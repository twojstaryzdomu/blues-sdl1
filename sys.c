
#include "message.h"

struct message_t g_message = {
	.add = add,
	.get = get,
	.clear = clear,
	.clear_all = clear_all
};

#include "sys_sdl2.h"

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
	.clear_slide = clear_slide,
	.resize_screen = sdl2_resize_screen,
	.update_screen = sdl2_update_screen,
	.update_screen_cached = sdl2_update_screen_cached,
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
