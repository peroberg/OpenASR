// license:BSD-3-Clause

#ifndef MAME_ENSONIQ_ASR10_BOOT_DEFS_H
#define MAME_ENSONIQ_ASR10_BOOT_DEFS_H

#include "emu.h"

namespace asr10_boot_defs {

enum class trace_region : u8
{
	LOWMEM,
	BUS_PROBE,
	HIGH_ROM_ALIAS,
	M68302_INTERNAL,
	UPD72069_FDC_CANDIDATE,
	DUART_PANEL_ASR_CANDIDATE,
	SCSI_ASR_CANDIDATE
};

struct trace_slot
{
	u32 pc = 0xffffffffU;
	u32 address = 0xffffffffU;
	u32 repeat_count = 0;
	u16 data = 0;
	u16 mem_mask = 0;
	u16 last_write = 0;
	trace_region region = trace_region::LOWMEM;
	bool write = false;
};

u16 ascii_to_14seg(u8 character);
const char *address_region_guess(u32 address);
const char *region_name(trace_region region);
const char *m68302_register_name(u32 address);
const char *fdc_state_field_name(u32 address);
bool is_fdc_state_field(u32 address);

} // namespace asr10_boot_defs

#endif // MAME_ENSONIQ_ASR10_BOOT_DEFS_H
