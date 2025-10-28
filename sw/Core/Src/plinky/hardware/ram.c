#include "ram.h"
#include "flash.h"
#include "gfx/gfx.h"
#include "synth/params.h"
#include "ui/shift_states.h"

// cleanup
#include "hardware/codec.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
// -- cleanup

#define NUM_RAM_ITEMS (NUM_PRESETS + NUM_PATTERNS + NUM_SAMPLES)

typedef enum RamItemType {
	RAM_PRESET,
	RAM_PATTERN,
	RAM_SAMPLE,
	NUM_ITEM_TYPES,
} RamItemType;

static bool ram_initialized = false;
SysParams sys_params;

// item we are (or want to be) editing
u8 cur_preset_id;
static u8 cur_pattern_id = 0;
u8 cur_sample_id = 0;

// item actually in ram
static u8 ram_preset_id = 255;
static u8 ram_pattern_id = 255;
static u8 ram_sample_id = 255;

// ram item contents
Preset cur_preset;                        // floating preset, the one we edit and use for sound generation
static PatternQuarter cur_pattern_qtr[4]; // floating pattern, the one we edit and use for recording/playing
SampleInfo cur_sample_info;

// item to change to
static u8 cued_preset_id = 255;
static u8 cued_pattern_id = 255;
static u8 cued_sample_id = 255;

// history of above, used to check double press on the same item
static u8 prev_cued_sample_id = 255;

static u8 edit_item_id = 255;   // ram item to edit | msb unset => save, msb set => clear
static u8 recent_load_item = 0; // the most recently touched load item

static u32 last_ram_write[NUM_RAM_SEGMENTS];
static u32 last_flash_write[NUM_RAM_SEGMENTS];

// == UTILS == //

static RamItemType get_item_type(u8 item_id) {
	return item_id < PATTERNS_START  ? RAM_PRESET
	       : item_id < SAMPLES_START ? RAM_PATTERN
	       : item_id < NUM_RAM_ITEMS ? RAM_SAMPLE
	                                 : NUM_RAM_ITEMS;
}

// when the current item is not equal to the item in ram, this indicates we're still writing the old ram item to flash
static bool preset_outdated(void) {
	return cur_preset_id != ram_preset_id;
}
bool pattern_outdated(void) {
	return cur_pattern_id != ram_pattern_id;
}
static bool sample_outdated(void) {
	return cur_sample_id != ram_sample_id;
}

// == MAIN == //

// only_filled returns 0 if the step doesn't hold any pressure data
PatternStringStep* string_step_ptr(u8 string_id, bool only_filled, u8 seq_step) {
	if (preset_outdated() && only_filled)
		return 0;
	PatternStringStep* step = &cur_pattern_qtr[(seq_step >> 4) & 3].steps[seq_step & 15][string_id];
	if (!only_filled)
		return step;
	// return pointer if any of its substeps hold pressure
	for (u8 substep_id = 0; substep_id < 8; substep_id++)
		if (step->pres[substep_id])
			return step;
	// otherwise return 0
	return 0;
}

void init_ram(void) {
	cued_preset_id = -1;
	cued_pattern_id = -1;
	cued_sample_id = -1;
	ram_pattern_id = -1;
	ram_sample_id = -1;
	ram_preset_id = -1;
	// relocate the first preset and pattern into ram
	edit_item_id = 255;
	for (u8 i = 0; i < NUM_RAM_SEGMENTS; ++i) {
		last_ram_write[i] = 0;
		last_flash_write[i] = 0;
	}
	// update sys params
	Font font = F_16;
	switch (sys_params.version) {
	case LPE_SYS_PARAMS_VERSION:
		// correct!
		break;
	case OG_SYS_PARAMS_VERSION:
		// never initialized before - fill with defaults
		sys_params.midi_in_chan = 0;
		sys_params.midi_out_chan = 0;
		sys_params.accel_sens = 150; // 100%
		sys_params.cv_quant = CVQ_OFF;
		sys_params.reverse_encoder = false;
		sys_params.preset_aligned = false;
		sys_params.pattern_aligned = false;
		memset(sys_params.pad, 0, sizeof(sys_params.pad));
		sys_params.version = REV_SYS_PARAMS_VERSION;
		// fall through for further updating
	case REV_SYS_PARAMS_VERSION:
		// initialized but volume in OG mapping - remap volume
		u16 og_vol = clampi(((s8)sys_params.volume_lsb + 45) << 4, 0, RAW_SIZE);
		sys_params.volume_lsb = og_vol & 255;
		sys_params.volume_msb = (og_vol >> 8) & 7;

		// finalize
		sys_params.version = LPE_SYS_PARAMS_VERSION;
		log_ram_edit(SEG_SYS);
		HAL_Delay(500);
		oled_clear();
		draw_str_ctr(0, font, "updated");
		draw_str_ctr(16, font, "system settings");
		oled_flip();
		HAL_Delay(2000);
	}
	codec_update_volume();
	cur_preset_id = sys_params.preset_id;
	const Preset* cur_preset_ptr = cur_preset_flash_ptr();
	if (cur_preset_ptr) {
		// load floating preset
		memcpy(&cur_preset, cur_preset_ptr, sizeof(Preset));
		ram_preset_id = sys_params.preset_id;
		// load floating pattern
		for (u8 qtr = 0; qtr < 4; qtr++)
			memcpy(&cur_pattern_qtr[qtr], cur_pattern_qtr_flash_ptr(qtr), sizeof(PatternQuarter));
		cur_pattern_id = param_index_unmod(P_PATTERN);
		ram_pattern_id = cur_pattern_id;
	}

	recent_load_item = cur_preset_id;
	ram_initialized = true;
}

static bool need_flash_write(RamSegment seg, u32 now) {
	// segment up to date => no write
	if (last_ram_write[seg] == last_flash_write[seg])
		return false;

	// a ram item (preset, pattern, sample) being outdated means the user has requested to load a different one, but
	// that load has not happened yet because the current item hasn't finished writing to flash - we need to write it to
	// flash immediately so the new item can be loaded
	switch (seg) {
	case SEG_PRESET:
		if (preset_outdated())
			return true;
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		if (pattern_outdated())
			return true;
		break;
	case SEG_SAMPLE:
		if (sample_outdated())
			return true;
		break;
	default:
		break;
	}

	// if our ram item was not outdated, that means we changed something small (a parameter, the contents of a step,
	// etc) we try to wait for at least 5 seconds after the most recent edit before we write them to flash
	if (now - last_ram_write[seg] > 5000)
		return true;
	// but if we are out of date for a minute or more, we write immediately
	return last_ram_write[seg] - last_flash_write[seg] > 60000;
}

void ram_frame(void) {

	u32 now = millis();

	// handle requested edits
	if (edit_item_id != 255) {
		RamItemType item_type = get_item_type(edit_item_id & 63);
		// msb set => clear
		if (edit_item_id & 128) {
			edit_item_id &= 63;
			switch (item_type) {
			case RAM_PRESET:
				// clear floating preset
				memcpy(&cur_preset, init_params_ptr(), sizeof(Preset));
				log_ram_edit(SEG_PRESET);
				// write floating preset to preset slot
				flash_write_page(&cur_preset, sizeof(Preset), edit_item_id);
				// update state
				cur_preset_id = edit_item_id;
				ram_preset_id = edit_item_id;
				if (!sys_params.preset_aligned) {
					sys_params.preset_aligned = true;
					log_ram_edit(SEG_SYS);
				}
				break;
			case RAM_PATTERN:
				// clear floating pattern
				memset(&cur_pattern_qtr, 0, 4 * sizeof(PatternQuarter));
				log_ram_edit(SEG_PAT0);
				log_ram_edit(SEG_PAT1);
				log_ram_edit(SEG_PAT2);
				log_ram_edit(SEG_PAT3);
				// write floating pattern to pattern slot
				u8 dst_page = 4 * (edit_item_id - PATTERNS_START) + PATTERNS_START;
				for (u8 qtr = 0; qtr < 4; qtr++)
					flash_write_page(&cur_pattern_qtr, sizeof(PatternQuarter), dst_page + qtr);
				// update state
				cur_pattern_id = edit_item_id;
				ram_pattern_id = edit_item_id;
				if (!sys_params.pattern_aligned) {
					sys_params.pattern_aligned = true;
					log_ram_edit(SEG_SYS);
				}
				break;
			case RAM_SAMPLE:
				memset(&cur_sample_info, 0, sizeof(SampleInfo));
				log_ram_edit(SEG_SAMPLE);
				break;
			default:
				break;
			}
		}
		// msb not set => save
		else {
			switch (item_type) {
			case RAM_PRESET:
				// write floating preset to selected preset slot
				flash_write_page(&cur_preset, sizeof(Preset), edit_item_id);
				// make selected preset active - the fast loop will retrieve this when necessary
				cur_preset_id = edit_item_id;
				break;
			case RAM_PATTERN: {
				// write floating pattern to selected pattern slot
				u8 ptn_id = edit_item_id - PATTERNS_START;
				u8 dst_page = 4 * ptn_id + PATTERNS_START;
				for (u8 qtr = 0; qtr < 4; ++qtr)
					flash_write_page(cur_pattern_qtr_flash_ptr(qtr), sizeof(PatternQuarter), dst_page + qtr);
				// make selected pattern active in preset - the fast loop will retrieve this when necessary
				save_param_index(P_PATTERN, ptn_id);
			} break;
			default:
				// samples don't copy
				break;
			}
		}
		edit_item_id = 255; // clear edit item
	}

	// write ram items to flash (auto-save)

	for (u8 qtr = 0; qtr < 4; ++qtr) {
		if (need_flash_write(SEG_PAT0 + qtr, now)) {
			last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
			last_flash_write[SEG_PAT0 + qtr] = last_ram_write[SEG_PAT0 + qtr];
			flash_write_page(&cur_pattern_qtr[qtr], sizeof(PatternQuarter), FLOAT_PATTERN_ID + qtr);
		}
	}
	if (need_flash_write(SEG_SAMPLE, now)) {
		last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
		last_flash_write[SEG_SAMPLE] = last_ram_write[SEG_SAMPLE];
		if (ram_sample_id < NUM_SAMPLES)
			flash_write_page(&cur_sample_info, sizeof(SampleInfo), F_SAMPLES_START + ram_sample_id);
	}
	if (need_flash_write(SEG_PRESET, now) || need_flash_write(SEG_SYS, now)) {
		last_flash_write[SEG_SYS] = last_ram_write[SEG_SYS];
		last_flash_write[SEG_PRESET] = last_ram_write[SEG_PRESET];
		flash_write_page(&cur_preset, sizeof(Preset), FLOAT_PRESET_ID);
	}
}

// == UPDATE RAM == //

static bool segment_outdated(RamSegment seg) {
	return last_ram_write[seg] != last_flash_write[seg];
}

void log_ram_edit(RamSegment segment) {
	switch (segment) {
	case SEG_PRESET:
		if (sys_params.preset_aligned) {
			sys_params.preset_aligned = false;
			last_ram_write[SEG_SYS] = millis();
		}
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		if (sys_params.pattern_aligned) {
			sys_params.pattern_aligned = false;
			last_ram_write[SEG_SYS] = millis();
		}
		break;
	default:
		break;
	}
	last_ram_write[segment] = millis();
}

void update_preset_ram(bool force) {
	if (!ram_initialized)
		return;
	// already up to date
	if (!preset_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PRESET))
		return;
	// retrieve preset from flash
	clear_latch();
	memcpy(&cur_preset, preset_flash_ptr(cur_preset_id), sizeof(Preset));
	ram_preset_id = cur_preset_id;
	sys_params.preset_id = cur_preset_id;
	sys_params.preset_aligned = true;
	log_ram_edit(SEG_SYS);
}

void update_pattern_ram(bool force) {
	if (!ram_initialized)
		return;
	cur_pattern_id = param_index(P_PATTERN);
	// already up to date
	if (!pattern_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_PAT0) || segment_outdated(SEG_PAT1) || segment_outdated(SEG_PAT2)
	    || segment_outdated(SEG_PAT3))
		return;
	// retrieve pattern from flash
	for (u8 qtr = 0; qtr < 4; ++qtr)
		memcpy(&cur_pattern_qtr[qtr], pattern_qtr_flash_ptr(4 * cur_pattern_id + qtr), sizeof(PatternQuarter));
	ram_pattern_id = cur_pattern_id;
	sys_params.pattern_aligned = true;
	log_ram_edit(SEG_SYS);
}

void update_sample_ram(bool force) {
	cur_sample_id = param_index(P_SAMPLE);
	// already up to date
	if (!sample_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_SAMPLE))
		return;
	// retrieve sample info from flash
	if (cur_sample_id < NUM_SAMPLES)
		memcpy(&cur_sample_info, sample_info_flash_ptr(cur_sample_id), sizeof(SampleInfo));
	else
		memset(&cur_sample_info, 0, sizeof(SampleInfo));
	ram_sample_id = cur_sample_id;
}

// == SAVE / LOAD == //

void load_preset(u8 preset_id, bool force) {
	if (preset_id == cur_preset_id && !force)
		return;
	cur_preset_id = preset_id;
	update_preset_ram(force);
}

// rj: this function is exclusively used by open_sampler, we might want to look at using the regular loading
// implementation instead of this for open_sampler as well
void load_sample(u8 sample_id) {
	memcpy(&cur_sample_info, sample_info_flash_ptr(sample_id), sizeof(SampleInfo));
	cur_sample_id = sample_id;
	ram_sample_id = sample_id;
	cued_sample_id = 255;
}

// register the most recently touched ram item
void touch_load_item(u8 item_id) {
	recent_load_item = item_id;
}

// line up recent_load_item to be cleared during the next tick
void clear_ram_item(void) {
	edit_item_id = recent_load_item | 128;
}

void save_load_ram_item(u8 item_id) {
	// holding shift pad: save item
	if (shift_state == SS_LOAD)
		edit_item_id = item_id;
	// not holding shift pad: cue item for loading
	else {
		touch_load_item(item_id);
		cue_ram_item(item_id);
	}
}

// returns true if there's a chance this made changes to the sequencer
bool apply_cued_load_items(void) {
	bool possible_seq_changes = false;
	if (cued_preset_id != 255) {
		load_preset(cued_preset_id, true);
		cued_preset_id = 255;
		possible_seq_changes = true;
	}
	if (cued_pattern_id != 255) {
		save_param_index(P_PATTERN, cued_pattern_id);
		cued_pattern_id = 255;
		possible_seq_changes = true;
	}
	if (cued_sample_id != cur_sample_id && cued_sample_id != 255) {
		save_param_index(P_SAMPLE, cued_sample_id);
		cued_sample_id = 255;
	}
	return possible_seq_changes;
}

void cue_ram_item(u8 item_id) {
	// triggered on touch start:
	// - save what was cued into prev_cued, if they end up the same this is a double press
	// - save touched pad as cued item
	switch (get_item_type(item_id)) {
	case RAM_PRESET:
		// want to cue current preset => cancel cue
		if (item_id == cur_preset_id && sys_params.preset_aligned) {
			cued_preset_id = 255;
			return;
		}
		// load immediately
		if (!seq_playing() || item_id == cued_preset_id) {
			cur_preset_id = item_id;
			update_preset_ram(true);
			return;
		}
		// cue
		cued_preset_id = item_id;
		break;
	case RAM_PATTERN: {
		u8 pattern_id = item_id - PATTERNS_START;
		// want to cue current pattern => cancel cue
		if (pattern_id == cur_pattern_id && sys_params.pattern_aligned) {
			cued_pattern_id = 255;
			return;
		}
		// load immediately
		if (!seq_playing() || pattern_id == cued_pattern_id) {
			save_param_index(P_PATTERN, pattern_id);
			update_pattern_ram(true);
			return;
		}
		// cue
		cued_pattern_id = pattern_id;
		break;
	}
	case RAM_SAMPLE: {
		prev_cued_sample_id = cued_sample_id;
		// pressing the current sample cues turning it off
		u8 sample_id = item_id - SAMPLES_START;
		cued_sample_id = (sample_id == cur_sample_id) ? NUM_SAMPLES : sample_id;
		break;
	}
	default:
		break;
	}
}

void try_apply_cued_ram_item(u8 item_id) {
	// triggered on pad release, conditionals:
	// 1. is a ram item cued?
	// 2a. is the sequencer not playing? (save to make changes)
	// 2b. or, was the same ram item pressed twice in a row? (force change while sequencer plays)
	switch (get_item_type(item_id)) {
	case RAM_SAMPLE:
		if (cued_sample_id != 255 && (!seq_playing() || cued_sample_id == prev_cued_sample_id)) {
			save_param_index(P_SAMPLE, cued_sample_id);
			cued_sample_id = 255;
		}
		break;
	default:
		break;
	}
}

u8 draw_cued_preset_id(void) {
	if (cued_preset_id != 255)
		return fdraw_str(0, 0, F_20_BOLD, "%c%d%s->%d", I_PRESET[0], cur_preset_id + 1,
		                 sys_params.preset_aligned ? "" : ".", cued_preset_id + 1);
	else
		return 0;
}

u8 draw_preset_id(void) {
	return fdraw_str(0, 0, F_20_BOLD, I_PRESET "%d%s", cur_preset_id + 1, sys_params.preset_aligned ? "" : ".");
}

void draw_preset_name(u8 xtab) {
	char preset_name[9];
	memcpy(preset_name, cur_preset.name, 8);
	preset_name[8] = 0;
	xtab += 2;
	draw_str(xtab, 0, F_8_BOLD, preset_name);
	// category
	if (cur_preset.category > 0 && cur_preset.category < NUM_PST_CATS)
		draw_str(xtab, 8, F_8, preset_category_name[cur_preset.category]);
}

u8 draw_cued_pattern_id(bool with_arp_icon) {
	if (cued_pattern_id != 255)
		return fdraw_str(0, 16, F_20_BOLD, "%c%d%s->%d", with_arp_icon ? I_NOTES[0] : I_SEQ[0], cur_pattern_id + 1,
		                 sys_params.pattern_aligned ? "" : ".", cued_pattern_id + 1);
	else
		return 0;
}

void draw_pattern_id(bool with_arp_icon) {
	fdraw_str(0, 16, F_20_BOLD, "%c%d%s", with_arp_icon ? I_NOTES[0] : I_SEQ[0], cur_pattern_id + 1,
	          sys_params.pattern_aligned ? "" : ".");
}

void draw_sample_id(void) {
	fdraw_str(-128, 16, F_20_BOLD, cur_sample_id < NUM_SAMPLES ? I_WAVE "%d" : I_WAVE "Off", cur_sample_id + 1);
}

void draw_select_load_item(u8 item_id, bool done) {
	switch (get_item_type(item_id)) {
	case RAM_PRESET:
		if (shift_state == SS_LOAD)
			fdraw_str(0, 0, F_16_BOLD, done ? "Saved\n " I_PRESET "preset %d" : "Save\n" I_PRESET "preset %d?",
			          item_id + 1);
		else
			fdraw_str(0, 0, F_16_BOLD, done ? "Loaded\n " I_PRESET "preset %d" : "Load\n" I_PRESET "preset %d?",
			          item_id + 1);
		break;
	case RAM_PATTERN:
		if (shift_state == SS_LOAD)
			fdraw_str(0, 0, F_16_BOLD, done ? "Saved\n " I_SEQ "pattern %d" : "Save\n" I_SEQ "pattern %d?",
			          item_id - PATTERNS_START + 1);
		else
			fdraw_str(0, 0, F_16_BOLD, done ? "Loaded\n " I_SEQ "pattern %d" : "Load\n" I_SEQ "pattern %d?",
			          item_id - PATTERNS_START + 1);
		break;
	case RAM_SAMPLE:
		fdraw_str(0, 0, F_16_BOLD, done ? "ok!" : "Edit\n" I_WAVE "Sample %d?", item_id - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

void draw_clear_load_item(bool done) {
	switch (get_item_type(recent_load_item)) {
	case RAM_PRESET:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_PRESET "Preset %d" : "initialize\n" I_PRESET "Preset %d?",
		          recent_load_item + 1);
		break;
	case RAM_PATTERN:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_SEQ "Pattern %d." : "Clear\n" I_SEQ "Pattern %d?",
		          recent_load_item - PATTERNS_START + 1);
		break;
	case RAM_SAMPLE:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_WAVE "Sample %d." : "Clear\n" I_WAVE "Sample %d?",
		          recent_load_item - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

void draw_ram_save_load(void) {
	const u8 x0 = 101;
	const u8 x1 = OLED_WIDTH - 1;
	fdraw_str(x0 + 5, 1, F_8_BOLD, "%s", shift_state == SS_LOAD ? "SAVE" : "LOAD");
	vline(x0, 0, 9, 1);
	vline(x1, 0, 9, 1);
	hline(x0, 9, x1 + 1, 1);
}

u8 ui_load_led(u8 x, u8 y, u8 pulse) {
	u8 item_id = x * 8 + y;

	// all patterns low brightness
	u8 k = get_item_type(item_id) == RAM_PATTERN ? 64 : 0;

	// pulse cued load item
	if (item_id == cued_preset_id)
		k = pulse;
	if (item_id == PATTERNS_START + cued_pattern_id)
		k = pulse;
	if (item_id == SAMPLES_START + cued_sample_id)
		k = pulse;

	// full selected load item
	if (item_id == cur_preset_id)
		k = 255;
	if (item_id == PATTERNS_START + cur_pattern_id)
		k = 255;
	if (item_id == SAMPLES_START + cur_sample_id)
		k = 255;

	return k;
}