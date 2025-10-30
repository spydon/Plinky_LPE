#pragma once
#include "utils.h"

// this module manages 512KB (256 * 2048 bytes) of onboard flash
//
// this flash stores:
// - 32 presets
// - 96 sequencer pattern quarters
// - 8 SampleInfo's
// - calibration data
//
// each of the presets, pattern quarters and sampleinfos stores a copy of the system parameters

extern bool flash_busy;
const Preset* preset_flash_ptr(u8 preset_id); // only for web editor

// main

void check_bootloader_flash(void);
void init_flash();

// read/write

void flash_read_preset(u8 preset_id);
bool flash_read_floating_preset(void);
void flash_write_preset(u8 preset_id);
void flash_write_floating_preset(void);
bool flash_read_floating_pattern(void);
void flash_read_pattern(u8 pattern_id);
void flash_write_pattern(u8 pattern_id);
void flash_write_floating_quarter(u8 quarter);
void flash_read_sample_info(u8 sample_id);
void flash_write_sample_info(u8 sample_id);
FlashCalibType flash_read_calib(void);
void flash_write_calib(FlashCalibType flash_calib_type);