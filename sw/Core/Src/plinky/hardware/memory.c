#include "memory.h"
#include "gfx/gfx.h"
#include "synth/audio.h"
#include "synth/params.h"
#include "ui/shift_states.h"

// cleanup
#include "adc_dac.h"
#include "hardware/codec.h"
#include "synth/sequencer.h"
#include "synth/strings.h"
#include "touchstrips.h"
// -- cleanup

#define NUM_RAM_ITEMS (NUM_PRESETS + NUM_PATTERNS + NUM_SAMPLES)

// == TYPEDEFS == //

typedef enum MemItemType {
	MEM_PRESET,
	MEM_PATTERN,
	MEM_SAMPLE,
} MemItemType;

typedef struct PageFooter {
	u8 idx; // preset 0-31, pattern (quarters!) 32-127, sample 128-136, blank=0xff
	u8 version;
	u16 crc;
	u32 seq;
} PageFooter;

typedef struct FlashPage {
	union {
		u8 raw[FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter)];
		Preset preset;
		PatternQuarter pattern_quarter;
		SampleInfo sample_info;
	};
	SysParams sys_params;
	PageFooter footer;
} FlashPage;
static_assert(sizeof(FlashPage) == 2048, "?");
static_assert(sizeof(Preset) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
static_assert(sizeof(PatternQuarter) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");
static_assert(sizeof(SampleInfo) + sizeof(SysParams) + sizeof(PageFooter) <= 2048, "?");

// == DEFINES == //

#define FLASH_ADDR_256 (0x08000000 + 256 * FLASH_PAGE_SIZE)
#define FLASH_PAGE_PTR(page) ((FlashPage*)(FLASH_ADDR_256 + (page) * FLASH_PAGE_SIZE))
#define FOOTER_VERSION 2
#define CALIB_PAGE 255
#define MAGIC ((u64)0xf00dcafe473ff02a)

// == FLASH VARS == //

static u8 lastest_flash_page_id[NUM_FLASH_ITEMS] = {};
static u8 next_free_flash_page = 0;
static u32 next_footer_seq = 0;
static bool flash_busy = false;

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
Preset cur_preset;                 // floating preset, the one we edit and use for sound generation
PatternQuarter cur_pattern_qtr[4]; // floating pattern, the one we edit and use for recording/playing
SampleInfo cur_sample_info;

// item to change to
static u8 cued_preset_id = 255;
static u8 cued_pattern_id = 255;
static u8 cued_sample_id = 255;

// history of above, used to check double press on the same item
static u8 prev_cued_sample_id = 255;

static u8 edit_item_id = 255;   // ram item to edit | msb unset => save, msb set => clear
static u8 recent_load_item = 0; // the most recently touched load item

static u32 last_ram_write[NUM_MEM_SEGMENTS];
static u32 last_flash_write[NUM_MEM_SEGMENTS];

// == UTILS == //

static MemItemType get_item_type(u8 item_id) {
	return item_id < PATTERNS_START  ? MEM_PRESET
	       : item_id < SAMPLES_START ? MEM_PATTERN
	       : item_id < NUM_RAM_ITEMS ? MEM_SAMPLE
	                                 : NUM_RAM_ITEMS;
}

static u16 compute_hash(const void* data, int nbytes) {
	u16 hash = 123;
	const u8* src = (const u8*)data;
	for (int i = 0; i < nbytes; ++i)
		hash = hash * 23 + *src++;
	return hash;
}

const Preset* preset_flash_ptr(u8 preset_id) {
	if (preset_id >= NUM_PRESETS)
		return init_params_ptr();
	FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[preset_id]);
	if (fp->footer.idx != preset_id || fp->footer.version != FOOTER_VERSION)
		return init_params_ptr();
	return (Preset*)fp;
}

// == FLASH == //

void check_bootloader_flash(void) {
	u8 count = 0;
	u32* rb32 = (u32*)reverb_ram_buf;
	u32 magic = rb32[64];
	char* rb = (char*)reverb_ram_buf;
	for (; count < 64; ++count)
		if (rb[count] != 1)
			break;
	DebugLog("bootloader left %d ones for us magic is %08x\r\n", count, magic);
	const u32* app_base = (const u32*)delay_ram_buf;

	if (count != 64 / 4 || magic != 0xa738ea75) {
		return;
	}
	char buf[32];
	// checksum!
	u32 checksum = 0;
	for (u32 i = 0; i < 65536 / 4; ++i) {
		checksum = checksum * 23 + ((u32*)delay_ram_buf)[i];
	}
	if (checksum != GOLDEN_CHECKSUM) {
		DebugLog("bootloader checksum failed %08x != %08x\r\n", checksum, GOLDEN_CHECKSUM);
		oled_clear();
		draw_str(0, 0, F_8, "bad bootloader crc");
		snprintf(buf, sizeof(buf), "%08x vs %08x", (unsigned int)checksum, (unsigned int)GOLDEN_CHECKSUM);
		draw_str(0, 8, F_8, buf);
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	oled_clear();
	snprintf(buf, sizeof(buf), "%08x %d", (unsigned int)magic, count);
	draw_str(0, 0, F_16, buf);
	snprintf(buf, sizeof(buf), "%08x %08x", (unsigned int)app_base[0], (unsigned int)app_base[1]);
	draw_str(0, 16, F_12, buf);
	oled_flip();

	rb32[64]++; // clear the magic

	DebugLog("bootloader app base is %08x %08x\r\n", (unsigned int)app_base[0], (unsigned int)app_base[1]);

	/*
	 * We refuse to program the first word of the app until the upload is marked
	 * complete by the host.  So if it's not 0xffffffff, we should try booting it.
	 */
	if (app_base[0] == 0xffffffff || app_base[0] == 0) {
		HAL_Delay(10000);
		return;
	}

	// first word is stack base - needs to be in RAM region and word-aligned
	if ((app_base[0] & 0xff000003) != 0x20000000) {
		HAL_Delay(10000);
		return;
	}

	/*
	 * The second word of the app is the entrypoint; it must point within the
	 * flash area (or we have a bad flash).
	 */
	if (app_base[1] < 0x08000000 || app_base[1] >= 0x08010000) {
		HAL_Delay(10000);
		return;
	}
	DebugLog("FLASHING BOOTLOADER! DO NOT RESET\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "FLASHING\nBOOTLOADER");
	char verbuf[5] = {};
	memcpy(verbuf, (void*)(delay_ram_buf + 65536 - 4), 4);
	draw_str(0, 24, F_8, verbuf);

	oled_flip();
	HAL_FLASH_Unlock();
	FLASH_EraseInitTypeDef EraseInitStruct;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.Page = 0;
	EraseInitStruct.NbPages = 65536 / 2048;
	u32 SECTORError = 0;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
		DebugLog("BOOTLOADER flash erase error %d\r\n", SECTORError);
		oled_clear();
		draw_str(0, 0, F_16_BOLD, "BOOTLOADER\nERASE ERROR");
		oled_flip();
		HAL_Delay(10000);
		return;
	}
	DebugLog("BOOTLOADER flash erased ok!\r\n");

	__HAL_FLASH_DATA_CACHE_DISABLE();
	__HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
	__HAL_FLASH_DATA_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_RESET();
	__HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
	__HAL_FLASH_DATA_CACHE_ENABLE();
	u64* s = (u64*)delay_ram_buf;
	volatile u64* d = (volatile u64*)0x08000000;
	u32 size_bytes = 65536;
	for (; size_bytes > 0; size_bytes -= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
	}
	HAL_FLASH_Lock();
	DebugLog("BOOTLOADER has been flashed!\r\n");
	oled_clear();
	draw_str(0, 0, F_12_BOLD, "BOOTLOADER\nFLASHED OK!");
	draw_str(0, 24, F_8, verbuf);
	oled_flip();
	HAL_Delay(3000);
}

// writing flash

static void flash_erase_page(u8 page) {
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	SET_BIT(FLASH->CR, FLASH_CR_BKER); // bank 2
	MODIFY_REG(FLASH->CR, FLASH_CR_PNB, ((page & 0xFFU) << FLASH_CR_PNB_Pos));
	SET_BIT(FLASH->CR, FLASH_CR_PER);
	SET_BIT(FLASH->CR, FLASH_CR_STRT);
	FLASH_WaitForLastOperation((u32)FLASH_TIMEOUT_VALUE);
	CLEAR_BIT(FLASH->CR, (FLASH_CR_PER | FLASH_CR_PNB));
}

static void flash_write_block(void* dst, const void* src, int size) {
	u64* s = (u64*)src;
	volatile u64* d = (volatile u64*)dst;
	while (size >= 8) {
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(size_t)(d++), *s++);
		size -= 8;
	}
}

static void flash_write_page(const void* src, u32 size, u8 item_id) {
	flash_busy = true;
	HAL_FLASH_Unlock();
	bool in_use;
	do {
		FlashPage* p = FLASH_PAGE_PTR(next_free_flash_page);
		in_use = next_free_flash_page == 255;
		in_use |= (p->footer.idx < NUM_FLASH_ITEMS && lastest_flash_page_id[p->footer.idx] == next_free_flash_page);
		if (in_use)
			++next_free_flash_page;
	} while (in_use);
	flash_erase_page(next_free_flash_page);
	u8 flash_page = next_free_flash_page++;
	u8* dst = (u8*)(FLASH_ADDR_256 + flash_page * FLASH_PAGE_SIZE);
	flash_write_block(dst, src, size);
	flash_write_block(dst + FLASH_PAGE_SIZE - sizeof(SysParams) - sizeof(PageFooter), &sys_params, sizeof(SysParams));
	PageFooter footer;
	footer.idx = item_id;
	footer.seq = next_footer_seq++;
	footer.version = FOOTER_VERSION;
	footer.crc = compute_hash(dst, 2040);
	flash_write_block(dst + 2040, &footer, 8);
	HAL_FLASH_Lock();
	lastest_flash_page_id[item_id] = flash_page;
	flash_busy = false;
}

// flash read/write helpers

void flash_read_preset(u8 preset_id) {
	const Preset* src = init_params_ptr();
	if (preset_id < NUM_PRESETS) {
		FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[preset_id]);
		if (fp->footer.idx == preset_id && fp->footer.version == FOOTER_VERSION)
			src = (Preset*)fp;
	}
	memcpy(&cur_preset, src, sizeof(Preset));
}

static bool flash_read_floating_preset(void) {
	FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[FLOAT_PRESET_ID]);
	if (fp->footer.idx != FLOAT_PRESET_ID || fp->footer.version != FOOTER_VERSION)
		return false;
	memcpy(&cur_preset, (Preset*)fp, sizeof(Preset));
	return true;
}

void flash_write_preset(u8 preset_id) {
	if (preset_id < NUM_PRESETS)
		flash_write_page(&cur_preset, sizeof(Preset), preset_id);
}

static void flash_write_floating_preset(void) {
	flash_write_page(&cur_preset, sizeof(Preset), FLOAT_PRESET_ID);
}

static void flash_read_pattern(u8 pattern_id) {
	if (pattern_id >= NUM_PATTERNS)
		return;
	u8 base_id = 4 * pattern_id;
	for (u8 qtr = 0; qtr < 4; ++qtr) {
		const PatternQuarter* src = (PatternQuarter*)zero;
		FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[PATTERNS_START + base_id + qtr]);
		if (fp->footer.idx == PATTERNS_START + base_id + qtr && fp->footer.version == FOOTER_VERSION)
			src = (PatternQuarter*)fp;
		memcpy(&cur_pattern_qtr[qtr], src, sizeof(PatternQuarter));
	}
}

static bool flash_read_floating_pattern(void) {
	FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[FLOAT_PATTERN_ID]);
	if (fp->footer.idx != FLOAT_PATTERN_ID || fp->footer.version != FOOTER_VERSION)
		return false;
	for (u8 qtr = 0; qtr < 4; ++qtr) {
		fp = FLASH_PAGE_PTR(lastest_flash_page_id[FLOAT_PATTERN_ID + qtr]);
		memcpy(&cur_pattern_qtr[qtr], (PatternQuarter*)fp, sizeof(PatternQuarter));
	}
	return true;
}

static void flash_write_pattern(u8 pattern_id) {
	if (pattern_id < NUM_PATTERNS) {
		u8 base_id = PATTERNS_START + 4 * pattern_id;
		for (u8 qtr = 0; qtr < 4; ++qtr)
			flash_write_page(&cur_pattern_qtr[qtr], sizeof(PatternQuarter), base_id + qtr);
	}
}

static void flash_write_floating_quarter(u8 quarter) {
	flash_write_page(&cur_pattern_qtr[quarter], sizeof(PatternQuarter), FLOAT_PATTERN_ID + quarter);
}

static void flash_read_sample_info(u8 sample_id) {
	const SampleInfo* src = (SampleInfo*)zero;
	if (sample_id < NUM_SAMPLES) {
		FlashPage* fp = FLASH_PAGE_PTR(lastest_flash_page_id[F_SAMPLES_START + sample_id]);
		if (fp->footer.idx == F_SAMPLES_START + sample_id && fp->footer.version == FOOTER_VERSION)
			src = (SampleInfo*)fp;
	}
	memcpy(&cur_sample_info, src, sizeof(SampleInfo));
}

static void flash_write_sample_info(u8 sample_id) {
	if (sample_id < NUM_SAMPLES)
		flash_write_page(&cur_sample_info, sizeof(SampleInfo), F_SAMPLES_START + sample_id);
}

FlashCalibType flash_read_calib(void) {
	FlashCalibType flash_calib_type = FLASH_CALIB_NONE;
	volatile u64* flash = (volatile u64*)(FLASH_ADDR_256 + CALIB_PAGE * 2048);
	if (!(flash[0] == MAGIC && flash[255] == ~MAGIC))
		return FLASH_CALIB_NONE;
	// read touch calibration data
	volatile u64* src = flash + 1;
	if (*src != ~(u64)(0)) {
		flash_calib_type |= FLASH_CALIB_TOUCH;
		memcpy(touch_calib_ptr(), (u64*)src, sizeof(TouchCalibData) * NUM_TOUCH_READINGS);
	}
	// read adc/dac calibration data
	src += sizeof(TouchCalibData) * NUM_TOUCH_READINGS / 8;
	if (*src != ~(u64)(0)) {
		flash_calib_type |= FLASH_CALIB_ADC_DAC;
		memcpy(adc_dac_calib_ptr(), (u64*)src, sizeof(ADC_DAC_Calib) * NUM_ADC_DAC_ITEMS);
	}
	return flash_calib_type;
}

void flash_write_calib(FlashCalibType flash_calib_type) {
	HAL_FLASH_Unlock();
	flash_erase_page(CALIB_PAGE);
	u64* flash = (u64*)(FLASH_ADDR_256 + CALIB_PAGE * 2048);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)flash, MAGIC);
	// write touch calibration data
	u64* dst = flash + 1;
	if (flash_calib_type & FLASH_CALIB_TOUCH)
		flash_write_block(dst, touch_calib_ptr(), sizeof(TouchCalibData) * NUM_TOUCH_READINGS);
	// write adc/dac calibration data
	dst += (sizeof(TouchCalibData) * NUM_TOUCH_READINGS + 7) / 8;
	if (flash_calib_type & FLASH_CALIB_ADC_DAC)
		flash_write_block(dst, adc_dac_calib_ptr(), sizeof(ADC_DAC_Calib) * NUM_ADC_DAC_ITEMS);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, (u32)(flash + 255), ~MAGIC);
	HAL_FLASH_Lock();
}

// == RAM == //

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

void init_memory(void) {
	// init flash

	u8 dummy_page = 0;
	memset(lastest_flash_page_id, dummy_page, sizeof(lastest_flash_page_id));
	u32 highest_seq = 0;
	next_free_flash_page = 0;
	memset(&sys_params, 0, sizeof(sys_params));
	// scan for the latest page for each object
	for (u8 page = 0; page < 255; ++page) {
		FlashPage* p = FLASH_PAGE_PTR(page);
		u8 i = p->footer.idx;
		if (i >= NUM_FLASH_ITEMS)
			continue; // skip blank
		if (p->footer.version < FOOTER_VERSION)
			continue; // skip old
		u16 check = compute_hash(p, 2040);
		if (check != p->footer.crc) {
			DebugLog("flash page %d has a bad crc!\r\n", page);
			if (page == dummy_page) {
				// shit, the dummy page is dead! move to a different dummy
				for (u8 i = 0; i < NUM_FLASH_ITEMS; ++i)
					if (lastest_flash_page_id[i] == dummy_page)
						lastest_flash_page_id[i]++;
				dummy_page++;
			}
			continue;
		}
		if (p->footer.seq > highest_seq) {
			highest_seq = p->footer.seq;
			next_free_flash_page = page + 1;
			sys_params = p->sys_params;
		}
		FlashPage* existing = FLASH_PAGE_PTR(lastest_flash_page_id[i]);
		if (existing->footer.idx != i || p->footer.seq > existing->footer.seq || existing->footer.version < 2)
			lastest_flash_page_id[i] = page;
	}
	next_footer_seq = highest_seq + 1;

	// init ram

	cued_preset_id = -1;
	cued_pattern_id = -1;
	cued_sample_id = -1;
	ram_pattern_id = -1;
	ram_sample_id = -1;
	ram_preset_id = -1;
	// relocate the first preset and pattern into ram
	edit_item_id = 255;
	for (u8 i = 0; i < NUM_MEM_SEGMENTS; ++i) {
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
		log_ram_edit(SEG_SYS_PARAMS);
		HAL_Delay(500);
		oled_clear();
		draw_str_ctr(0, font, "updated");
		draw_str_ctr(16, font, "system settings");
		oled_flip();
		HAL_Delay(2000);
	}
	codec_update_volume();
	cur_preset_id = sys_params.preset_id;
	// load floating preset
	if (flash_read_floating_preset()) {
		ram_preset_id = sys_params.preset_id;
		// load floating pattern
		flash_read_floating_pattern();
		cur_pattern_id = param_index_unmod(P_PATTERN);
		ram_pattern_id = cur_pattern_id;
	}

	recent_load_item = cur_preset_id;
	ram_initialized = true;
}

static bool need_flash_write(MemSegment segment, u32 now) {
	// segment up to date => no write
	if (last_ram_write[segment] == last_flash_write[segment])
		return false;

	// a ram item (preset, pattern, sample) being outdated means the user has requested to load a different one, but
	// that load has not happened yet because the current item hasn't finished writing to flash - we need to write it to
	// flash immediately so the new item can be loaded
	switch (segment) {
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
	case SEG_SAMPLE_INFO:
		if (sample_outdated())
			return true;
		break;
	default:
		break;
	}

	// if our ram item was not outdated, that means we changed something small (a parameter, the contents of a step,
	// etc) we try to wait for at least 5 seconds after the most recent edit before we write them to flash
	if (now - last_ram_write[segment] > 5000)
		return true;
	// but if we are out of date for a minute or more, we write immediately
	return last_ram_write[segment] - last_flash_write[segment] > 60000;
}

void memory_frame(void) {

	u32 now = millis();

	// handle requested edits
	if (edit_item_id != 255) {
		MemItemType item_type = get_item_type(edit_item_id & 63);
		// msb set => clear
		if (edit_item_id & 128) {
			edit_item_id &= 63;
			switch (item_type) {
			case MEM_PRESET:
				// clear floating preset
				memcpy(&cur_preset, init_params_ptr(), sizeof(Preset));
				log_ram_edit(SEG_PRESET);
				// write floating preset to preset slot
				flash_write_preset(edit_item_id);
				// update state
				cur_preset_id = edit_item_id;
				ram_preset_id = edit_item_id;
				if (!sys_params.preset_aligned) {
					sys_params.preset_aligned = true;
					log_ram_edit(SEG_SYS_PARAMS);
				}
				break;
			case MEM_PATTERN:
				// clear floating pattern
				memset(&cur_pattern_qtr, 0, 4 * sizeof(PatternQuarter));
				log_ram_edit(SEG_PAT0);
				log_ram_edit(SEG_PAT1);
				log_ram_edit(SEG_PAT2);
				log_ram_edit(SEG_PAT3);
				// write floating pattern to pattern slot
				flash_write_pattern(edit_item_id - PATTERNS_START);
				// update state
				cur_pattern_id = edit_item_id;
				ram_pattern_id = edit_item_id;
				if (!sys_params.pattern_aligned) {
					sys_params.pattern_aligned = true;
					log_ram_edit(SEG_SYS_PARAMS);
				}
				break;
			case MEM_SAMPLE:
				memset(&cur_sample_info, 0, sizeof(SampleInfo));
				log_ram_edit(SEG_SAMPLE_INFO);
				break;
			default:
				break;
			}
		}
		// msb not set => save
		else {
			switch (item_type) {
			case MEM_PRESET:
				// write floating preset to selected preset slot
				flash_write_preset(edit_item_id);
				// make selected preset active - the fast loop will retrieve this when necessary
				cur_preset_id = edit_item_id;
				break;
			case MEM_PATTERN: {
				// write floating pattern to selected pattern slot
				u8 ptn_id = edit_item_id - PATTERNS_START;
				flash_write_pattern(ptn_id);
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
			last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
			last_flash_write[SEG_PAT0 + qtr] = last_ram_write[SEG_PAT0 + qtr];
			flash_write_floating_quarter(qtr);
		}
	}
	if (need_flash_write(SEG_SAMPLE_INFO, now)) {
		last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
		last_flash_write[SEG_SAMPLE_INFO] = last_ram_write[SEG_SAMPLE_INFO];
		flash_write_sample_info(ram_sample_id);
	}
	if (need_flash_write(SEG_PRESET, now) || need_flash_write(SEG_SYS_PARAMS, now)) {
		last_flash_write[SEG_SYS_PARAMS] = last_ram_write[SEG_SYS_PARAMS];
		last_flash_write[SEG_PRESET] = last_ram_write[SEG_PRESET];
		flash_write_floating_preset();
	}
}

// == UPDATE RAM == //

static bool segment_outdated(MemSegment segment) {
	return last_ram_write[segment] != last_flash_write[segment];
}

void log_ram_edit(MemSegment segment) {
	switch (segment) {
	case SEG_PRESET:
		if (sys_params.preset_aligned) {
			sys_params.preset_aligned = false;
			last_ram_write[SEG_SYS_PARAMS] = millis();
		}
		break;
	case SEG_PAT0:
	case SEG_PAT1:
	case SEG_PAT2:
	case SEG_PAT3:
		if (sys_params.pattern_aligned) {
			sys_params.pattern_aligned = false;
			last_ram_write[SEG_SYS_PARAMS] = millis();
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
	flash_read_preset(cur_preset_id);
	ram_preset_id = cur_preset_id;
	sys_params.preset_id = cur_preset_id;
	sys_params.preset_aligned = true;
	log_ram_edit(SEG_SYS_PARAMS);
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
	flash_read_pattern(cur_pattern_id);
	ram_pattern_id = cur_pattern_id;
	sys_params.pattern_aligned = true;
	log_ram_edit(SEG_SYS_PARAMS);
}

void update_sample_ram(bool force) {
	cur_sample_id = param_index(P_SAMPLE);
	// already up to date
	if (!sample_outdated() && !force)
		return;
	// flash is not ready
	if (flash_busy || segment_outdated(SEG_SAMPLE_INFO))
		return;
	// retrieve sample info from flash
	flash_read_sample_info(cur_sample_id);
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
	flash_read_sample_info(sample_id);
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
	case MEM_PRESET:
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
	case MEM_PATTERN: {
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
	case MEM_SAMPLE: {
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
	case MEM_SAMPLE:
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
	case MEM_PRESET:
		if (shift_state == SS_LOAD)
			fdraw_str(0, 0, F_16_BOLD, done ? "Saved\n " I_PRESET "preset %d" : "Save\n" I_PRESET "preset %d?",
			          item_id + 1);
		else
			fdraw_str(0, 0, F_16_BOLD, done ? "Loaded\n " I_PRESET "preset %d" : "Load\n" I_PRESET "preset %d?",
			          item_id + 1);
		break;
	case MEM_PATTERN:
		if (shift_state == SS_LOAD)
			fdraw_str(0, 0, F_16_BOLD, done ? "Saved\n " I_SEQ "pattern %d" : "Save\n" I_SEQ "pattern %d?",
			          item_id - PATTERNS_START + 1);
		else
			fdraw_str(0, 0, F_16_BOLD, done ? "Loaded\n " I_SEQ "pattern %d" : "Load\n" I_SEQ "pattern %d?",
			          item_id - PATTERNS_START + 1);
		break;
	case MEM_SAMPLE:
		fdraw_str(0, 0, F_16_BOLD, done ? "ok!" : "Edit\n" I_WAVE "Sample %d?", item_id - SAMPLES_START + 1);
		break;
	default:
		break;
	}
}

void draw_clear_load_item(bool done) {
	switch (get_item_type(recent_load_item)) {
	case MEM_PRESET:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_PRESET "Preset %d" : "initialize\n" I_PRESET "Preset %d?",
		          recent_load_item + 1);
		break;
	case MEM_PATTERN:
		fdraw_str(0, 0, F_16_BOLD, done ? "cleared\n" I_SEQ "Pattern %d." : "Clear\n" I_SEQ "Pattern %d?",
		          recent_load_item - PATTERNS_START + 1);
		break;
	case MEM_SAMPLE:
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
	u8 k = get_item_type(item_id) == MEM_PATTERN ? 64 : 0;

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