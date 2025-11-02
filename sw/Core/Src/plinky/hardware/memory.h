#pragma once
#include "utils.h"

// this module manages the preset, pattern quarters, SampleInfo and SysParams that are curently loaded into ram
// it manages retrieving data from, and writing data to onboard flash when needed

// ram ids
extern u8 ram_preset_id; // only for web editor

// ram contents
extern SysParams sys_params;
extern Preset cur_preset;
extern PatternQuarter cur_pattern_qtr[4];
extern SampleInfo cur_sample_info;

// utils
u32 get_sample_address(void);
const Preset* preset_flash_ptr(u8 preset_id); // only for web editor
void set_sys_param(SysParam param, u16 value);

// get ram state
bool preset_outdated(void);  // only for sequencer
bool pattern_outdated(void); // only for sequencer

// init
void check_bootloader_flash(void);
void init_memory(void);

// main
void memory_frame(void);
void revert_presets(void);

// update ram
void log_ram_edit(MemSegment segment);
void update_preset_ram(void);
void update_pattern_ram(void);
void update_sample_ram(void);

// save/load
void load_preset(u8 preset_id); // only for web-editor
void apply_cued_mem_items(void);
void cue_mem_item(u8 item_id);

// ui
void long_press_load_item(u8 item_id);
void clear_mem_item(void);

// calib
FlashCalibType flash_read_calib(void);
void flash_write_calib(FlashCalibType flash_calib_type);

// visuals
u8 draw_cued_preset_id(void);
u8 draw_high_note(void);
u8 draw_preset_id(void);
u8 draw_cued_pattern_id(bool with_arp_icon);
void draw_pattern_id(bool with_arp_icon);
void draw_preset_name(u8 xtab);
void draw_sample_id(void);
void draw_save_load_item(u8 item_id, bool done);
void draw_clear_item(bool done);
void draw_ui_load_label(void);

u8 ui_load_led(u8 x, u8 y, u8 pulse);