// license:BSD-3-Clause
// copyright-holders:

/***************************************************************************
 *
 *     Ensoniq ASR-10 boot harness
 *
 *	Experimental upstream-MAME-oriented boot tracer for the ASR-10 1.5B ROM.
 *	This is not a full ASR-10 driver and not an emulator contract.
 *
 *	Canonical project references:
 *	- docs/hardware-identity.md
 *	- docs/address-model.md
 *	- docs/mame-asr10-boot-harness.md
 *
 *	Current conservative model:
 *	- ASR-10 1.5B EPROM pair
 *	- 68000-compatible big-endian boot code on likely MC68302-family board
 *	- $fc68xx is board/MMIO
 *	- $fc6830 is control_register_candidate
 *	- high-runtime alias behavior is an experiment, not proven hardware
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  68302 CPU address space
 *   $0000f2/$0000f4    initial BAR/SCR
 *   $fc6000-$fc6fff    68302 internal DPRAM/register block candidate
 *   ?                  ES5506 host regs, small window
 *   ?                  ES5510 host regs, ~0x200 window
 *   ?                  floppy controller
 *   ?                  SCSI controller
 *   ?                  frontpanel/display/keyscan/glue
 *   ?                  sample RAM window / main sample RAM
 *   ?                  work RAM / high RAM
 *   ?                  ROM / high alias
 *
 **************************************************************************/

#include "emu.h"

#include "cpu/m68000/m68000.h"
#include "machine/upd765.h"

#include <algorithm>
#include <array>


namespace {

class asr10_boot_state : public driver_device
{
public:
	asr10_boot_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_fdc(*this, "fdc")
		, m_rom(*this, "maincpu")
	{
	}

	void asr10_boot(machine_config &config) ATTR_COLD;

private:
	static constexpr u32 ROM_MASK = 0x0003ffff;
	static constexpr u32 LOWMEM_WORDS = 0x00100000 / 2;
	static constexpr u32 LOWMEM_LOG_END = 0x00000200;
	static constexpr u32 CONTROL_REGISTER_CANDIDATE = 0x00fc6830;
	static constexpr u32 PROBE_OR_ALIAS_REGION_COUNT = 4;
	static constexpr u32 TRACE_SLOT_COUNT = 64;
	static constexpr u32 PANEL_TEXT_LENGTH = 64;
	static constexpr u32 MAX_PC_POLLS = 4'000'000;

	// True forces the ROM into the SCSI-installed/searching path;
	// False lets the ROM fall through to "PLEASE INSERT DISK"
	static constexpr bool ASR10_FAKE_SCSI_INSTALLED = false;
	static constexpr u8 ASR10_DUART_INPUT_CHANGE_STUB = 0x00;
	static constexpr bool ASR10_PULSE_DUART_INPUT_BIT4_AT_SEMANTIC_READER = false;

	enum class trace_region : u8
	{
		LOWMEM,
		BUS_PROBE,
		HIGH_ROM_ALIAS,
		M68302_INTERNAL,
		UPD72069_FDC_CANDIDATE,
		DUART_PANEL_ASR_CANDIDATE,
		SCSI_ASR_CANDIDATE,
		ES550X_VFX_CANDIDATE,
		ES5510_VFX_CANDIDATE,
		DUART_VFX_CANDIDATE,
		FDC_VFX_CANDIDATE,
		ES5506_TS_CANDIDATE,
		ES5510_TS_CANDIDATE
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

	required_device<m68000_device> m_maincpu;
	required_device<upd72069_device> m_fdc;
	required_memory_region m_rom;

	emu_timer *m_pc_timer = nullptr;
	std::unique_ptr<u16[]> m_lowmem_shadow;
	u16 m_probe_or_alias_region_shadow[PROBE_OR_ALIAS_REGION_COUNT][2]{};
	u16 m_m68302_internal_shadow[0x80]{};
	u8 m_fdc_last_aux_command = 0;
	u8 m_fdc_last_fifo_read = 0;
	u8 m_fdc_last_fifo_write = 0;
	u64 m_fdc_trace_sequence = 0;
	u32 m_fdc_transaction = 0;
	u32 m_fdc_transaction_access = 0;
	u8 m_fdc_fifo_transaction_reads = 0;
	u8 m_fdc_lowmem_watch = 0;
	u16 m_duart_panel_asr_shadow[0x10]{};
	u16 m_scsi_asr_shadow[0x10]{};
	u16 m_es550x_vfx_shadow[0x40]{};
	u16 m_es5510_vfx_shadow[0x100]{};
	u16 m_duart_vfx_shadow[0x10]{};
	u16 m_fdc_vfx_shadow[4]{};
	u16 m_es5506_ts_shadow[0x40]{};
	u16 m_es5510_ts_shadow[0x100]{};
	std::array<trace_slot, TRACE_SLOT_COUNT> m_trace_slots{};
	char m_panel_text[PANEL_TEXT_LENGTH]{};
	u32 m_panel_text_length = 0;
	bool m_high_alias_enabled = false;
	bool m_lowmem_overlay_enabled = false;
	bool m_seen_insert_disk_prompt = false;
	u64 m_pc_poll_count = 0;
	u32 m_last_pc = 0xffffffffU;
	u32 m_last_distinct_pc = 0xffffffffU;
	u32 m_pc_repeat_count = 0;
	u32 m_pc_change_count = 0;
	u32 m_dispatcher_hits = 0;
	u32 m_context_hits[20]{};

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void mem_map(address_map &map) ATTR_COLD;

	u16 low_rom_or_lowmem_r(offs_t offset, u16 mem_mask = ~0);
	void lowmem_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 probe_or_alias_region_408000_r(offs_t offset, u16 mem_mask = ~0);
	void probe_or_alias_region_408000_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 probe_or_alias_region_808000_r(offs_t offset, u16 mem_mask = ~0);
	void probe_or_alias_region_808000_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 probe_or_alias_region_c08000_r(offs_t offset, u16 mem_mask = ~0);
	void probe_or_alias_region_c08000_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 high_alias_r(offs_t offset, u16 mem_mask = ~0);
	void high_alias_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 m68302_internal_r(offs_t offset, u16 mem_mask = ~0);
	void m68302_internal_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 upd72069_fdc_r(offs_t offset, u16 mem_mask = ~0);
	void upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 scsi_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void scsi_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es550x_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es550x_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5510_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5510_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 duart_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void duart_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 fdc_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void fdc_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5506_ts_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5506_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5510_ts_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5510_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(pc_poll);

	bool probe_or_alias_region_index(u32 address, u32 &index, u32 &word_index) const;
	u16 probe_or_alias_region_r_at(u32 base, offs_t offset, u16 mem_mask);
	void probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask);
	u16 candidate_r(u32 base, offs_t offset, u16 mem_mask, u16 *shadow, u32 words, trace_region region);
	void candidate_w(u32 base, offs_t offset, u16 data, u16 mem_mask, u16 *shadow, u32 words, trace_region region);
	void trace_access(trace_region region, bool write, u32 address, u16 data, u16 mem_mask, u16 last_write);
	void dump_repeated_accesses();
	void panel_text_byte(u8 data, u32 pc);
	void flush_panel_text();
	void log_cpu_context(u32 pc);
	void log_lowmem_04ee(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_lowmem_049d(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_pc_summary(const char *reason, u32 pc);
	void log_watched_pc(u32 pc);
	u32 read_stack_long(u32 address);
	static bool likely_rom_address(u32 address);
	static const char *address_region_guess(u32 address);
	static const char *trace_detail(trace_region region, u32 address);
	static const char *region_name(trace_region region);
	static const char *m68302_register_name(u32 address);
	u16 read_code_word(u32 address) const;
};


void asr10_boot_state::machine_start()
{
	m_pc_timer = timer_alloc(FUNC(asr10_boot_state::pc_poll), this);
	m_lowmem_shadow = make_unique_clear<u16[]>(LOWMEM_WORDS);

	save_pointer(NAME(m_lowmem_shadow), LOWMEM_WORDS);
	save_item(NAME(m_probe_or_alias_region_shadow));
	save_item(NAME(m_m68302_internal_shadow));
	save_item(NAME(m_fdc_last_aux_command));
	save_item(NAME(m_fdc_last_fifo_read));
	save_item(NAME(m_fdc_last_fifo_write));
	save_item(NAME(m_fdc_trace_sequence));
	save_item(NAME(m_fdc_transaction));
	save_item(NAME(m_fdc_transaction_access));
	save_item(NAME(m_fdc_fifo_transaction_reads));
	save_item(NAME(m_fdc_lowmem_watch));
	save_item(NAME(m_duart_panel_asr_shadow));
	save_item(NAME(m_scsi_asr_shadow));
	save_item(NAME(m_es550x_vfx_shadow));
	save_item(NAME(m_es5510_vfx_shadow));
	save_item(NAME(m_duart_vfx_shadow));
	save_item(NAME(m_fdc_vfx_shadow));
	save_item(NAME(m_es5506_ts_shadow));
	save_item(NAME(m_es5510_ts_shadow));
	save_item(NAME(m_panel_text));
	save_item(NAME(m_panel_text_length));
	save_item(NAME(m_high_alias_enabled));
	save_item(NAME(m_lowmem_overlay_enabled));
	save_item(NAME(m_seen_insert_disk_prompt));
	save_item(NAME(m_pc_poll_count));
	save_item(NAME(m_last_pc));
	save_item(NAME(m_last_distinct_pc));
	save_item(NAME(m_pc_repeat_count));
	save_item(NAME(m_pc_change_count));
	save_item(NAME(m_dispatcher_hits));
	save_item(NAME(m_context_hits));
}


void asr10_boot_state::machine_reset()
{
	m_high_alias_enabled = false;
	m_lowmem_overlay_enabled = false;
	m_seen_insert_disk_prompt = false;
	m_panel_text_length = 0;
	std::fill(std::begin(m_panel_text), std::end(m_panel_text), 0);
	m_pc_poll_count = 0;
	m_last_pc = 0xffffffffU;
	m_last_distinct_pc = 0xffffffffU;
	m_pc_repeat_count = 0;
	m_pc_change_count = 0;
	m_dispatcher_hits = 0;
	std::fill(std::begin(m_context_hits), std::end(m_context_hits), 0);
	std::fill_n(m_lowmem_shadow.get(), LOWMEM_WORDS, 0);
	for (auto &entry : m_probe_or_alias_region_shadow)
		std::fill(std::begin(entry), std::end(entry), 0);
	std::fill(std::begin(m_m68302_internal_shadow), std::end(m_m68302_internal_shadow), 0);
	m_fdc_last_aux_command = 0;
	m_fdc_last_fifo_read = 0;
	m_fdc_last_fifo_write = 0;
	m_fdc_trace_sequence = 0;
	m_fdc_transaction = 0;
	m_fdc_transaction_access = 0;
	m_fdc_fifo_transaction_reads = 0;
	m_fdc_lowmem_watch = 0;
	std::fill(std::begin(m_duart_panel_asr_shadow), std::end(m_duart_panel_asr_shadow), 0);
	std::fill(std::begin(m_scsi_asr_shadow), std::end(m_scsi_asr_shadow), 0);
	std::fill(std::begin(m_es550x_vfx_shadow), std::end(m_es550x_vfx_shadow), 0);
	std::fill(std::begin(m_es5510_vfx_shadow), std::end(m_es5510_vfx_shadow), 0);
	std::fill(std::begin(m_duart_vfx_shadow), std::end(m_duart_vfx_shadow), 0);
	std::fill(std::begin(m_fdc_vfx_shadow), std::end(m_fdc_vfx_shadow), 0);
	std::fill(std::begin(m_es5506_ts_shadow), std::end(m_es5506_ts_shadow), 0);
	std::fill(std::begin(m_es5510_ts_shadow), std::end(m_es5510_ts_shadow), 0);
	m_trace_slots = {};

	// Coarse landmark polling only. This does not replace instruction tracing.
	m_pc_timer->adjust(attotime::zero, 0, attotime::from_ticks(64, m_maincpu->clock()));

	logerror("ASR10BOOT reset: expected SP=$00000300 PC=$0000000c from ROM vectors\n");
}


void asr10_boot_state::mem_map(address_map &map)
{
	// Low boot region. Reads normally come from ROM; writes are logged and
	// shadowed only where the static analysis expects low-memory/vector data.
	map(0x000000, 0x0fffff).rw(FUNC(asr10_boot_state::low_rom_or_lowmem_r), FUNC(asr10_boot_state::lowmem_w));

	// Minimal RAM/MMIO map for boot/remap tracing only.
	map(0x408000, 0x408003).rw(FUNC(asr10_boot_state::probe_or_alias_region_408000_r), FUNC(asr10_boot_state::probe_or_alias_region_408000_w));
	map(0x808000, 0x808003).rw(FUNC(asr10_boot_state::probe_or_alias_region_808000_r), FUNC(asr10_boot_state::probe_or_alias_region_808000_w));
	map(0xc08000, 0xc08003).rw(FUNC(asr10_boot_state::probe_or_alias_region_c08000_r), FUNC(asr10_boot_state::probe_or_alias_region_c08000_w));

	// Reference-based candidate windows. These are deliberately traceable
	// register shadows, not device implementations or asserted ASR-10 decode.
	map(0x100000, 0x1fffff).ram(); // sample RAM candidate, directly tested by the boot ROM at 0x100000
	map(0x200000, 0x20007f).rw(FUNC(asr10_boot_state::es550x_vfx_candidate_r), FUNC(asr10_boot_state::es550x_vfx_candidate_w));
	map(0x260000, 0x2601ff).rw(FUNC(asr10_boot_state::es5510_vfx_candidate_r), FUNC(asr10_boot_state::es5510_vfx_candidate_w));
	map(0x280000, 0x28001f).rw(FUNC(asr10_boot_state::duart_vfx_candidate_r), FUNC(asr10_boot_state::duart_vfx_candidate_w));
	map(0x2c0000, 0x2c0007).rw(FUNC(asr10_boot_state::fdc_vfx_candidate_r), FUNC(asr10_boot_state::fdc_vfx_candidate_w));
	map(0x300000, 0x30007f).rw(FUNC(asr10_boot_state::es5506_ts_candidate_r), FUNC(asr10_boot_state::es5506_ts_candidate_w));
	map(0x380000, 0x3801ff).rw(FUNC(asr10_boot_state::es5510_ts_candidate_r), FUNC(asr10_boot_state::es5510_ts_candidate_w));

	map(0xf00000, 0xf7ffff).ram();
	map(0xf80000, 0xfbffff).rw(FUNC(asr10_boot_state::high_alias_r), FUNC(asr10_boot_state::high_alias_w));
	map(0xfc0000, 0xfc3fff).ram();
	map(0xfc4000, 0xfc4003).rw(FUNC(asr10_boot_state::upd72069_fdc_r), FUNC(asr10_boot_state::upd72069_fdc_w));
	map(0xfc4004, 0xfc47ff).ram();
	map(0xfc4800, 0xfc481f).rw(FUNC(asr10_boot_state::duart_panel_asr_candidate_r), FUNC(asr10_boot_state::duart_panel_asr_candidate_w));
	map(0xfc4820, 0xfc4fff).ram();
	map(0xfc5000, 0xfc501f).rw(FUNC(asr10_boot_state::scsi_asr_candidate_r), FUNC(asr10_boot_state::scsi_asr_candidate_w));
	map(0xfc5020, 0xfc67ff).ram();
	map(0xfc6800, 0xfc68ff).rw(FUNC(asr10_boot_state::m68302_internal_r), FUNC(asr10_boot_state::m68302_internal_w));
	map(0xfc6900, 0xffffff).ram();
}


u16 asr10_boot_state::low_rom_or_lowmem_r(offs_t offset, u16 mem_mask)
{
	const u32 byte_address = offset << 1;
	u32 probe_index = 0;
	u32 probe_word = 0;
	if (probe_or_alias_region_index(byte_address, probe_index, probe_word))
	{
		const u16 data = m_probe_or_alias_region_shadow[probe_index][probe_word] & mem_mask;
		trace_access(trace_region::BUS_PROBE, false, byte_address, data, mem_mask, m_probe_or_alias_region_shadow[probe_index][probe_word]);
		return data;
	}

	if (m_lowmem_overlay_enabled)
	{
		const u16 data = m_lowmem_shadow[offset] & mem_mask;
		if (byte_address == 0x04ee && !machine().side_effects_disabled())
			log_lowmem_04ee(false, m_lowmem_shadow[offset], m_lowmem_shadow[offset], mem_mask);
		else if (byte_address == 0x049c && !machine().side_effects_disabled())
			log_lowmem_049d(false, m_lowmem_shadow[offset], m_lowmem_shadow[offset], mem_mask);
		return data;
	}

	const u8 *rom = m_rom->base();
	const u32 rom_offset = byte_address & ROM_MASK;
	return (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
}


void asr10_boot_state::lowmem_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 byte_address = offset << 1;
	u32 probe_index = 0;
	u32 probe_word = 0;
	if (probe_or_alias_region_index(byte_address, probe_index, probe_word))
	{
		COMBINE_DATA(&m_probe_or_alias_region_shadow[probe_index][probe_word]);
		trace_access(trace_region::BUS_PROBE, true, byte_address, data, mem_mask, m_probe_or_alias_region_shadow[probe_index][probe_word]);
		return;
	}

	const u16 previous = m_lowmem_shadow[offset];
	COMBINE_DATA(&m_lowmem_shadow[offset]);
	if (byte_address == 0x04ee)
		log_lowmem_04ee(true, previous, m_lowmem_shadow[offset], mem_mask);
	else if (byte_address == 0x049c)
		log_lowmem_049d(true, previous, m_lowmem_shadow[offset], mem_mask);
	else if (m_seen_insert_disk_prompt && m_fdc_lowmem_watch && byte_address >= 0x0480 && byte_address <= 0x04fe)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10FDCLOWMEM time=%s seq=%llu txn=%u pc=%06x addr=%06x data=%04x mem_mask=%04x "
			"previous=%04x current=%04x last_command=%02x\n",
			machine().time().to_string(), (unsigned long long)m_fdc_trace_sequence, m_fdc_transaction,
			pc, byte_address, data, mem_mask, previous, m_lowmem_shadow[offset], m_fdc_last_aux_command);
		m_fdc_lowmem_watch--;
	}

	if (byte_address < LOWMEM_LOG_END || byte_address == 0x00ea || byte_address == 0x0b7a || byte_address == 0x0b7c || byte_address == 0x0b7e)
		trace_access(trace_region::LOWMEM, true, byte_address, data, mem_mask, m_lowmem_shadow[offset]);
}


u16 asr10_boot_state::probe_or_alias_region_408000_r(offs_t offset, u16 mem_mask)
{
	return probe_or_alias_region_r_at(0x00408000, offset, mem_mask);
}


void asr10_boot_state::probe_or_alias_region_408000_w(offs_t offset, u16 data, u16 mem_mask)
{
	probe_or_alias_region_w_at(0x00408000, offset, data, mem_mask);
}


u16 asr10_boot_state::probe_or_alias_region_808000_r(offs_t offset, u16 mem_mask)
{
	return probe_or_alias_region_r_at(0x00808000, offset, mem_mask);
}


void asr10_boot_state::probe_or_alias_region_808000_w(offs_t offset, u16 data, u16 mem_mask)
{
	probe_or_alias_region_w_at(0x00808000, offset, data, mem_mask);
}


u16 asr10_boot_state::probe_or_alias_region_c08000_r(offs_t offset, u16 mem_mask)
{
	return probe_or_alias_region_r_at(0x00c08000, offset, mem_mask);
}


void asr10_boot_state::probe_or_alias_region_c08000_w(offs_t offset, u16 data, u16 mem_mask)
{
	probe_or_alias_region_w_at(0x00c08000, offset, data, mem_mask);
}


u16 asr10_boot_state::high_alias_r(offs_t offset, u16 mem_mask)
{
	const u8 *rom = m_rom->base();
	const u32 runtime_address_24 = 0x00f80000 | (offset << 1);
	const u32 rom_offset = runtime_address_24 & ROM_MASK;
	const u16 data = (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
	if (!m_high_alias_enabled)
		trace_access(trace_region::HIGH_ROM_ALIAS, false, runtime_address_24, data, mem_mask, data);
	return data;
}


void asr10_boot_state::high_alias_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00f80000 | (offset << 1);
	trace_access(trace_region::HIGH_ROM_ALIAS, true, address, data, mem_mask, data);
}


u16 asr10_boot_state::m68302_internal_r(offs_t offset, u16 mem_mask)
{
	const u32 address = 0x00fc6800 | (offset << 1);
	const u16 data = m_m68302_internal_shadow[offset & 0x7f] & mem_mask;
	trace_access(trace_region::M68302_INTERNAL, false, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	return data;
}


void asr10_boot_state::m68302_internal_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00fc6800 | (offset << 1);
	COMBINE_DATA(&m_m68302_internal_shadow[offset & 0x7f]);
	trace_access(trace_region::M68302_INTERNAL, true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);

	if (address == CONTROL_REGISTER_CANDIDATE && ACCESSING_BITS_0_15)
	{
		if (data == 0x1f01)
		{
			m_high_alias_enabled = true;
			m_lowmem_overlay_enabled = true;
			logerror("ASR10BOOT control_register_candidate: write #$1f01 -> enable likely high alias runtime&0x3ffff and lowmem overlay (speculative harness behavior)\n");
		}
		else
		{
			logerror("ASR10BOOT control_register_candidate: write %04x observed; exact semantics unknown\n", data);
		}
	}
}


u16 asr10_boot_state::upd72069_fdc_r(offs_t offset, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u64 sequence = ++m_fdc_trace_sequence;
	const u32 transaction_access = ++m_fdc_transaction_access;

	u8 raw_data = 0;
	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1)
	{
		raw_data = m_fdc->msr_r();
		switch (pc)
		{
		case 0x00fb8d1e:
			detail = "status_bit4_busy_must_clear";
			break;
		case 0x00fb8d40:
			detail = "status_bit7_write_ready_must_set";
			break;
		case 0x00fb8d4e:
			detail = "status_bit6_write_direction_must_clear";
			break;
		case 0x00fb8d9a:
			detail = "status_bit7_receive_ready_must_set";
			break;
		case 0x00fb8da8:
			detail = "status_bit6_receive_data_present";
			break;
		default:
			detail = "upd72069_msr";
			break;
		}
	}
	else if ((address & 3) == 3)
	{
		raw_data = m_fdc->fifo_r();
		m_fdc_last_fifo_read = raw_data;
		m_fdc_fifo_transaction_reads++;
		m_fdc_lowmem_watch = 8;
		detail = "upd72069_fifo_read";
	}

	const u16 result = raw_data & mem_mask;
	logerror("ASR10FDC time=%s seq=%llu txn=%u txn_access=%u pc=%06x addr=%06x rw=R data=%04x "
		"mem_mask=%04x last_aux_command=%02x last_data_read=%02x last_data_write=%02x "
		"transaction_reads=%u phase=%s panel=\"%s\" detail=%s\n",
		machine().time().to_string(), (unsigned long long)sequence, m_fdc_transaction, transaction_access,
		pc, address, result, mem_mask, m_fdc_last_aux_command,
		m_fdc_last_fifo_read, m_fdc_last_fifo_write, m_fdc_fifo_transaction_reads,
		m_seen_insert_disk_prompt ? "post_insert_disk" : "boot", m_panel_text, detail);
	trace_access(trace_region::UPD72069_FDC_CANDIDATE, false, address, result, mem_mask, m_fdc_last_aux_command);
	return result;
}


void asr10_boot_state::upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u64 sequence = ++m_fdc_trace_sequence;

	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1 && ACCESSING_BITS_0_7)
	{
		m_fdc_transaction++;
		m_fdc_transaction_access = 0;
		m_fdc_fifo_transaction_reads = 0;
		m_fdc_last_aux_command = u8(data);
		m_fdc->auxcmd_w(m_fdc_last_aux_command);
		detail = (m_fdc_last_aux_command == 0x36) ? "upd72069_software_reset" : "upd72069_aux_command";
	}
	else if ((address & 3) == 3 && ACCESSING_BITS_0_7)
	{
		m_fdc_last_fifo_write = u8(data);
		m_fdc->fifo_w(m_fdc_last_fifo_write);
		m_fdc_lowmem_watch = 8;
		detail = "upd72069_fifo";
	}
	const u32 transaction_access = ++m_fdc_transaction_access;

	logerror("ASR10FDC time=%s seq=%llu txn=%u txn_access=%u pc=%06x addr=%06x rw=W data=%04x "
		"mem_mask=%04x last_aux_command=%02x last_data_read=%02x last_data_write=%02x "
		"transaction_reads=%u phase=%s panel=\"%s\" detail=%s\n",
		machine().time().to_string(), (unsigned long long)sequence, m_fdc_transaction, transaction_access,
		pc, address, data, mem_mask, m_fdc_last_aux_command,
		m_fdc_last_fifo_read, m_fdc_last_fifo_write, m_fdc_fifo_transaction_reads,
		m_seen_insert_disk_prompt ? "post_insert_disk" : "boot", m_panel_text, detail);
	trace_access(trace_region::UPD72069_FDC_CANDIDATE, true, address, data, mem_mask, m_fdc_last_aux_command);
}


u16 asr10_boot_state::duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	const u32 address = 0x00fc4800 | (offset << 1);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);

	// Channel B carries panel traffic on VFX-SD/SD-1 and TS-10. The ASR ROM
	// writes the channel-B TX buffer at +0x17, then polls bit 0 at +0x13.
	// Report receive-ready to expose the next boot dependency.
	if (address == 0x00fc4812 && ACCESSING_BITS_0_7)
		m_duart_panel_asr_shadow[word] = 0x0001;

	u16 raw_data = (address == 0x00fc4816) ? 0 : m_duart_panel_asr_shadow[word];
	if (address == 0x00fc4808 && ACCESSING_BITS_0_7)
	{
		raw_data = ASR10_DUART_INPUT_CHANGE_STUB;
		if (ASR10_PULSE_DUART_INPUT_BIT4_AT_SEMANTIC_READER && pc == 0x00fb7c84)
			raw_data |= 0x10;
		if (m_seen_insert_disk_prompt)
		{
			logerror("ASR10DUART_INPUT_BRANCH pc=%06x value=%02x instruction=tst.b tested_mask=none local_branch=none next_pc=fb7c36 side_effect=ack_input_change_then_set_04ee_ff\n",
				pc, ASR10_DUART_INPUT_CHANGE_STUB);
		}
	}

	const u16 data = raw_data & mem_mask;
	trace_access(trace_region::DUART_PANEL_ASR_CANDIDATE, false, address | (ACCESSING_BITS_0_7 ? 1 : 0), data, mem_mask, m_duart_panel_asr_shadow[word]);
	return data;
}


void asr10_boot_state::duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	COMBINE_DATA(&m_duart_panel_asr_shadow[word]);
	const u32 address = (0x00fc4800 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
	{
		const u8 character = u8(data);
		if (character >= 0x20 && character <= 0x7e)
			logerror("ASR10PANEL char='%c' hex=%02x pc=%06x\n", character, character, pc);
		else
			logerror("ASR10PANEL control=%02x pc=%06x\n", character, pc);
		panel_text_byte(character, pc);
	}
	trace_access(trace_region::DUART_PANEL_ASR_CANDIDATE, true, address, data, mem_mask, m_duart_panel_asr_shadow[word]);
}


void asr10_boot_state::panel_text_byte(u8 data, u32 pc)
{
	if (pc != 0x00f89cb0 || data < 0x20 || data > 0x7e)
	{
		flush_panel_text();
		return;
	}

	if (m_panel_text_length == PANEL_TEXT_LENGTH - 1)
		flush_panel_text();

	m_panel_text[m_panel_text_length++] = char(data);
	m_panel_text[m_panel_text_length] = 0;

	if (!m_seen_insert_disk_prompt && strstr(m_panel_text, "PLEASE INSERT DISK"))
	{
		m_seen_insert_disk_prompt = true;
		m_trace_slots = {};
		logerror("ASR10PHASE phase=post_insert_disk_prompt pc=%06x\n", pc);
	}
}


void asr10_boot_state::flush_panel_text()
{
	if (m_panel_text_length)
		logerror("ASR10PANEL text=\"%s\"\n", m_panel_text);

	m_panel_text_length = 0;
	m_panel_text[0] = 0;
}


u32 asr10_boot_state::read_stack_long(u32 address)
{
	auto const disable_side_effects = machine().disable_side_effects();
	return m_maincpu->space(AS_PROGRAM).read_dword(address & 0x00ffffff);
}


bool asr10_boot_state::likely_rom_address(u32 address)
{
	address &= 0x00ffffff;
	return address >= 0x00f80000 && address <= 0x00fbffff;
}


void asr10_boot_state::log_cpu_context(u32 pc)
{
	static constexpr u32 landmarks[] = {
		0x00fb7c30, 0x00fb7c7a, 0x00fb7c9c,
		0x00fb9104, 0x00fb9184, 0x00fb9188,
		0x00fb92ce, 0x00fb92d2, 0x00fb92d8, 0x00fb9344, 0x00fb9342,
		0x00fb9358, 0x00fb9376, 0x00fb937e, 0x00fb9382, 0x00fb93c2,
		0x00f88030, 0x00f87fd2, 0x00f89c48, 0x00f89cb0
	};
	u32 landmark = std::size(landmarks);
	for (u32 index = 0; index < std::size(landmarks); index++)
	{
		if (pc == landmarks[index])
		{
			landmark = index;
			break;
		}
	}
	if (landmark == std::size(landmarks))
		return;

	const u32 hits = ++m_context_hits[landmark];
	if (pc != 0x00fb7c30 && pc != 0x00fb7c7a && hits != 1 && (hits & (hits - 1)) != 0)
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	const u32 return_address = likely_rom_address(stack0) ? (stack0 & 0x00ffffff) : 0xffffffffU;

	logerror("ASR10CPUCONTEXT pc=%06x previous_pc=%06x opcode=%04x sr=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x guessed_return=%08x phase=%s hits=%u\n",
		pc, m_last_distinct_pc, read_code_word(pc), u16(m_maincpu->state_int(M68K_SR)),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		sp, stack0, stack1, stack2, return_address,
		m_seen_insert_disk_prompt ? "post_insert_disk" : "boot", hits);

	if (pc == 0x00fb7c30 && return_address != 0xffffffffU)
	{
		logerror("ASR10CALLCONTEXT callee=fb7c30 return=%06x stack0=%08x "
			"callsite=fb9184 containing_routine=fb9104 path=normal_bsr_nested_return_fb90ee\n",
			return_address, stack0);
	}
	else if (pc == 0x00fb7c7a)
	{
		const u8 lowmem_04ee = u8(m_lowmem_shadow[0x04ee / 2] >> 8);
		logerror("ASR10SEMANTICREADER pc=fb7c7a lowmem_04ee=%02x negative=%u "
			"duart_fc4809=%02x bit4=%u gate_bpl_target=fb7c9c return=%08x\n",
			lowmem_04ee, BIT(lowmem_04ee, 7), ASR10_DUART_INPUT_CHANGE_STUB |
				(ASR10_PULSE_DUART_INPUT_BIT4_AT_SEMANTIC_READER ? 0x10 : 0),
			ASR10_PULSE_DUART_INPUT_BIT4_AT_SEMANTIC_READER ? 1 : BIT(ASR10_DUART_INPUT_CHANGE_STUB, 4),
			return_address);
	}
}


void asr10_boot_state::log_lowmem_04ee(bool write, u16 previous, u16 current, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	logerror("ASR10LOWMEM04EE pc=%06x rw=%c value=%04x previous=%04x current=%04x mem_mask=%04x "
		"phase=%s sp=%06x stack0=%08x stack1=%08x stack2=%08x\n",
		pc, write ? 'W' : 'R', current & mem_mask, previous, current, mem_mask,
		m_seen_insert_disk_prompt ? "post_insert_disk" : "boot", sp, stack0, stack1, stack2);
}


void asr10_boot_state::log_lowmem_049d(bool write, u16 previous, u16 current, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u8 previous_byte = u8(previous);
	const u8 current_byte = u8(current);
	if (ACCESSING_BITS_0_7)
	{
		logerror("ASR10STATE049D pc=%06x rw=%c previous=%02x current=%02x phase=%s\n",
			pc, write ? 'W' : 'R', previous_byte, current_byte,
			m_seen_insert_disk_prompt ? "post_insert_disk" : "boot");
	}
}


u16 asr10_boot_state::scsi_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	const u32 address = 0x00fc5000 | (offset << 1);

	u16 data = 0;
	if (ASR10_FAKE_SCSI_INSTALLED)
	{
		// Preserve the original harness behavior for path comparison.
		if (address == 0x00fc5000 && ACCESSING_BITS_0_7)
			m_scsi_asr_shadow[word] |= 0x0080;
		data = m_scsi_asr_shadow[word];
	}

	data &= mem_mask;
	trace_access(trace_region::SCSI_ASR_CANDIDATE, false, address | (ACCESSING_BITS_0_7 ? 1 : 0), data, mem_mask, m_scsi_asr_shadow[word]);
	return data;
}


void asr10_boot_state::scsi_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	COMBINE_DATA(&m_scsi_asr_shadow[word]);
	const u32 address = (0x00fc5000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	trace_access(trace_region::SCSI_ASR_CANDIDATE, true, address, data, mem_mask, m_scsi_asr_shadow[word]);
}


u16 asr10_boot_state::es550x_vfx_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x00200000, offset, mem_mask, m_es550x_vfx_shadow, std::size(m_es550x_vfx_shadow), trace_region::ES550X_VFX_CANDIDATE);
}


void asr10_boot_state::es550x_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x00200000, offset, data, mem_mask, m_es550x_vfx_shadow, std::size(m_es550x_vfx_shadow), trace_region::ES550X_VFX_CANDIDATE);
}


u16 asr10_boot_state::es5510_vfx_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x00260000, offset, mem_mask, m_es5510_vfx_shadow, std::size(m_es5510_vfx_shadow), trace_region::ES5510_VFX_CANDIDATE);
}


void asr10_boot_state::es5510_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x00260000, offset, data, mem_mask, m_es5510_vfx_shadow, std::size(m_es5510_vfx_shadow), trace_region::ES5510_VFX_CANDIDATE);
}


u16 asr10_boot_state::duart_vfx_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x00280000, offset, mem_mask, m_duart_vfx_shadow, std::size(m_duart_vfx_shadow), trace_region::DUART_VFX_CANDIDATE);
}


void asr10_boot_state::duart_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x00280000, offset, data, mem_mask, m_duart_vfx_shadow, std::size(m_duart_vfx_shadow), trace_region::DUART_VFX_CANDIDATE);
}


u16 asr10_boot_state::fdc_vfx_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x002c0000, offset, mem_mask, m_fdc_vfx_shadow, std::size(m_fdc_vfx_shadow), trace_region::FDC_VFX_CANDIDATE);
}


void asr10_boot_state::fdc_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x002c0000, offset, data, mem_mask, m_fdc_vfx_shadow, std::size(m_fdc_vfx_shadow), trace_region::FDC_VFX_CANDIDATE);
}


u16 asr10_boot_state::es5506_ts_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x00300000, offset, mem_mask, m_es5506_ts_shadow, std::size(m_es5506_ts_shadow), trace_region::ES5506_TS_CANDIDATE);
}


void asr10_boot_state::es5506_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x00300000, offset, data, mem_mask, m_es5506_ts_shadow, std::size(m_es5506_ts_shadow), trace_region::ES5506_TS_CANDIDATE);
}


u16 asr10_boot_state::es5510_ts_candidate_r(offs_t offset, u16 mem_mask)
{
	return candidate_r(0x00380000, offset, mem_mask, m_es5510_ts_shadow, std::size(m_es5510_ts_shadow), trace_region::ES5510_TS_CANDIDATE);
}


void asr10_boot_state::es5510_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	candidate_w(0x00380000, offset, data, mem_mask, m_es5510_ts_shadow, std::size(m_es5510_ts_shadow), trace_region::ES5510_TS_CANDIDATE);
}


bool asr10_boot_state::probe_or_alias_region_index(u32 address, u32 &index, u32 &word_index) const
{
	static constexpr u32 bases[PROBE_OR_ALIAS_REGION_COUNT] = { 0x00008000, 0x00408000, 0x00808000, 0x00c08000 };
	for (u32 i = 0; i < PROBE_OR_ALIAS_REGION_COUNT; i++)
	{
		if (address >= bases[i] && address <= bases[i] + 3)
		{
			index = i;
			word_index = (address - bases[i]) >> 1;
			return true;
		}
	}

	return false;
}


u16 asr10_boot_state::probe_or_alias_region_r_at(u32 base, offs_t offset, u16 mem_mask)
{
	u32 index = 0;
	u32 word = 0;
	const u32 address = base + ((offset << 1) & 0x00000002);
	if (probe_or_alias_region_index(address, index, word))
	{
		const u16 data = m_probe_or_alias_region_shadow[index][word] & mem_mask;
		trace_access(trace_region::BUS_PROBE, false, address, data, mem_mask, m_probe_or_alias_region_shadow[index][word]);
		return data;
	}

	trace_access(trace_region::BUS_PROBE, false, address, 0, mem_mask, 0);
	return 0;
}


void asr10_boot_state::probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask)
{
	u32 index = 0;
	u32 word = 0;
	const u32 address = base + ((offset << 1) & 0x00000002);
	if (probe_or_alias_region_index(address, index, word))
		COMBINE_DATA(&m_probe_or_alias_region_shadow[index][word]);

	trace_access(trace_region::BUS_PROBE, true, address, data, mem_mask, m_probe_or_alias_region_shadow[index][word]);
}


u16 asr10_boot_state::candidate_r(u32 base, offs_t offset, u16 mem_mask, u16 *shadow, u32 words, trace_region region)
{
	const u32 word = offset % words;
	const u16 data = shadow[word] & mem_mask;
	trace_access(region, false, base + (offset << 1), data, mem_mask, shadow[word]);
	return data;
}


void asr10_boot_state::candidate_w(u32 base, offs_t offset, u16 data, u16 mem_mask, u16 *shadow, u32 words, trace_region region)
{
	const u32 word = offset % words;
	COMBINE_DATA(&shadow[word]);
	trace_access(region, true, base + (offset << 1), data, mem_mask, shadow[word]);
}


void asr10_boot_state::trace_access(trace_region region, bool write, u32 address, u16 data, u16 mem_mask, u16 last_write)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	trace_slot *slot = nullptr;
	for (trace_slot &candidate : m_trace_slots)
	{
		if (candidate.repeat_count && candidate.pc == pc && candidate.address == address && candidate.write == write &&
			candidate.data == data && candidate.mem_mask == mem_mask && candidate.region == region)
		{
			slot = &candidate;
			break;
		}
		if (!slot && !candidate.repeat_count)
			slot = &candidate;
	}

	if (!slot)
		slot = &m_trace_slots[(pc ^ address ^ data ^ mem_mask ^ write) % m_trace_slots.size()];

	const bool same = slot->repeat_count && slot->pc == pc && slot->address == address && slot->write == write &&
		slot->data == data && slot->mem_mask == mem_mask && slot->region == region;
	if (!same)
	{
		*slot = {};
		slot->pc = pc;
		slot->address = address;
		slot->data = data;
		slot->mem_mask = mem_mask;
		slot->region = region;
		slot->write = write;
	}

	slot->last_write = last_write;
	slot->repeat_count++;
	const u32 repeats = slot->repeat_count;
	if ((repeats == 1) || ((repeats & (repeats - 1)) == 0))
	{
		const char *const detail = trace_detail(region, address);
		logerror("ASR10TRACE pc=%06x addr=%06x rw=%c data=%04x mem_mask=%04x last_write=%04x region=%s detail=%s repeats=%u\n",
			pc, address, write ? 'W' : 'R', data, mem_mask, last_write, region_name(region), detail, repeats);
		if (m_seen_insert_disk_prompt)
		{
			logerror("ASR10POSTDISK pc=%06x addr=%06x rw=%c data=%04x mem_mask=%04x region=%s detail=%s repeats=%u\n",
				pc, address, write ? 'W' : 'R', data, mem_mask, region_name(region), detail, repeats);
		}
	}
}


void asr10_boot_state::dump_repeated_accesses()
{
	for (trace_slot const &slot : m_trace_slots)
	{
		if (slot.repeat_count > 1)
		{
			logerror("ASR10POLL pc=%06x addr=%06x rw=%c data=%04x mem_mask=%04x last_write=%04x region=%s repeats=%u\n",
				slot.pc, slot.address, slot.write ? 'W' : 'R', slot.data, slot.mem_mask, slot.last_write, region_name(slot.region), slot.repeat_count);
			if (m_seen_insert_disk_prompt)
			{
				logerror("ASR10POSTDISK summary=repeat pc=%06x addr=%06x rw=%c data=%04x mem_mask=%04x region=%s detail=%s repeats=%u\n",
					slot.pc, slot.address, slot.write ? 'W' : 'R', slot.data, slot.mem_mask,
					region_name(slot.region), trace_detail(slot.region, slot.address), slot.repeat_count);
			}
		}
	}
}


const char *asr10_boot_state::trace_detail(trace_region region, u32 address)
{
	if (region == trace_region::M68302_INTERNAL)
		return m68302_register_name(address);
	if (region == trace_region::UPD72069_FDC_CANDIDATE)
		return ((address & 3) == 1) ? "upd72069_msr_auxcmd_candidate" :
			((address & 3) == 3) ? "upd72069_fifo_candidate" : "upd72069_register_unknown";
	if (region == trace_region::DUART_PANEL_ASR_CANDIDATE)
	{
		switch (address & 0x1f)
		{
		case 0x09: return "input_port_change_candidate";
		case 0x13: return "channel_b_status_rx_ready_stub";
		case 0x17: return "channel_b_rx_tx_buffer";
		default: return "duart_register_unknown";
		}
	}
	if (region == trace_region::SCSI_ASR_CANDIDATE)
	{
		if ((address & 0x1f) == 0x01)
			return ASR10_FAKE_SCSI_INSTALLED ? "status_control_candidate_fake_installed" : "status_control_candidate_no_scsi";
		if ((address & 0x1f) == 0x03)
			return ASR10_FAKE_SCSI_INSTALLED ? "data_scratch_candidate_fake_installed" : "data_scratch_candidate_no_scsi";
		return "scsi_register_unknown";
	}
	if (region == trace_region::FDC_VFX_CANDIDATE)
	{
		switch ((address >> 1) & 3)
		{
		case 0: return "vfx_reference_fdc_status_command";
		case 1: return "vfx_reference_fdc_track";
		case 2: return "vfx_reference_fdc_sector";
		case 3: return "vfx_reference_fdc_data";
		}
	}
	return "register_unknown";
}


const char *asr10_boot_state::region_name(trace_region region)
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


const char *asr10_boot_state::address_region_guess(u32 address)
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


const char *asr10_boot_state::m68302_register_name(u32 address)
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
	case 0x28: return "port_b_data_candidate";
	case 0x30: return "chip_select_0_base_candidate";
	case 0x32: return "chip_select_0_option_candidate";
	case 0x34: return "chip_select_1_base_candidate";
	case 0x36: return "chip_select_1_option_candidate";
	case 0x38: return "chip_select_2_base_candidate";
	case 0x3a: return "chip_select_2_option_candidate";
	case 0x3c: return "chip_select_3_base_candidate";
	case 0x3e: return "chip_select_3_option_candidate";
	case 0x4a: return "timer_or_clock_candidate";
	case 0x50: return "timer_1_mode_candidate";
	case 0x52: return "timer_1_reference_candidate";
	default: return "internal_register_unknown";
	}
}


void asr10_boot_state::log_watched_pc(u32 pc)
{
	log_cpu_context(pc);

	switch (pc)
	{
	case 0x00fb8e06:
		logerror("ASR10BOOT watched_pc: pc=%06x full_static=$fffb8e06 rom_offset=0x38e06 boot_continuation\n", pc);
		break;
	case 0x00f87e5c:
		logerror("ASR10BOOT watched_pc: pc=%06x full_static=$fff87e5c rom_offset=0x07e5c boot_handoff\n", pc);
		break;
	case 0x00f87fd2:
		m_dispatcher_hits++;
		logerror("ASR10BOOT watched_pc: pc=%06x full_static=$fff87fd2 rom_offset=0x07fd2 dispatcher_7fd2 hit=%u\n", pc, m_dispatcher_hits);
		if (m_dispatcher_hits > 16)
		{
			logerror("ASR10BOOT stop: dispatcher loop threshold reached\n");
			machine().schedule_exit();
		}
		break;
	case 0x00f88030:
		logerror("ASR10BOOT watched_pc: pc=%06x full_static=$fff88030 rom_offset=0x08030 system_call_8030\n", pc);
		break;
	default:
		break;
	}
}


u16 asr10_boot_state::read_code_word(u32 address) const
{
	if ((address <= ROM_MASK) || ((address >= 0x00f80000) && (address <= 0x00fbffff)))
	{
		const u32 offset = address & ROM_MASK;
		const u8 *const rom = m_rom->base();
		return (u16(rom[offset]) << 8) | rom[(offset + 1) & ROM_MASK];
	}
	return 0xffff;
}


void asr10_boot_state::log_pc_summary(const char *reason, u32 pc)
{
	const u16 opcode = read_code_word(pc);
	u32 branch_target = 0xffffffffU;
	u32 branch_pc = 0xffffffffU;
	u32 accessed_address = 0xffffffffU;
	s32 tested_bit = -1;
	const char *branch_kind = "not_decoded";
	const char *loop_guess = "unknown";

	if (opcode == 0x1039)
		accessed_address = (u32(read_code_word(pc + 2)) << 16) | read_code_word(pc + 4);
	else if ((opcode & 0xfff8) == 0x0810)
	{
		accessed_address = m_maincpu->state_int(M68K_A0 + (opcode & 7)) & 0x00ffffff;
		tested_bit = read_code_word(pc + 2) & 7;
	}

	for (u32 candidate = pc; candidate < pc + 0x20; candidate += 2)
	{
		const u16 candidate_opcode = read_code_word(candidate);
		u32 candidate_target = 0xffffffffU;
		const char *candidate_kind = nullptr;
		if ((candidate_opcode & 0xf000) == 0x6000)
		{
			const s8 displacement = s8(candidate_opcode);
			candidate_kind = "bcc_bra_bsr";
			candidate_target = displacement ?
				((candidate + 2 + displacement) & 0x00ffffff) :
				((candidate + 2 + s16(read_code_word(candidate + 2))) & 0x00ffffff);
		}
		else if ((candidate_opcode & 0xf0f8) == 0x50c8)
		{
			candidate_kind = "dbcc";
			candidate_target = (candidate + 2 + s16(read_code_word(candidate + 2))) & 0x00ffffff;
		}

		if (candidate_kind && candidate_target <= pc)
		{
			branch_pc = candidate;
			branch_target = candidate_target;
			branch_kind = candidate_kind;
			break;
		}
	}

	for (u32 candidate = pc - std::min<u32>(pc, 8); candidate <= pc; candidate += 2)
	{
		if (read_code_word(candidate) == 0xb683 && read_code_word(candidate + 2) == 0x2f03 &&
			read_code_word(candidate + 4) == 0x261f && read_code_word(candidate + 6) == 0x5383 &&
			pc <= candidate + 8)
		{
			loop_guess = "d3_register_countdown_not_mmio";
			break;
		}
	}
	if (!strcmp(loop_guess, "unknown") && accessed_address != 0xffffffffU)
	{
		loop_guess = "device_or_memory_poll";
	}

	logerror("ASR10HANG reason=%s pc=%06x previous_pc=%06x opcode=%04x accessed_address=%08x tested_bit=%d region_guess=%s loop_guess=%s d3=%08x branch_pc=%06x branch_kind=%s branch_target=%06x pc_repeat_count=%u poll_count=%u\n",
		reason, pc, m_last_distinct_pc, opcode, accessed_address, tested_bit, address_region_guess(accessed_address),
		loop_guess, u32(m_maincpu->state_int(M68K_D3)), branch_pc, branch_kind, branch_target, m_pc_repeat_count, u32(m_pc_poll_count));
	dump_repeated_accesses();
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::pc_poll)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_pc_poll_count++;

	if (pc != m_last_pc)
	{
		m_last_distinct_pc = m_last_pc;
		m_pc_repeat_count = 1;
		m_pc_change_count++;
		if (m_pc_change_count <= 256)
			logerror("ASR10PC pc=%06x previous_pc=%06x opcode=%04x\n", pc, m_last_distinct_pc, read_code_word(pc));
		else if (m_pc_change_count == 257)
			logerror("ASR10PC further transitions suppressed; final loop summary remains enabled\n");
		log_watched_pc(pc);
		m_last_pc = pc;
	}
	else
	{
		m_pc_repeat_count++;
	}

	if (m_pc_poll_count > MAX_PC_POLLS)
	{
		log_pc_summary("max_poll_count", pc);
		machine().schedule_exit();
	}
}

static INPUT_PORTS_START(asr10_boot)
INPUT_PORTS_END


void asr10_boot_state::asr10_boot(machine_config &config)
{
	M68000(config, m_maincpu, XTAL(16'000'000)); // 68000-compatible stand-in for likely MC68302-family board
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);

	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder for boot tracing
}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
