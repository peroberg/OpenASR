// license:BSD-3-Clause

#include "emu.h"
#include "asr10_boot_defs.h"

namespace asr10_boot_defs {

/**
 * 1 => top -
 * 2 => hi right |
 * 4 => lo right |
 * 8 => bot -
 * 1 => lo left |
 * 2 => hi left |
 * 4 => mid left -
 * 8 => mid right -
 * 1 => hi cen |
 * 2 => lo cen |
 * 4 => lo left /
 * 8 => hi left \
 * 1 => hi right /
 * 2 => lo right \
 * 4 => .
 * 8 => ,
 *
 * @param character
 * @return
 */
u16 ascii_to_14seg(u8 character)
{
	if (character >= 'a' && character <= 'z')
		character -= 'a' - 'A';

	switch (character)
	{
	case ' ': return 0x0000;
	case '-': return 0x00c0;
	case '_': return 0x0008;
	case '.': return 0x4000;
	case ',': return 0x8000;

	case '0': return 0x003f;
	case '1': return 0x0006;
	case '2': return 0x00db;
	case '3': return 0x00cf;
	case '4': return 0x00e6;
	case '5': return 0x00ed;
	case '6': return 0x00fd;
	case '7': return 0x0007;
	case '8': return 0x00ff;
	case '9': return 0x00ef;

		// Alphabet
	case 'A': return 0x00f7;
	case 'B': return 0x03f9;
	case 'C': return 0x0039;
	case 'D': return 0x030F;
	case 'E': return 0x00f9;
	case 'F': return 0x00f1;
	case 'G': return 0x00bd;
	case 'H': return 0x00f6;
		// Your preferred I:
		// top + both right segments + bottom
	case 'I': return 0x0309;
	case 'J': return 0x001e;
	case 'K': return 0x3030;
	case 'L': return 0x0038;
		// M uses the two upper inward diagonals.
	case 'M': return 0x1836;
		// N uses upper-left and lower-right diagonals.
	case 'N': return 0x2836;
	case 'O': return 0x003f;
	case 'P': return 0x00f3;
	case 'Q': return 0x203f;
	case 'R': return 0x20f3;
	case 'S': return 0x00ed;
		// Top bar plus the two centre vertical segments.
	case 'T': return 0x0301;
	case 'U': return 0x003e;
	case 'V': return 0x2422;
		// Lower inward diagonals.
	case 'W': return 0x2436;
		// All four diagonals.
	case 'X': return 0x3c00;
		// Upper inward diagonals plus lower centre vertical.
	case 'Y': return 0x1a00;
		// Top/bottom plus diagonal from lower-left to upper-right.
	case 'Z': return 0x1409;
	default:
		return 0x0000;
	}
}

const char *region_name(trace_region region)
{
	switch (region)
	{
	case trace_region::LOWMEM: return "ram_rom_overlay";
	case trace_region::BUS_PROBE: return "ram_chip_select_probe";
	case trace_region::HIGH_ROM_ALIAS: return "rom_high_alias";
	case trace_region::M68302_INTERNAL: return "m68302_internal_candidate";
	case trace_region::UPD72069_FDC_CANDIDATE: return "upd72069_fdc_candidate";
	case trace_region::DUART_PANEL_ASR_CANDIDATE: return "duart_panel_asr_candidate";
	case trace_region::SCSI_ASR_CANDIDATE: return "scsi_asr_candidate";
	case trace_region::ES550X_VFX_CANDIDATE: return "es5505_es5506_vfx_reference";
	case trace_region::ES5510_VFX_CANDIDATE: return "es5510_vfx_reference";
	case trace_region::DUART_VFX_CANDIDATE: return "duart_panel_vfx_reference";
	case trace_region::FDC_VFX_CANDIDATE: return "fdc_media_vfx_reference";
	case trace_region::ES5506_TS_CANDIDATE: return "es5506_ts_reference";
	case trace_region::ES5510_TS_CANDIDATE: return "es5510_ts_reference";
	}
	return "unknown";
}

const char *address_region_guess(u32 address)
{
	if (address <= 0x0fffff) return "ram_rom_overlay";
	if (address <= 0x1fffff) return "sample_ram_candidate";
	if (address >= 0x200000 && address <= 0x20007f) return "es5505_es5506_vfx_reference";
	if (address >= 0x260000 && address <= 0x2601ff) return "es5510_vfx_reference";
	if (address >= 0x280000 && address <= 0x28001f) return "duart_panel_vfx_reference";
	if (address >= 0x2c0000 && address <= 0x2c0007) return "fdc_media_vfx_reference";
	if (address >= 0x300000 && address <= 0x30007f) return "es5506_ts_reference";
	if (address >= 0x380000 && address <= 0x3801ff) return "es5510_ts_reference";
	if (address >= 0xf00000 && address <= 0xf7ffff) return "high_ram";
	if (address >= 0xf80000 && address <= 0xfbffff) return "rom_high_alias";
	if (address >= 0xfc4000 && address <= 0xfc4003) return "upd72069_fdc_candidate";
	if (address >= 0xfc4800 && address <= 0xfc481f) return "duart_panel_asr_candidate";
	if (address >= 0xfc5000 && address <= 0xfc501f) return "scsi_asr_candidate";
	if (address >= 0xfc6800 && address <= 0xfc68ff) return "m68302_internal_candidate";
	if (address >= 0xfc0000 && address <= 0xffffff) return "board_ram_or_unknown_mmio";
	return "unmapped_unknown";
}

const char *m68302_register_name(u32 address)
{
	switch (address & 0xff)
	{
	case 0x12: return "interrupt_mask_candidate";
	case 0x14: return "interrupt_pending_candidate";
	case 0x16: return "interrupt_in_service_candidate";
	case 0x18: return "interrupt_control_candidate";
	case 0x1e: return "port_a_control_candidate";
	case 0x20: return "port_a_direction_candidate";
	case 0x22: return "port_a_data_candidate";
	case 0x24: return "port_b_control_candidate";
	case 0x26: return "port_b_direction_candidate";
	case 0x28: return "port_b_data_bits0_2_control_lrclk_bit3_candidate";
	case 0x30: return "chip_select_0_base_candidate";
	case 0x32: return "chip_select_0_option_candidate";
	case 0x34: return "chip_select_1_base_candidate";
	case 0x36: return "chip_select_1_option_candidate";
	case 0x38: return "chip_select_2_base_candidate";
	case 0x3a: return "chip_select_2_option_candidate";
	case 0x3c: return "chip_select_3_base_candidate";
	case 0x3e: return "chip_select_3_option_candidate";
	case 0x48: return "timer_neighbor_0x48_candidate";
	case 0x4a: return "timer_or_clock_candidate";
	case 0x4c: return "timer_neighbor_0x4c_candidate";
	case 0x4e: return "timer_neighbor_0x4e_candidate";
	case 0x50: return "timer_1_mode_candidate";
	case 0x52: return "timer_1_reference_candidate";
	case 0x54: return "timer_neighbor_0x54_candidate";
	case 0x56: return "timer_neighbor_0x56_candidate";
	case 0x60: return "unknown_0x60_busy_bit0_candidate";
	default: return "internal_register_unknown";
	}
}

const char *fdc_state_field_name(u32 address)
{
	switch (address)
	{
	case 0x04a6: return "media_probe_field_04a6";
	case 0x04ae: return "media_probe_field_04ae";
	case 0x04b0: return "media_probe_field_04b0";
	case 0x04b4: return "media_probe_field_04b4";
	case 0x04b6: return "media_probe_field_04b6";
	case 0x04c4: return "media_probe_field_04c4";
	case 0x04c6: return "media_probe_field_04c6";
	case 0x04d6: return "media_probe_field_04d6";
	case 0x04e6: return "media_probe_field_04e6";
	default: return "media_probe_field_unknown";
	}
}

bool is_fdc_state_field(u32 address)
{
	switch (address)
	{
	case 0x04a6:
	case 0x04ae:
	case 0x04b0:
	case 0x04b4:
	case 0x04b6:
	case 0x04c4:
	case 0x04c6:
	case 0x04d6:
	case 0x04e6:
		return true;
	default:
		return false;
	}
}

} // namespace asr10_boot_defs
