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
#include "imagedev/floppy.h"
#include "machine/upd765.h"

#include "formats/esq16_dsk.h"
#include "formats/hxchfe_dsk.h"

#include <algorithm>
#include <array>

// TODO: ASR-10 likely contains ES5701/Super-GLU-class Ensoniq ASIC.
// Known/claimed roles: 68000<->ESP, 68000<->OTIS,
// OTIS<->static memory, clock generation.
// Current harness models only decoded candidate windows and does not yet
// emulate Super-GLU bus/memory/clock behavior.

namespace {

class asr10_boot_state : public driver_device
{
public:
	asr10_boot_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_fdc(*this, "fdc")
		, m_floppy_connector(*this, "fdc:0")
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
	static constexpr bool ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 = true;
	static constexpr bool ASR10_LOG_FDC_ACCESS = false;
	static constexpr bool ASR10_LOG_FDC_04B0_CONTEXT = false;
	static constexpr bool ASR10_LOG_PANEL_BYTES = false;
	static constexpr bool ASR10_EXPERIMENT_CMD88_RATE_500K = true;
	static constexpr bool ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE = true;
	static constexpr u8 ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_READ_DELAY = 2;
	static constexpr bool ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21 = false;
	static constexpr bool ASR10_EXPERIMENT_STUB_CMD1E_RESULTS = false;
	static constexpr u8 ASR10_STUB_CMD1E_RESULT_BYTE0 = 0x00;
	static constexpr u8 ASR10_STUB_CMD1E_RESULT_BYTE1 = 0x00;
	static constexpr bool ASR10_EXPERIMENT_STUB_CMD0E_RESULT = false;
	static constexpr u8 ASR10_STUB_CMD0E_RESULT_BYTE = 0x00;

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
	required_device<floppy_connector> m_floppy_connector;
	required_memory_region m_rom;

	emu_timer *m_pc_timer = nullptr;
	emu_timer *m_prompt_select_timer = nullptr;
	std::unique_ptr<u16[]> m_lowmem_shadow;
	u16 m_probe_or_alias_region_shadow[PROBE_OR_ALIAS_REGION_COUNT][2]{};
	u16 m_m68302_internal_shadow[0x80]{};
	u8 m_fc6860_reads_after_write = 0;
	bool m_fc6860_busy_clear_logged = false;
	u8 m_fdc_last_aux_command = 0;
	u8 m_fdc_last_msr = 0;
	u8 m_fdc_last_fifo_read = 0;
	u8 m_fdc_last_fifo_write = 0;
	u32 m_fdc_data_rate = 250000;
	u8 m_fdc_data_rate_source = 0;
	u64 m_fdc_trace_sequence = 0;
	u32 m_fdc_transaction = 0;
	u32 m_fdc_transaction_access = 0;
	u8 m_fdc_fifo_transaction_reads = 0;
	u8 m_fdc_lowmem_watch = 0;
	std::array<u8, 8> m_fdc_txn_read_bytes{};
	std::array<u32, 8> m_fdc_txn_read_pcs{};
	std::array<u8, 8> m_fdc_txn_write_bytes{};
	std::array<u32, 8> m_fdc_txn_write_pcs{};
	std::array<u8, 8> m_fdc_txn_read_msr{};
	std::array<u8, 8> m_fdc_txn_write_msr{};
	u8 m_fdc_txn_read_count = 0;
	u8 m_fdc_txn_write_count = 0;
	bool m_fdc_txn_summary_active = false;
	bool m_fdc_cmd0e_active = false;
	std::array<u8, 8> m_fdc_command_ring{};
	u8 m_fdc_command_ring_count = 0;
	u8 m_fdc_command_ring_next = 0;
	std::array<u8, 9> m_fdc_cmd46_write_bytes{};
	std::array<u32, 9> m_fdc_cmd46_write_pcs{};
	std::array<u8, 7> m_fdc_cmd46_result_bytes{};
	std::array<u32, 7> m_fdc_cmd46_result_pcs{};
	u32 m_fdc_cmd46_transaction = 0;
	u8 m_fdc_cmd46_write_count = 0;
	u8 m_fdc_cmd46_result_count = 0;
	bool m_fdc_cmd46_active = false;
	bool m_fdc_cmd46_result_complete = false;
	u8 m_prompt_select_trace_mask = 0;
	u8 m_04b0_countdown_trace_mask = 0;
	u32 m_media_branch_last_pc = 0xffffffff;
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
	bool m_insert_disk_decision_logged = false;
	bool m_seen_error_reboot_prompt = false;
	bool m_panel_reboot_confirm_injected = false;
	bool m_error009_origin_logged = false;
	u32 m_lrclk_trace_count = 0;
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
	TIMER_CALLBACK_MEMBER(prompt_select_poll);

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
	void log_fdc_04b0_context(bool write, u16 mem_mask);
	void log_fdc_cmd0e_summary();
	void log_fdc_txn_summary();
	void log_fdc_88_f3_summary();
	void log_fdc_cmd46_summary();
	void log_fdc_cmd46_lowmem_store(u32 byte_address, u16 mem_mask);
	void log_insert_disk_decision(u32 pc);
	void log_prompt_select(u32 pc);
	void log_04b0_countdown(u32 pc, char rw, u16 previous, u16 current);
	void log_media_branch(u32 pc, u16 sr_override = 0xffff);
	void log_fb81b4_path(const char *landmark, u32 pc, u8 tested_value, bool branch_taken,
		u32 branch_target, u16 sr_override = 0xffff, u32 d2_override = 0xffffffff);
	void log_04c6_origin(const char *landmark, u32 pc, u8 value, bool branch_taken, u32 branch_target);
	void log_error009_context(const char *source, u32 pc, u16 value, u16 mem_mask);
	void log_lrclk_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 shadow);
	void log_lowmem_04ee(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_lowmem_049d(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_pc_summary(const char *reason, u32 pc);
	void log_watched_pc(u32 pc);
	u32 read_stack_long(u32 address);
	static void floppy_drives(device_slot_interface &device);
	static void floppy_formats(format_registration &fr);
	static bool likely_rom_address(u32 address);
	static const char *address_region_guess(u32 address);
	static const char *trace_detail(trace_region region, u32 address);
	static const char *region_name(trace_region region);
	static const char *m68302_register_name(u32 address);
	static const char *fdc_state_field_name(u32 address);
	static bool is_fdc_state_field(u32 address);
	u16 read_code_word(u32 address) const;
};


void asr10_boot_state::machine_start()
{
	m_pc_timer = timer_alloc(FUNC(asr10_boot_state::pc_poll), this);
	m_prompt_select_timer = timer_alloc(FUNC(asr10_boot_state::prompt_select_poll), this);
	m_lowmem_shadow = make_unique_clear<u16[]>(LOWMEM_WORDS);

	save_pointer(NAME(m_lowmem_shadow), LOWMEM_WORDS);
	save_item(NAME(m_probe_or_alias_region_shadow));
	save_item(NAME(m_m68302_internal_shadow));
	save_item(NAME(m_fc6860_reads_after_write));
	save_item(NAME(m_fc6860_busy_clear_logged));
	save_item(NAME(m_fdc_last_aux_command));
	save_item(NAME(m_fdc_last_msr));
	save_item(NAME(m_fdc_last_fifo_read));
	save_item(NAME(m_fdc_last_fifo_write));
	save_item(NAME(m_fdc_data_rate));
	save_item(NAME(m_fdc_data_rate_source));
	save_item(NAME(m_fdc_trace_sequence));
	save_item(NAME(m_fdc_transaction));
	save_item(NAME(m_fdc_transaction_access));
	save_item(NAME(m_fdc_fifo_transaction_reads));
	save_item(NAME(m_fdc_lowmem_watch));
	save_item(NAME(m_fdc_txn_read_bytes));
	save_item(NAME(m_fdc_txn_read_pcs));
	save_item(NAME(m_fdc_txn_write_bytes));
	save_item(NAME(m_fdc_txn_write_pcs));
	save_item(NAME(m_fdc_txn_read_msr));
	save_item(NAME(m_fdc_txn_write_msr));
	save_item(NAME(m_fdc_txn_read_count));
	save_item(NAME(m_fdc_txn_write_count));
	save_item(NAME(m_fdc_txn_summary_active));
	save_item(NAME(m_fdc_cmd0e_active));
	save_item(NAME(m_fdc_command_ring));
	save_item(NAME(m_fdc_command_ring_count));
	save_item(NAME(m_fdc_command_ring_next));
	save_item(NAME(m_fdc_cmd46_write_bytes));
	save_item(NAME(m_fdc_cmd46_write_pcs));
	save_item(NAME(m_fdc_cmd46_result_bytes));
	save_item(NAME(m_fdc_cmd46_result_pcs));
	save_item(NAME(m_fdc_cmd46_transaction));
	save_item(NAME(m_fdc_cmd46_write_count));
	save_item(NAME(m_fdc_cmd46_result_count));
	save_item(NAME(m_fdc_cmd46_active));
	save_item(NAME(m_fdc_cmd46_result_complete));
	save_item(NAME(m_prompt_select_trace_mask));
	save_item(NAME(m_04b0_countdown_trace_mask));
	save_item(NAME(m_media_branch_last_pc));
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
	save_item(NAME(m_insert_disk_decision_logged));
	save_item(NAME(m_seen_error_reboot_prompt));
	save_item(NAME(m_panel_reboot_confirm_injected));
	save_item(NAME(m_error009_origin_logged));
	save_item(NAME(m_lrclk_trace_count));
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
	m_insert_disk_decision_logged = false;
	m_seen_error_reboot_prompt = false;
	m_panel_reboot_confirm_injected = false;
	m_error009_origin_logged = false;
	m_lrclk_trace_count = 0;
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
	m_fc6860_reads_after_write = 0;
	m_fc6860_busy_clear_logged = false;
	m_fdc_last_aux_command = 0;
	m_fdc_last_msr = 0;
	m_fdc_last_fifo_read = 0;
	m_fdc_last_fifo_write = 0;
	m_fdc_data_rate = 250000;
	m_fdc_data_rate_source = 0;
	m_fdc_trace_sequence = 0;
	m_fdc_transaction = 0;
	m_fdc_transaction_access = 0;
	m_fdc_fifo_transaction_reads = 0;
	m_fdc_lowmem_watch = 0;
	m_fdc_txn_read_bytes.fill(0);
	m_fdc_txn_read_pcs.fill(0);
	m_fdc_txn_write_bytes.fill(0);
	m_fdc_txn_write_pcs.fill(0);
	m_fdc_txn_read_msr.fill(0);
	m_fdc_txn_write_msr.fill(0);
	m_fdc_txn_read_count = 0;
	m_fdc_txn_write_count = 0;
	m_fdc_txn_summary_active = false;
	m_fdc_cmd0e_active = false;
	m_fdc_command_ring.fill(0);
	m_fdc_command_ring_count = 0;
	m_fdc_command_ring_next = 0;
	m_fdc_cmd46_write_bytes.fill(0);
	m_fdc_cmd46_write_pcs.fill(0);
	m_fdc_cmd46_result_bytes.fill(0);
	m_fdc_cmd46_result_pcs.fill(0);
	m_fdc_cmd46_transaction = 0;
	m_fdc_cmd46_write_count = 0;
	m_fdc_cmd46_result_count = 0;
	m_fdc_cmd46_active = false;
	m_fdc_cmd46_result_complete = false;
	m_prompt_select_trace_mask = 0;
	m_04b0_countdown_trace_mask = 0;
	m_media_branch_last_pc = 0xffffffff;
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
		else if (m_seen_insert_disk_prompt && is_fdc_state_field(byte_address) && !machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10FDCSTATE_READ time=%s pc=%06x addr=%06x field=%s value=%04x mem_mask=%04x "
				"last_command=%02x phase=post_insert_disk\n",
				machine().time().to_string(), pc, byte_address, fdc_state_field_name(byte_address),
				data, mem_mask, m_fdc_last_aux_command);
			if constexpr (ASR10_LOG_FDC_04B0_CONTEXT)
			{
				if (byte_address == 0x04b0)
					log_fdc_04b0_context(false, mem_mask);
			}
		}
		if (byte_address == 0x04c6 && !machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc == 0x00fb8c4e)
			{
				const u8 masked_status = u8(data >> 8) & 0xc0;
				u16 branch_sr = u16(m_maincpu->state_int(M68K_SR)) & ~0x000f;
				if (!masked_status)
					branch_sr |= 0x0004;
				else if (BIT(masked_status, 7))
					branch_sr |= 0x0008;
				log_fb81b4_path("media_status_mask_c0", 0x00fb8c56,
					masked_status, masked_status != 0, 0x00fb8c5e, branch_sr,
					(u32(m_maincpu->state_int(M68K_D2)) & 0xffffff00) | masked_status);
			}
		}
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
	if (m_fdc_cmd46_result_complete && byte_address >= 0x04c6 && byte_address <= 0x04cc)
		log_fdc_cmd46_lowmem_store(byte_address, mem_mask);
	if constexpr (ASR10_LOG_FDC_04B0_CONTEXT)
	{
		if (m_seen_insert_disk_prompt && byte_address == 0x04b0)
			log_fdc_04b0_context(true, mem_mask);
	}
	if (byte_address == 0x04b0)
		log_04b0_countdown(m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff, 'W', previous, m_lowmem_shadow[offset]);
	if ((byte_address == 0x0b7e || byte_address == 0x0b80) &&
		(m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff) == 0x00f882de)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		logerror("ASR10_ERROR_ENTRY_STUB pc=%06x previous_pc=%06x addr=%06x data=%04x mem_mask=%04x "
			"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
			"sp=%06x stack0=%08x stack1=%08x stack2=%08x stack3=%08x "
			"lowmem_00c0=%04x lowmem_04c6=%04x lowmem_04c8=%04x lowmem_04ca=%04x lowmem_04cc=%04x\n",
			pc, m_last_distinct_pc, byte_address, data, mem_mask,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
			u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
			u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
			u16(m_maincpu->state_int(M68K_SR)), sp, read_stack_long(sp), read_stack_long(sp + 4),
			read_stack_long(sp + 8), read_stack_long(sp + 12),
			m_lowmem_shadow[0x00c0 >> 1], m_lowmem_shadow[0x04c6 >> 1],
			m_lowmem_shadow[0x04c8 >> 1], m_lowmem_shadow[0x04ca >> 1],
			m_lowmem_shadow[0x04cc >> 1]);
	}
	if (byte_address == 0x00c0)
		log_error009_context("error_number_write_00c0", m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
			m_lowmem_shadow[offset], mem_mask);
	if (byte_address == 0x04ee)
		log_lowmem_04ee(true, previous, m_lowmem_shadow[offset], mem_mask);
	else if (byte_address == 0x049c)
		log_lowmem_049d(true, previous, m_lowmem_shadow[offset], mem_mask);
	else if (byte_address == 0x04c6)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		if (ACCESSING_BITS_8_15 && pc == 0x00fb8db2 && m_fdc_last_aux_command == 0xf3 &&
			(read_stack_long(sp) & 0x00ffffff) == 0x00fb7c78)
		{
			log_04c6_origin("fifo_store", pc, u8(m_lowmem_shadow[offset] >> 8), false, 0);
		}
	}
	else if (m_seen_insert_disk_prompt && m_fdc_lowmem_watch && byte_address >= 0x0480 && byte_address <= 0x04fe)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10FDCSTATE time=%s seq=%llu txn=%u pc=%06x addr=%06x field=%s data=%04x mem_mask=%04x "
			"previous=%04x current=%04x last_command=%02x\n",
			machine().time().to_string(), (unsigned long long)m_fdc_trace_sequence, m_fdc_transaction,
			pc, byte_address, fdc_state_field_name(byte_address), data, mem_mask,
			previous, m_lowmem_shadow[offset], m_fdc_last_aux_command);
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
	const u16 shadow = m_m68302_internal_shadow[offset & 0x7f];
	u16 effective = shadow;
	if (ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE && address == 0x00fc6860 && mem_mask == 0xff00 &&
		!machine().side_effects_disabled())
	{
		const u8 original_byte = u8(shadow >> 8);
		if (m_fc6860_reads_after_write < ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_READ_DELAY)
		{
			m_fc6860_reads_after_write++;
		}
		else if (BIT(original_byte, 0))
		{
			const u8 effective_byte = original_byte & ~u8(0x01);
			effective = (shadow & 0x00ff) | (u16(effective_byte) << 8);
			if (!m_fc6860_busy_clear_logged && !machine().side_effects_disabled())
			{
				const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
				logerror("ASR10_EXPERIMENT_FC6860_CLEAR_BUSY bit0 1->0 original=%02x effective=%02x pc=%06x\n",
					original_byte, effective_byte, pc);
				m_fc6860_busy_clear_logged = true;
			}
		}
	}
	const u16 data = effective & mem_mask;
	trace_access(trace_region::M68302_INTERNAL, false, address, data, mem_mask, shadow);
	log_lrclk_candidate(false, address, data, mem_mask, shadow);
	if (address == 0x00fc6860 && !machine().side_effects_disabled())
	{
		const u8 relevant_byte = (mem_mask & 0xff00) ? u8(effective >> 8) : u8(effective);
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10_M68302_6860 pc=%06x rw=R data=%04x mem_mask=%04x relevant_byte=%02x "
			"bit0=%u last_write=%04x opcode=%04x detail=%s\n",
			pc, data, mem_mask, relevant_byte, BIT(relevant_byte, 0), shadow,
			read_code_word(pc), m68302_register_name(address));
	}
	return data;
}


void asr10_boot_state::m68302_internal_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00fc6800 | (offset << 1);
	COMBINE_DATA(&m_m68302_internal_shadow[offset & 0x7f]);
	trace_access(trace_region::M68302_INTERNAL, true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	log_lrclk_candidate(true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	if (address == 0x00fc6860 && !machine().side_effects_disabled())
	{
		const u16 shadow = m_m68302_internal_shadow[offset & 0x7f];
		const u8 relevant_byte = (mem_mask & 0xff00) ? u8(shadow >> 8) : u8(shadow);
		if (ASR10_EXPERIMENT_FC6860_CLEAR_BUSY_BIT0_AFTER_WRITE && mem_mask == 0xff00)
		{
			m_fc6860_reads_after_write = 0;
			m_fc6860_busy_clear_logged = false;
		}
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10_M68302_6860 pc=%06x rw=W data=%04x mem_mask=%04x relevant_byte=%02x "
			"bit0=%u last_write=%04x d0=%08x opcode=%04x detail=%s\n",
			pc, data, mem_mask, relevant_byte, BIT(relevant_byte, 0), shadow,
			u32(m_maincpu->state_int(M68K_D0)), read_code_word(pc), m68302_register_name(address));
	}

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
	++m_fdc_trace_sequence;
	++m_fdc_transaction_access;

	u8 raw_data = 0;
	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1)
	{
		raw_data = m_fdc->msr_r();
		m_fdc_last_msr = raw_data;
		if (m_fdc_cmd0e_active && m_fdc_txn_read_count && !(raw_data & 0x40))
			log_fdc_cmd0e_summary();
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
		if (m_fdc_last_aux_command == 0xf3)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			if ((read_stack_long(sp) & 0x00ffffff) == 0x00fb7c78)
			{
				if (pc == 0x00fb8d9a)
					log_04c6_origin("receive_rqm_bit7", 0x00fb8da2, raw_data,
						!BIT(raw_data, 7), 0x00fb8d86);
				else if (pc == 0x00fb8da8)
					log_04c6_origin("receive_dio_bit6", 0x00fb8db0, raw_data,
						!BIT(raw_data, 6), 0x00fb8dcc);
			}
		}
	}
	else if ((address & 3) == 3)
	{
		const u8 device_data = m_fdc->fifo_r();
		if (ASR10_EXPERIMENT_STUB_CMD1E_RESULTS && m_fdc_last_aux_command == 0x1e && m_fdc_txn_read_count < 2)
		{
			raw_data = m_fdc_txn_read_count ? ASR10_STUB_CMD1E_RESULT_BYTE1 : ASR10_STUB_CMD1E_RESULT_BYTE0;
			logerror("ASR10FDC_CMD1E_STUB index=%u result=%02x pc=%06x\n",
				m_fdc_txn_read_count, raw_data, pc);
		}
		else if (ASR10_EXPERIMENT_STUB_CMD0E_RESULT && m_fdc_last_aux_command == 0x0e)
		{
			raw_data = ASR10_STUB_CMD0E_RESULT_BYTE;
			if (m_fdc_txn_read_count == 0)
				logerror("ASR10FDC_CMD0E_STUB result=%02x pc=%06x\n", raw_data, pc);
		}
		else
		{
			raw_data = device_data;
		}
		m_fdc_last_fifo_read = raw_data;
		if (m_fdc_cmd46_active && m_fdc_cmd46_write_count == m_fdc_cmd46_write_bytes.size() &&
			BIT(m_fdc_last_msr, 6) && !BIT(m_fdc_last_msr, 5) &&
			m_fdc_cmd46_result_count < m_fdc_cmd46_result_bytes.size())
		{
			m_fdc_cmd46_result_bytes[m_fdc_cmd46_result_count] = raw_data;
			m_fdc_cmd46_result_pcs[m_fdc_cmd46_result_count] = pc;
			m_fdc_cmd46_result_count++;
			if (m_fdc_cmd46_result_count == m_fdc_cmd46_result_bytes.size())
				m_fdc_cmd46_result_complete = true;
		}
		if (pc == 0x00fb8db2 && m_fdc_last_aux_command == 0xf3)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			if ((read_stack_long(sp) & 0x00ffffff) == 0x00fb7c78)
				log_04c6_origin("fifo_read_fc4003", pc, raw_data, false, 0);
		}
		m_fdc_fifo_transaction_reads++;
		m_fdc_lowmem_watch = 8;
		if (m_fdc_txn_summary_active && m_fdc_txn_read_count < m_fdc_txn_read_bytes.size())
		{
			m_fdc_txn_read_bytes[m_fdc_txn_read_count] = raw_data;
			m_fdc_txn_read_pcs[m_fdc_txn_read_count] = pc;
			m_fdc_txn_read_msr[m_fdc_txn_read_count] = m_fdc_last_msr;
			m_fdc_txn_read_count++;
		}
		detail = "upd72069_fifo_read";
	}

	const u16 result = raw_data & mem_mask;
	if constexpr (ASR10_LOG_FDC_ACCESS)
		logerror("ASR10FDC time=%s seq=%llu txn=%u txn_access=%u pc=%06x addr=%06x rw=R data=%04x "
			"mem_mask=%04x last_aux_command=%02x last_data_read=%02x last_data_write=%02x "
			"transaction_reads=%u phase=%s panel=\"%s\" detail=%s\n",
			machine().time().to_string(), (unsigned long long)m_fdc_trace_sequence,
			m_fdc_transaction, m_fdc_transaction_access,
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
	++m_fdc_trace_sequence;

	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1 && ACCESSING_BITS_0_7)
	{
		if (m_fdc_txn_summary_active)
		{
			if (m_fdc_last_aux_command == 0x88 || m_fdc_last_aux_command == 0xf3)
				log_fdc_88_f3_summary();
			log_fdc_txn_summary();
		}
		m_fdc_transaction++;
		m_fdc_transaction_access = 0;
		m_fdc_fifo_transaction_reads = 0;
		m_fdc_last_aux_command = u8(data);
		m_fdc_command_ring[m_fdc_command_ring_next] = m_fdc_last_aux_command;
		m_fdc_command_ring_next = (m_fdc_command_ring_next + 1) % m_fdc_command_ring.size();
		if (m_fdc_command_ring_count < m_fdc_command_ring.size())
			m_fdc_command_ring_count++;
		m_fdc_txn_read_bytes.fill(0);
		m_fdc_txn_read_pcs.fill(0);
		m_fdc_txn_write_bytes.fill(0);
		m_fdc_txn_write_pcs.fill(0);
		m_fdc_txn_read_msr.fill(0);
		m_fdc_txn_write_msr.fill(0);
		m_fdc_txn_read_count = 0;
		m_fdc_txn_write_count = 0;
		m_fdc_txn_summary_active =
			m_fdc_last_aux_command == 0x0b ||
			m_fdc_last_aux_command == 0x4f ||
			m_fdc_last_aux_command == 0x1e ||
			m_fdc_last_aux_command == 0x0e ||
			m_fdc_last_aux_command == 0x88 ||
			m_fdc_last_aux_command == 0xf3;
		m_fdc_cmd0e_active = (m_fdc_last_aux_command == 0x0e);
		if ((m_fdc_last_aux_command & 0x0f) == 0x08)
		{
			switch (m_fdc_last_aux_command & 0x70)
			{
			case 0x00:
				m_fdc_data_rate = 250000;
				break;
			case 0x10:
			case 0x40:
				m_fdc_data_rate = 500000;
				break;
			case 0x20:
			case 0x70:
				m_fdc_data_rate = 600000;
				break;
			case 0x30:
				m_fdc_data_rate = 300000;
				break;
			case 0x50:
				m_fdc_data_rate = 1000000;
				break;
			case 0x60:
				m_fdc_data_rate = 1250000;
				break;
			}
			m_fdc_data_rate_source = m_fdc_last_aux_command;
		}
		if (m_fdc_cmd0e_active)
		{
			m_prompt_select_trace_mask = 0;
			m_04b0_countdown_trace_mask = 0;
			m_media_branch_last_pc = 0xffffffff;
			m_prompt_select_timer->adjust(attotime::zero, 0, attotime::from_ticks(1, m_maincpu->clock()));
		}
		m_fdc->auxcmd_w(m_fdc_last_aux_command);
		if (ASR10_EXPERIMENT_CMD88_RATE_500K && m_fdc_last_aux_command == 0x88)
		{
			m_fdc->set_rate(500000);
			m_fdc_data_rate = 500000;
		}
		if (m_fdc_last_aux_command == 0x88 || m_fdc_last_aux_command == 0xf3)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			floppy_image_device *const floppy = m_floppy_connector->get_device();
			logerror("ASR10_FDC_CMD%02X txn=%u event=aux_write pc=%06x value=%02x "
				"sp=%06x stack0=%08x stack1=%08x stack2=%08x "
				"meaning=%s experiment_cmd88_rate_500k=%u effective_data_rate=%u "
				"drive_attached=%u media_mounted=%u ready=%u motor=%u density=%s\n",
				m_fdc_last_aux_command, m_fdc_transaction, pc, m_fdc_last_aux_command,
				sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
				m_fdc_last_aux_command == 0x88 ? "control_data_rate_250000_asr_experiment_forces_500000" : "precompensation",
				ASR10_EXPERIMENT_CMD88_RATE_500K && m_fdc_last_aux_command == 0x88 ? 1 : 0,
				m_fdc_data_rate,
				floppy ? 1 : 0, floppy && floppy->exists() ? 1 : 0,
				floppy && !floppy->ready_r() ? 1 : 0, floppy && !floppy->mon_r() ? 1 : 0,
				floppy && floppy->floppy_is_hd() ? "hd" : "dd");
		}
		detail = (m_fdc_last_aux_command == 0x36) ? "upd72069_software_reset" : "upd72069_aux_command";
	}
	else if ((address & 3) == 3 && ACCESSING_BITS_0_7)
	{
		m_fdc_last_fifo_write = u8(data);
		if (!m_fdc_cmd46_active && m_fdc_last_fifo_write == 0x46)
		{
			floppy_image_device *const floppy = m_floppy_connector->get_device();
			const floppy_image_format_t *const format = floppy ? floppy->get_load_format() : nullptr;
			m_fdc_cmd46_transaction++;
			m_fdc_cmd46_write_bytes.fill(0);
			m_fdc_cmd46_write_pcs.fill(0);
			m_fdc_cmd46_result_bytes.fill(0);
			m_fdc_cmd46_result_pcs.fill(0);
			m_fdc_cmd46_write_count = 0;
			m_fdc_cmd46_result_count = 0;
			m_fdc_cmd46_active = true;
			m_fdc_cmd46_result_complete = false;
			logerror("ASR10_FDC_CMD46 txn=%u event=start pc=%06x "
				"format=%s media_mounted=%u ready=%u motor=%u current_cylinder=%d current_side=%u "
				"drive_sides=%d density=%s data_rate=%u data_rate_source=%02x "
				"read_source=upd72069_device stubbed=0\n",
				m_fdc_cmd46_transaction, pc, format ? format->name() : "none",
				floppy && floppy->exists() ? 1 : 0, floppy && !floppy->ready_r() ? 1 : 0,
				floppy && !floppy->mon_r() ? 1 : 0, floppy ? floppy->get_cyl() : -1,
				floppy ? floppy->ss_r() : 0, floppy ? floppy->get_sides() : 0,
				floppy && floppy->floppy_is_hd() ? "hd" : "dd",
				m_fdc_data_rate, m_fdc_data_rate_source);
		}
		if (m_fdc_cmd46_active && m_fdc_cmd46_write_count < m_fdc_cmd46_write_bytes.size())
		{
			m_fdc_cmd46_write_bytes[m_fdc_cmd46_write_count] = m_fdc_last_fifo_write;
			m_fdc_cmd46_write_pcs[m_fdc_cmd46_write_count] = pc;
			m_fdc_cmd46_write_count++;
		}
		m_fdc->fifo_w(m_fdc_last_fifo_write);
		m_fdc_lowmem_watch = 8;
		if (m_fdc_txn_summary_active && m_fdc_txn_write_count < m_fdc_txn_write_bytes.size())
		{
			m_fdc_txn_write_bytes[m_fdc_txn_write_count] = m_fdc_last_fifo_write;
			m_fdc_txn_write_pcs[m_fdc_txn_write_count] = pc;
			m_fdc_txn_write_msr[m_fdc_txn_write_count] = m_fdc_last_msr;
			m_fdc_txn_write_count++;
		}
		detail = "upd72069_fifo";
	}
	++m_fdc_transaction_access;

	if constexpr (ASR10_LOG_FDC_ACCESS)
		logerror("ASR10FDC time=%s seq=%llu txn=%u txn_access=%u pc=%06x addr=%06x rw=W data=%04x "
			"mem_mask=%04x last_aux_command=%02x last_data_read=%02x last_data_write=%02x "
			"transaction_reads=%u phase=%s panel=\"%s\" detail=%s\n",
			machine().time().to_string(), (unsigned long long)m_fdc_trace_sequence,
			m_fdc_transaction, m_fdc_transaction_access,
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
		if (ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 && pc == 0x00fb7c84)
		{
			raw_data |= 0x10;
			logerror("ASR10_DUART_INPUT_STUB pc=fb7c84 addr=fc4809 set_mask=10 value=%02x\n", u8(raw_data));
		}
		if (pc == 0x00fb7c30 || pc == 0x00fb7c84)
		{
			const bool semantic_reader = (pc == 0x00fb7c84);
			logerror("ASR10_DUART_INPUT pc=%06x addr=fc4809 value=%02x "
				"role=%s tested_mask=%02x branch_pc=%06x branch_taken=%u branch_target=%06x\n",
				pc, u8(raw_data),
				semantic_reader ? "semantic_input_change_bit4" : "ack_input_change_latch",
				semantic_reader ? 0x10 : 0x00,
				semantic_reader ? 0x00fb7c8c : 0,
				semantic_reader && !BIT(raw_data, 4) ? 1 : 0,
				semantic_reader ? 0x00fb7c92 : 0);
			if (semantic_reader)
				logerror("ASR10_INPUT_BRANCH pc=fb7c8c opcode=6704 tested=duart_fc4809_bit4 "
					"value=%02x branch_taken=%u branch_target=fb7c92 "
					"not_taken_path=set_semantic_result_01\n",
					u8(raw_data), BIT(raw_data, 4) ? 0 : 1);
		}
	}
	if (ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21 &&
		pc == 0x00f89cea && address == 0x00fc4816 && ACCESSING_BITS_0_7 &&
		m_seen_error_reboot_prompt && !m_panel_reboot_confirm_injected &&
		!machine().side_effects_disabled())
	{
		raw_data = 0x21;
		m_panel_reboot_confirm_injected = true;
		logerror("ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM raw=21 mapped=23 pc=f89cea\n");
	}

	const u16 data = raw_data & mem_mask;
	if (!machine().side_effects_disabled())
	{
		if (pc == 0x00f89cd8 && address == 0x00fc4812 && ACCESSING_BITS_0_7)
		{
			const u8 status = u8(data);
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			logerror("ASR10_PANEL_INPUT_STATUS pc=%06x data=%04x mem_mask=%04x bit0_ready=%u "
				"d0=%08x d1=%08x sp=%06x stack0=%08x stack1=%08x stack2=%08x\n",
				pc, data, mem_mask, BIT(status, 0),
				u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
				sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8));
		}
		else if (pc == 0x00f89cea && address == 0x00fc4816 && ACCESSING_BITS_0_7)
		{
			const u8 raw_byte = u8(data);
			const u32 mapped_address = 0x00f82484 + raw_byte;
			const u16 mapped_word = read_code_word(mapped_address & ~1U);
			const u8 mapped_byte = BIT(mapped_address, 0) ? u8(mapped_word) : u8(mapped_word >> 8);
			logerror("ASR10_PANEL_INPUT_BYTE pc=%06x raw=%02x mapped=%02x "
				"accepted_reboot_confirm=%u mapped_23=%u mapped_40=%u mapped_17=%u mapped_16=%u\n",
				pc, raw_byte, mapped_byte, mapped_byte == 0x23 ? 1 : 0,
				mapped_byte == 0x23 ? 1 : 0, mapped_byte == 0x40 ? 1 : 0,
				mapped_byte == 0x17 ? 1 : 0, mapped_byte == 0x16 ? 1 : 0);
		}
	}
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
		if constexpr (ASR10_LOG_PANEL_BYTES)
		{
			if (character >= 0x20 && character <= 0x7e)
				logerror("ASR10PANEL char='%c' hex=%02x pc=%06x\n", character, character, pc);
			else
				logerror("ASR10PANEL control=%02x pc=%06x\n", character, pc);
		}
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

	if (!m_insert_disk_decision_logged && strstr(m_panel_text, "PLEASE INSERT DISK"))
	{
		m_insert_disk_decision_logged = true;
		log_insert_disk_decision(pc);
		if (!m_seen_insert_disk_prompt)
		{
			m_seen_insert_disk_prompt = true;
			m_trace_slots = {};
			logerror("ASR10PHASE phase=post_insert_disk_prompt pc=%06x\n", pc);
		}
	}
}


void asr10_boot_state::flush_panel_text()
{
	if (m_panel_text_length)
	{
		logerror("ASR10PANEL text=\"%s\"\n", m_panel_text);
		if (strstr(m_panel_text, "ERROR 009 - REBOOT ?"))
		{
			m_seen_error_reboot_prompt = true;
			log_error009_context("panel_error009_text",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
				m_lowmem_shadow[0x00c0 >> 1], 0xffff);
		}
	}

	m_panel_text_length = 0;
	m_insert_disk_decision_logged = false;
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


void asr10_boot_state::log_insert_disk_decision(u32 pc)
{
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	const u32 return_address = likely_rom_address(stack0) ? (stack0 & 0x00ffffff) : 0xffffffffU;
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	logerror("ASR10_INSERT_DISK_DECISION pc=%06x last_distinct_pc=%06x sp=%06x "
		"stack0=%08x stack1=%08x stack2=%08x guessed_return=%08x "
		"last_command=%02x last_fifo_read=%02x last_fifo_write=%02x txn=%u recent_commands=\"%s\" "
		"field_04a6=%04x field_04ae=%04x field_04b0=%04x field_04b4=%04x field_04b6=%04x "
		"field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x\n",
		pc, m_last_distinct_pc, sp, stack0, stack1, stack2, return_address,
		m_fdc_last_aux_command, m_fdc_last_fifo_read, m_fdc_last_fifo_write,
		m_fdc_transaction, command_sequence.c_str(),
		m_lowmem_shadow[0x04a6 >> 1], m_lowmem_shadow[0x04ae >> 1],
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04b4 >> 1],
		m_lowmem_shadow[0x04b6 >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1]);
}


void asr10_boot_state::log_prompt_select(u32 pc)
{
	u8 trace_bit = 0;
	const char *landmark = "unknown";
	switch (pc)
	{
	case 0x00fb9490:
		trace_bit = 0x01;
		landmark = "prompt_select_read_049d";
		break;
	case 0x00fb9494:
		trace_bit = 0x02;
		landmark = "prompt_select_compare";
		break;
	case 0x00fb9496:
		trace_bit = 0x04;
		landmark = "prompt_select_branch";
		break;
	case 0x00fb94ae:
		trace_bit = 0x08;
		landmark = "prompt_select_compare";
		break;
	case 0x00fb94b2:
		trace_bit = 0x10;
		landmark = "prompt_select_branch";
		break;
	case 0x00fb94b4:
		trace_bit = 0x20;
		landmark = "prompt_select_panel_call";
		break;
	case 0x00fb94da:
		trace_bit = 0x40;
		landmark = "prompt_select_panel_call";
		break;
	case 0x00fb94e0:
		trace_bit = 0x80;
		landmark = "prompt_select_return";
		break;
	}
	if (!trace_bit || (m_prompt_select_trace_mask & trace_bit))
		return;
	m_prompt_select_trace_mask |= trace_bit;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	const u32 stack3 = read_stack_long(sp + 12);
	const u16 opcode = read_code_word(pc);
	u32 branch_target = 0xffffffffU;
	if ((opcode & 0xf000) == 0x6000)
	{
		const s8 displacement = s8(opcode);
		branch_target = displacement ?
			((pc + 2 + displacement) & 0x00ffffff) :
			((pc + 2 + s16(read_code_word(pc + 2))) & 0x00ffffff);
	}
	else if ((opcode & 0xf0f8) == 0x50c8)
	{
		branch_target = (pc + 2 + s16(read_code_word(pc + 2))) & 0x00ffffff;
	}
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}
	const u16 lowmem_049d_word = m_lowmem_shadow[0x049c >> 1];

	logerror("ASR10_PROMPT_SELECT landmark=%s pc=%06x "
		"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x "
		"op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x stack3=%08x "
		"lowmem_049d_word=%04x lowmem_049d_byte=%02x branch_target=%08x "
		"recent_commands=\"%s\" last_command=%02x last_fifo_read=%02x last_fifo_write=%02x "
		"field_04a6=%04x field_04ae=%04x field_04b0=%04x field_04b4=%04x field_04b6=%04x "
		"field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x\n",
		landmark, pc,
		read_code_word(pc - 8), read_code_word(pc - 6), read_code_word(pc - 4),
		read_code_word(pc - 2), read_code_word(pc), read_code_word(pc + 2),
		read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), sp, stack0, stack1, stack2, stack3,
		lowmem_049d_word, lowmem_049d_word & 0xff, branch_target,
		command_sequence.c_str(), m_fdc_last_aux_command, m_fdc_last_fifo_read, m_fdc_last_fifo_write,
		m_lowmem_shadow[0x04a6 >> 1], m_lowmem_shadow[0x04ae >> 1],
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04b4 >> 1],
		m_lowmem_shadow[0x04b6 >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1]);
}


void asr10_boot_state::log_04b0_countdown(u32 pc, char rw, u16 previous, u16 current)
{
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	logerror("ASR10_04B0_COUNTDOWN pc=%06x rw=%c previous=%04x current=%04x high_byte=%u "
		"d0=%08x d1=%08x d2=%08x d3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x "
		"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x "
		"op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"last_command=%02x last_fifo_read=%02x txn=%u recent_commands=\"%s\"\n",
		pc, rw, previous, current, current >> 8,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u16(m_maincpu->state_int(M68K_SR)), sp, stack0, stack1, stack2,
		read_code_word(pc - 8), read_code_word(pc - 6), read_code_word(pc - 4),
		read_code_word(pc - 2), read_code_word(pc), read_code_word(pc + 2),
		read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
		m_fdc_last_aux_command, m_fdc_last_fifo_read, m_fdc_transaction, command_sequence.c_str());
}


void asr10_boot_state::log_media_branch(u32 pc, u16 sr_override)
{
	const u16 opcode = read_code_word(pc);
	const u16 sr = (sr_override == 0xffff) ? u16(m_maincpu->state_int(M68K_SR)) : sr_override;
	const bool carry = BIT(sr, 0);
	const bool overflow = BIT(sr, 1);
	const bool zero = BIT(sr, 2);
	const bool negative = BIT(sr, 3);
	const u8 condition = (opcode >> 8) & 0x0f;
	bool branch_taken = false;
	switch (condition)
	{
	case 0x0: branch_taken = true; break;
	case 0x2: branch_taken = !carry && !zero; break;
	case 0x3: branch_taken = carry || zero; break;
	case 0x4: branch_taken = !carry; break;
	case 0x5: branch_taken = carry; break;
	case 0x6: branch_taken = !zero; break;
	case 0x7: branch_taken = zero; break;
	case 0x8: branch_taken = !overflow; break;
	case 0x9: branch_taken = overflow; break;
	case 0xa: branch_taken = !negative; break;
	case 0xb: branch_taken = negative; break;
	case 0xc: branch_taken = negative == overflow; break;
	case 0xd: branch_taken = negative != overflow; break;
	case 0xe: branch_taken = !zero && (negative == overflow); break;
	case 0xf: branch_taken = zero || (negative != overflow); break;
	}
	const s8 displacement = s8(opcode);
	const u32 branch_target = displacement ?
		((pc + 2 + displacement) & 0x00ffffff) :
		((pc + 2 + s16(read_code_word(pc + 2))) & 0x00ffffff);
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	logerror("ASR10_MEDIA_BRANCH pc=%06x opcode=%04x sr=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"branch_taken=%u branch_target=%06x lowmem_049d=%02x "
		"field_04b0=%04x field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x "
		"last_command=%02x last_fifo_read=%02x recent_commands=\"%s\"\n",
		pc, opcode, sr,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		branch_taken ? 1 : 0, branch_target, u8(m_lowmem_shadow[0x049c >> 1]),
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1], m_fdc_last_aux_command,
		m_fdc_last_fifo_read, command_sequence.c_str());
}


void asr10_boot_state::log_fb81b4_path(const char *landmark, u32 pc, u8 tested_value, bool branch_taken,
	u32 branch_target, u16 sr_override, u32 d2_override)
{
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	logerror("ASR10_FB81B4 landmark=%s pc=%06x opcode=%04x tested_value=%02x "
		"branch_taken=%u branch_target=%06x "
		"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x "
		"op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x "
		"lowmem_049d=%02x lowmem_04ee=%02x field_04b0=%04x field_04ae=%04x "
		"field_04c6=%04x field_04d6=%04x field_04e6=%04x "
		"last_command=%02x last_fifo_read=%02x last_fifo_write=%02x recent_commands=\"%s\"\n",
		landmark, pc, read_code_word(pc), tested_value,
		branch_taken ? 1 : 0, branch_target,
		read_code_word(pc - 8), read_code_word(pc - 6), read_code_word(pc - 4),
		read_code_word(pc - 2), read_code_word(pc), read_code_word(pc + 2),
		read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		d2_override == 0xffffffff ? u32(m_maincpu->state_int(M68K_D2)) : d2_override,
		u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		sr_override == 0xffff ? u16(m_maincpu->state_int(M68K_SR)) : sr_override,
		sp, stack0, stack1, stack2,
		u8(m_lowmem_shadow[0x049c >> 1]), u8(m_lowmem_shadow[0x04ee >> 1] >> 8),
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04ae >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1], m_fdc_last_aux_command,
		m_fdc_last_fifo_read, m_fdc_last_fifo_write, command_sequence.c_str());
}


void asr10_boot_state::log_04c6_origin(const char *landmark, u32 pc, u8 value, bool branch_taken, u32 branch_target)
{
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	logerror("ASR10_04C6 landmark=%s pc=%06x opcode=%04x value=%02x "
		"branch_taken=%u branch_target=%06x "
		"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x "
		"op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x "
		"lowmem_049d=%02x lowmem_04ee=%02x field_04b0=%04x field_04ac=%04x "
		"field_04ae=%04x field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x "
		"last_command=%02x last_fifo_read=%02x last_fifo_write=%02x recent_commands=\"%s\"\n",
		landmark, pc, read_code_word(pc), value, branch_taken ? 1 : 0, branch_target,
		read_code_word(pc - 8), read_code_word(pc - 6), read_code_word(pc - 4),
		read_code_word(pc - 2), read_code_word(pc), read_code_word(pc + 2),
		read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), sp, stack0, stack1, stack2,
		u8(m_lowmem_shadow[0x049c >> 1]), u8(m_lowmem_shadow[0x04ee >> 1] >> 8),
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04ac >> 1],
		m_lowmem_shadow[0x04ae >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1], m_fdc_last_aux_command,
		m_fdc_last_fifo_read, m_fdc_last_fifo_write, command_sequence.c_str());
}


void asr10_boot_state::log_error009_context(const char *source, u32 pc, u16 value, u16 mem_mask)
{
	if (m_error009_origin_logged && strcmp(source, "panel_error009_text"))
		return;

	const u8 error_number = u8(value);
	if (error_number != 0x09 && strcmp(source, "panel_error009_text"))
		return;

	if (strcmp(source, "panel_error009_text"))
		m_error009_origin_logged = true;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	std::string command_sequence;
	const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
	for (u8 index = 0; index < m_fdc_command_ring_count; index++)
	{
		if (index)
			command_sequence += ',';
		command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
	}

	const u8 last_st0 = m_fdc_cmd46_result_bytes[0];
	const u8 last_st1 = m_fdc_cmd46_result_bytes[1];
	const u8 last_st2 = m_fdc_cmd46_result_bytes[2];

	logerror("ASR10_ERROR009_CONTEXT source=%s pc=%06x previous_pc=%06x opcode=%04x value=%04x mem_mask=%04x "
		"error_number=%02x sr=%04x d0=%08x d1=%08x d2=%08x d3=%08x "
		"a0=%08x a1=%08x a2=%08x a3=%08x sp=%06x "
		"stack0=%08x stack1=%08x stack2=%08x stack3=%08x stack4=%08x stack5=%08x "
		"lowmem_00c0=%04x lowmem_049d=%02x lowmem_04ae=%04x lowmem_04b0=%04x "
		"lowmem_04c6=%04x lowmem_04c8=%04x lowmem_04ca=%04x lowmem_04cc=%04x "
		"lowmem_04ee=%02x panel=\"%s\" "
		"last_fdc_txn=%u last_aux=%02x last_fifo_read=%02x last_fifo_write=%02x recent_commands=\"%s\" "
		"last_cmd46_bytes=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x "
		"last_cmd46_result=%02x,%02x,%02x,%02x,%02x,%02x,%02x "
		"last_ST0_invalid=%u last_ST0_abnormal=%u last_ST0_seek_end=%u last_ST0_equipment_check=%u last_ST0_not_ready=%u "
		"last_ST1_end_of_cylinder=%u last_ST1_data_error=%u last_ST1_overrun=%u last_ST1_no_data=%u "
		"last_ST1_not_writable=%u last_ST1_missing_address_mark=%u "
		"last_ST2_control_mark=%u last_ST2_data_error=%u last_ST2_wrong_cylinder=%u last_ST2_scan_equal=%u "
		"last_ST2_scan_not_satisfied=%u last_ST2_bad_cylinder=%u last_ST2_missing_data_address_mark=%u "
		"data_rate=%u data_rate_source=%02x\n",
		source, pc, m_last_distinct_pc, read_code_word(pc), value, mem_mask, error_number,
		u16(m_maincpu->state_int(M68K_SR)),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
		read_stack_long(sp + 12), read_stack_long(sp + 16), read_stack_long(sp + 20),
		m_lowmem_shadow[0x00c0 >> 1], u8(m_lowmem_shadow[0x049c >> 1]),
		m_lowmem_shadow[0x04ae >> 1], m_lowmem_shadow[0x04b0 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04c8 >> 1],
		m_lowmem_shadow[0x04ca >> 1], m_lowmem_shadow[0x04cc >> 1],
		u8(m_lowmem_shadow[0x04ee >> 1] >> 8), m_panel_text,
		m_fdc_cmd46_transaction, m_fdc_last_aux_command, m_fdc_last_fifo_read,
		m_fdc_last_fifo_write, command_sequence.c_str(),
		m_fdc_cmd46_write_bytes[0], m_fdc_cmd46_write_bytes[1], m_fdc_cmd46_write_bytes[2],
		m_fdc_cmd46_write_bytes[3], m_fdc_cmd46_write_bytes[4], m_fdc_cmd46_write_bytes[5],
		m_fdc_cmd46_write_bytes[6], m_fdc_cmd46_write_bytes[7], m_fdc_cmd46_write_bytes[8],
		last_st0, last_st1, last_st2,
		m_fdc_cmd46_result_bytes[3], m_fdc_cmd46_result_bytes[4], m_fdc_cmd46_result_bytes[5],
		m_fdc_cmd46_result_bytes[6],
		BIT(last_st0, 7), BIT(last_st0, 6), BIT(last_st0, 5), BIT(last_st0, 4), BIT(last_st0, 3),
		BIT(last_st1, 7), BIT(last_st1, 5), BIT(last_st1, 4), BIT(last_st1, 2), BIT(last_st1, 1), BIT(last_st1, 0),
		BIT(last_st2, 6), BIT(last_st2, 5), BIT(last_st2, 4), BIT(last_st2, 3), BIT(last_st2, 2), BIT(last_st2, 1), BIT(last_st2, 0),
		m_fdc_data_rate, m_fdc_data_rate_source);
}


void asr10_boot_state::log_lrclk_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 shadow)
{
	if (machine().side_effects_disabled())
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (pc < 0x00f8c100 || pc > 0x00f8c180)
		return;

	m_lrclk_trace_count++;
	if (m_lrclk_trace_count > 96 && (m_lrclk_trace_count & (m_lrclk_trace_count - 1)))
		return;

	const bool low_byte = bool(mem_mask & 0x00ff);
	const bool high_byte = bool(mem_mask & 0xff00);
	const u8 relevant_byte = low_byte ? u8(data) : u8(data >> 8);
	const u8 shadow_byte = low_byte ? u8(shadow) : u8(shadow >> 8);
	const bool lrclk_candidate = (address == 0x00fc6828) && low_byte;
	const s32 bit3_state = lrclk_candidate ? s32(BIT(relevant_byte, 3)) : -1;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;

	logerror("ASR10_LRCLK_CANDIDATE pc=%06x opcode=%04x rw=%c addr=%06x detail=%s "
		"data=%04x mem_mask=%04x selected_byte=%s relevant_byte=%02x bit3_lrclk_candidate=%d "
		"shadow=%04x shadow_relevant_byte=%02x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x trace_count=%u\n",
		pc, read_code_word(pc), write ? 'W' : 'R', address, m68302_register_name(address),
		data, mem_mask, high_byte && !low_byte ? "high" : low_byte && !high_byte ? "low" : "word",
		relevant_byte, bit3_state, shadow, shadow_byte,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u16(m_maincpu->state_int(M68K_SR)),
		sp, read_stack_long(sp), read_stack_long(sp + 4), m_lrclk_trace_count);
}


void asr10_boot_state::log_fdc_04b0_context(bool write, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u16 value = m_lowmem_shadow[0x04b0 >> 1];

	logerror("ASR10FDC04B0_CONTEXT pc=%06x "
		"op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"value=%04x high_byte=%u mem_mask=%04x rw=%c last_command=%02x\n",
		pc,
		read_code_word(pc - 6), read_code_word(pc - 4), read_code_word(pc - 2), read_code_word(pc),
		read_code_word(pc + 2), read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), value, value >> 8, mem_mask,
		write ? 'W' : 'R', m_fdc_last_aux_command);
}


void asr10_boot_state::log_fdc_cmd0e_summary()
{
	std::string result_bytes;
	std::string result_pcs;
	for (u8 index = 0; index < m_fdc_txn_read_count; index++)
	{
		if (index)
		{
			result_bytes += ',';
			result_pcs += ',';
		}
		result_bytes += util::string_format("%02x", m_fdc_txn_read_bytes[index]);
		result_pcs += util::string_format("%06x", m_fdc_txn_read_pcs[index]);
	}

	logerror("ASR10FDC_CMD0E_SUMMARY txn=%u command=%02x result_count=%u fifo_bytes=\"%s\" "
		"fifo_read_pcs=\"%s\" field_04b0=%04x field_04c4=%04x field_04c6=%04x "
		"field_04d6=%04x field_04e6=%04x insert_disk_phase=%u\n",
		m_fdc_transaction, m_fdc_last_aux_command, m_fdc_txn_read_count,
		result_bytes.c_str(), result_pcs.c_str(),
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1], m_seen_insert_disk_prompt ? 1 : 0);

	m_fdc_cmd0e_active = false;
}


void asr10_boot_state::log_fdc_txn_summary()
{
	std::string read_bytes;
	std::string read_pcs;
	std::string write_bytes;
	std::string write_pcs;
	for (u8 index = 0; index < m_fdc_txn_read_count; index++)
	{
		if (index)
		{
			read_bytes += ',';
			read_pcs += ',';
		}
		read_bytes += util::string_format("%02x", m_fdc_txn_read_bytes[index]);
		read_pcs += util::string_format("%06x", m_fdc_txn_read_pcs[index]);
	}
	for (u8 index = 0; index < m_fdc_txn_write_count; index++)
	{
		if (index)
		{
			write_bytes += ',';
			write_pcs += ',';
		}
		write_bytes += util::string_format("%02x", m_fdc_txn_write_bytes[index]);
		write_pcs += util::string_format("%06x", m_fdc_txn_write_pcs[index]);
	}

	logerror("ASR10FDC_TXN_SUMMARY txn=%u command=%02x result_count=%u fifo_bytes=\"%s\" "
		"fifo_read_pcs=\"%s\" fifo_write_count=%u fifo_write_bytes=\"%s\" fifo_write_pcs=\"%s\" "
		"field_04a6=%04x field_04ae=%04x field_04b0=%04x field_04b4=%04x field_04b6=%04x "
		"field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x insert_disk_phase=%u\n",
		m_fdc_transaction, m_fdc_last_aux_command, m_fdc_txn_read_count,
		read_bytes.c_str(), read_pcs.c_str(), m_fdc_txn_write_count,
		write_bytes.c_str(), write_pcs.c_str(),
		m_lowmem_shadow[0x04a6 >> 1], m_lowmem_shadow[0x04ae >> 1],
		m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04b4 >> 1],
		m_lowmem_shadow[0x04b6 >> 1], m_lowmem_shadow[0x04c4 >> 1],
		m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
		m_lowmem_shadow[0x04e6 >> 1], m_seen_insert_disk_prompt ? 1 : 0);

	m_fdc_txn_summary_active = false;
}


void asr10_boot_state::log_fdc_88_f3_summary()
{
	// Confirmed media probe: 88 selects 250 kbit/s, F3 is the precompensation
	// auxiliary command, then 03 specifies timing, 07 recalibrates drive 0, and
	// 08 senses interrupt status.  With no floppy device or media attached, the
	// current device returns ST0=68 (failure, seek end, drive not ready).
	std::string read_bytes;
	std::string read_pcs;
	std::string read_msr;
	std::string write_bytes;
	std::string write_pcs;
	std::string write_msr;
	floppy_image_device *const floppy = m_floppy_connector->get_device();
	for (u8 index = 0; index < m_fdc_txn_read_count; index++)
	{
		if (index)
		{
			read_bytes += ',';
			read_pcs += ',';
			read_msr += ',';
		}
		read_bytes += util::string_format("%02x", m_fdc_txn_read_bytes[index]);
		read_pcs += util::string_format("%06x", m_fdc_txn_read_pcs[index]);
		read_msr += util::string_format("%02x", m_fdc_txn_read_msr[index]);
	}
	for (u8 index = 0; index < m_fdc_txn_write_count; index++)
	{
		if (index)
		{
			write_bytes += ',';
			write_pcs += ',';
			write_msr += ',';
		}
		write_bytes += util::string_format("%02x", m_fdc_txn_write_bytes[index]);
		write_pcs += util::string_format("%06x", m_fdc_txn_write_pcs[index]);
		write_msr += util::string_format("%02x", m_fdc_txn_write_msr[index]);
	}

	logerror("ASR10_FDC_CMD%02X txn=%u event=summary "
		"fifo_reads=\"%s\" fifo_read_pcs=\"%s\" msr_before_reads=\"%s\" "
		"fifo_writes=\"%s\" fifo_write_pcs=\"%s\" msr_before_writes=\"%s\" "
		"read_source=upd72069_device stubbed=0 "
		"drive_attached=%u media_mounted=%u ready=%u motor=%u density=%s "
		"field_049d=%04x field_04ac=%04x field_04ae=%04x field_04b0=%04x "
		"field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x\n",
		m_fdc_last_aux_command, m_fdc_transaction,
		read_bytes.c_str(), read_pcs.c_str(), read_msr.c_str(),
		write_bytes.c_str(), write_pcs.c_str(), write_msr.c_str(),
		floppy ? 1 : 0, floppy && floppy->exists() ? 1 : 0,
		floppy && !floppy->ready_r() ? 1 : 0, floppy && !floppy->mon_r() ? 1 : 0,
		floppy && floppy->floppy_is_hd() ? "hd" : "dd",
		m_lowmem_shadow[0x049c >> 1], m_lowmem_shadow[0x04ac >> 1],
		m_lowmem_shadow[0x04ae >> 1], m_lowmem_shadow[0x04b0 >> 1],
		m_lowmem_shadow[0x04c4 >> 1], m_lowmem_shadow[0x04c6 >> 1],
		m_lowmem_shadow[0x04d6 >> 1], m_lowmem_shadow[0x04e6 >> 1]);
}

void asr10_boot_state::log_fdc_cmd46_lowmem_store(u32 byte_address, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;

	const u32 word_address = byte_address & ~u32(1);
	const u16 value = m_lowmem_shadow[word_address >> 1];

	const char *high_field = "unknown";
	const char *low_field = "unknown";

	switch (word_address)
	{
	case 0x04c6: high_field = "ST0"; low_field = "ST1"; break;
	case 0x04c8: high_field = "ST2"; low_field = "C"; break;
	case 0x04ca: high_field = "H"; low_field = "R"; break;
	case 0x04cc: high_field = "N"; low_field = "following_state"; break;
	}

	if (ACCESSING_BITS_8_15)
		logerror("ASR10_FDC_CMD46_RESULT_STORE txn=%u event=result_store pc=%06x addr=%06x field=%s value=%02x mem_mask=%04x\n",
			m_fdc_cmd46_transaction, pc, word_address, high_field, u8(value >> 8), mem_mask);

	if (ACCESSING_BITS_0_7)
		logerror("ASR10_FDC_CMD46_RESULT_STORE txn=%u event=result_store pc=%06x addr=%06x field=%s value=%02x mem_mask=%04x\n",
			m_fdc_cmd46_transaction, pc, word_address + 1, low_field, u8(value), mem_mask);

	if (word_address == 0x04cc && ACCESSING_BITS_8_15)
		log_fdc_cmd46_summary();
}

void asr10_boot_state::log_fdc_cmd46_summary()
{
	std::string write_bytes;
	std::string write_pcs;
	std::string result_bytes;
	std::string result_pcs;
	for (u8 index = 0; index < m_fdc_cmd46_write_count; index++)
	{
		if (index)
		{
			write_bytes += ',';
			write_pcs += ',';
		}
		write_bytes += util::string_format("%02x", m_fdc_cmd46_write_bytes[index]);
		write_pcs += util::string_format("%06x", m_fdc_cmd46_write_pcs[index]);
	}
	for (u8 index = 0; index < m_fdc_cmd46_result_count; index++)
	{
		if (index)
		{
			result_bytes += ',';
			result_pcs += ',';
		}
		result_bytes += util::string_format("%02x", m_fdc_cmd46_result_bytes[index]);
		result_pcs += util::string_format("%06x", m_fdc_cmd46_result_pcs[index]);
	}

	floppy_image_device *const floppy = m_floppy_connector->get_device();
	const floppy_image_format_t *const format = floppy ? floppy->get_load_format() : nullptr;
	const u8 command = m_fdc_cmd46_write_bytes[0];
	const u8 select = m_fdc_cmd46_write_bytes[1];
	const u8 c = m_fdc_cmd46_write_bytes[2];
	const u8 h = m_fdc_cmd46_write_bytes[3];
	const u8 r = m_fdc_cmd46_write_bytes[4];
	const u8 n = m_fdc_cmd46_write_bytes[5];
	const u8 eot = m_fdc_cmd46_write_bytes[6];
	const u8 gpl = m_fdc_cmd46_write_bytes[7];
	const u8 dtl = m_fdc_cmd46_write_bytes[8];
	const u8 st0 = m_fdc_cmd46_result_bytes[0];
	const u8 st1 = m_fdc_cmd46_result_bytes[1];
	const u8 st2 = m_fdc_cmd46_result_bytes[2];
	const u8 result_c = m_fdc_cmd46_result_bytes[3];
	const u8 result_h = m_fdc_cmd46_result_bytes[4];
	const u8 result_r = m_fdc_cmd46_result_bytes[5];
	const u8 result_n = m_fdc_cmd46_result_bytes[6];
	logerror("ASR10_FDC_CMD46 txn=%u event=summary "
		"data_rate=%u data_rate_source=%02x "
		"command_bytes=\"%s\" fifo_write_pcs=\"%s\" "
		"command_byte=%02x drive_head_byte=%02x C_byte=%02x H_byte=%02x R_byte=%02x N_byte=%02x "
		"EOT_byte=%02x GPL_byte=%02x DTL_byte=%02x "
		"decoded_drive=%u decoded_head_select=%u decoded_C=%02x decoded_H=%02x decoded_R=%02x decoded_N=%02x "
		"decoded_sector_size=%u decoded_EOT=%02x decoded_GPL=%02x decoded_DTL=%02x mfm=%u mt=%u sk=%u "
		"result_bytes=\"%s\" fifo_result_pcs=\"%s\" "
		"ST0=%02x ST1=%02x ST2=%02x result_C_byte=%02x result_H_byte=%02x result_R_byte=%02x result_N_byte=%02x "
		"ST0_invalid=%u ST0_abnormal=%u ST0_seek_end=%u ST0_equipment_check=%u ST0_not_ready=%u "
		"ST1_end_of_cylinder=%u ST1_data_error=%u ST1_overrun=%u ST1_no_data=%u ST1_not_writable=%u ST1_missing_address_mark=%u "
		"ST2_control_mark=%u ST2_data_error=%u ST2_wrong_cylinder=%u ST2_scan_equal=%u ST2_scan_not_satisfied=%u "
		"ST2_bad_cylinder=%u ST2_missing_data_address_mark=%u "
		"decoded_result_C=%02x decoded_result_H=%02x decoded_result_R=%02x decoded_result_N=%02x "
		"lowmem_ST0_04c6=%02x lowmem_ST1_04c7=%02x lowmem_ST2_04c8=%02x "
		"lowmem_C_04c9=%02x lowmem_H_04ca=%02x lowmem_R_04cb=%02x lowmem_N_04cc=%02x "
		"format=%s image_geometry=not_exposed current_cylinder=%d current_side=%u drive_sides=%d "
		"media_mounted=%u ready=%u motor=%u density=%s read_source=upd72069_device stubbed=0\n",
		m_fdc_cmd46_transaction, m_fdc_data_rate, m_fdc_data_rate_source,
		write_bytes.c_str(), write_pcs.c_str(),
		command, select, c, h, r, n, eot, gpl, dtl,
		select & 3, BIT(select, 2), c, h, r, n, n <= 7 ? 128U << n : 0,
		eot, gpl, dtl, BIT(command, 6), BIT(command, 7), BIT(command, 5),
		result_bytes.c_str(), result_pcs.c_str(),
		st0, st1, st2, result_c, result_h, result_r, result_n,
		BIT(st0, 7), BIT(st0, 6), BIT(st0, 5), BIT(st0, 4), BIT(st0, 3),
		BIT(st1, 7), BIT(st1, 5), BIT(st1, 4), BIT(st1, 2), BIT(st1, 1), BIT(st1, 0),
		BIT(st2, 6), BIT(st2, 5), BIT(st2, 4), BIT(st2, 3), BIT(st2, 2), BIT(st2, 1), BIT(st2, 0),
		result_c, result_h, result_r, result_n,
		u8(m_lowmem_shadow[0x04c6 >> 1] >> 8), u8(m_lowmem_shadow[0x04c6 >> 1]),
		u8(m_lowmem_shadow[0x04c8 >> 1] >> 8), u8(m_lowmem_shadow[0x04c8 >> 1]),
		u8(m_lowmem_shadow[0x04ca >> 1] >> 8), u8(m_lowmem_shadow[0x04ca >> 1]),
		u8(m_lowmem_shadow[0x04cc >> 1] >> 8), format ? format->name() : "none",
		floppy ? floppy->get_cyl() : -1, floppy ? floppy->ss_r() : 0,
		floppy ? floppy->get_sides() : 0, floppy && floppy->exists() ? 1 : 0,
		floppy && !floppy->ready_r() ? 1 : 0, floppy && !floppy->mon_r() ? 1 : 0,
		floppy && floppy->floppy_is_hd() ? "hd" : "dd");

	m_fdc_cmd46_active = false;
	m_fdc_cmd46_result_complete = false;
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
				(ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 ? 0x10 : 0),
			ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 ? 1 : BIT(ASR10_DUART_INPUT_CHANGE_STUB, 4),
			return_address);
	}
}


void asr10_boot_state::log_lowmem_04ee(bool write, u16 previous, u16 current, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u8 previous_byte = previous >> 8;
	const u8 current_byte = current >> 8;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 stack2 = read_stack_long(sp + 8);
	logerror("ASR10LOWMEM04EE pc=%06x rw=%c value=%04x previous=%04x current=%04x mem_mask=%04x "
		"phase=%s sp=%06x stack0=%08x stack1=%08x stack2=%08x\n",
		pc, write ? 'W' : 'R', current & mem_mask, previous, current, mem_mask,
		m_seen_insert_disk_prompt ? "post_insert_disk" : "boot", sp, stack0, stack1, stack2);

	if (!write && pc == 0x00fb7c7a)
	{
		logerror("ASR10_04EE pc=fb7c7a rw=R value=%02x negative=%u zero=%u "
			"branch_pc=fb7c80 branch=bpl branch_taken=%u branch_target=fb7c9c "
			"not_taken_path=duart_input_change_bit4\n",
			current_byte, BIT(current_byte, 7), current_byte == 0,
			BIT(current_byte, 7) ? 0 : 1);
		logerror("ASR10_INPUT_BRANCH pc=fb7c80 opcode=6a1a tested=04ee_sign value=%02x "
			"branch_taken=%u branch_target=fb7c9c\n",
			current_byte, BIT(current_byte, 7) ? 0 : 1);
	}
	else if (write && pc == 0x00fb7c98)
	{
		logerror("ASR10_04EE pc=fb7c98 rw=W previous=%02x current=%02x source=semantic_input_result "
			"branch_pc=fb7c9c branch=bne branch_taken=%u branch_target=fb7ca4\n",
			previous_byte, current_byte, current_byte ? 1 : 0);
		logerror("ASR10_INPUT_BRANCH pc=fb7c9c opcode=6606 tested=04ee_write_result value=%02x "
			"branch_taken=%u branch_target=fb7ca4 not_taken_target=fb7c9e\n",
			current_byte, current_byte ? 1 : 0);
	}
}


void asr10_boot_state::log_lowmem_049d(bool write, u16 previous, u16 current, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	log_cpu_context(pc);
	const u8 previous_byte = u8(previous);
	const u8 current_byte = u8(current);
	if (!ACCESSING_BITS_0_7)
		return;

	if (write)
	{
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		const u32 stack0 = read_stack_long(sp);
		const u32 stack1 = read_stack_long(sp + 4);
		const u32 stack2 = read_stack_long(sp + 8);
		std::string command_sequence;
		const u8 first = (m_fdc_command_ring_next + m_fdc_command_ring.size() - m_fdc_command_ring_count) % m_fdc_command_ring.size();
		for (u8 index = 0; index < m_fdc_command_ring_count; index++)
		{
			if (index)
				command_sequence += ',';
			command_sequence += util::string_format("%02x", m_fdc_command_ring[(first + index) % m_fdc_command_ring.size()]);
		}

		logerror("ASR10_049D_WRITE pc=%06x previous=%02x current=%02x mem_mask=%04x "
			"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x "
			"op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
			"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
			"sp=%06x stack0=%08x stack1=%08x stack2=%08x "
			"last_command=%02x last_fifo_read=%02x last_fifo_write=%02x txn=%u recent_commands=\"%s\" "
			"field_04a6=%04x field_04ae=%04x field_04b0=%04x field_04b4=%04x field_04b6=%04x "
			"field_04c4=%04x field_04c6=%04x field_04d6=%04x field_04e6=%04x\n",
			pc, previous_byte, current_byte, mem_mask,
			read_code_word(pc - 8), read_code_word(pc - 6), read_code_word(pc - 4),
			read_code_word(pc - 2), read_code_word(pc), read_code_word(pc + 2),
			read_code_word(pc + 4), read_code_word(pc + 6), read_code_word(pc + 8),
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
			u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
			u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
			u16(m_maincpu->state_int(M68K_SR)), sp, stack0, stack1, stack2,
			m_fdc_last_aux_command, m_fdc_last_fifo_read, m_fdc_last_fifo_write,
			m_fdc_transaction, command_sequence.c_str(),
			m_lowmem_shadow[0x04a6 >> 1], m_lowmem_shadow[0x04ae >> 1],
			m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04b4 >> 1],
			m_lowmem_shadow[0x04b6 >> 1], m_lowmem_shadow[0x04c4 >> 1],
			m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04d6 >> 1],
			m_lowmem_shadow[0x04e6 >> 1]);
		if (pc == 0x00fb81b4)
			log_fb81b4_path("write_error_0d", pc, current_byte, false, 0x00fb81b8);
	}
	else
	{
		u32 branch_pc = 0;
		switch (pc)
		{
		case 0x00fb917e: branch_pc = 0x00fb9182; break;
		case 0x00fb918e: branch_pc = 0x00fb9192; break;
		case 0x00fb91a2: branch_pc = 0x00fb91a6; break;
		}
		if (pc == 0x00fb8d80 && m_fdc_last_aux_command == 0xf3)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			if ((read_stack_long(sp) & 0x00ffffff) == 0x00fb7c78)
				log_04c6_origin("preexisting_error_gate", 0x00fb8d84,
					current_byte, current_byte != 0, 0x00fb8dcc);
		}
		if (pc == 0x00fb8c46)
			log_fb81b4_path("preexisting_error_gate", 0x00fb8c4a,
				current_byte, current_byte != 0, 0x00fb8c6c);
		if (branch_pc)
		{
			u16 tst_sr = u16(m_maincpu->state_int(M68K_SR)) & ~0x000f;
			if (!current_byte)
				tst_sr |= 0x0004;
			else if (BIT(current_byte, 7))
				tst_sr |= 0x0008;
			log_media_branch(branch_pc, tst_sr);
			m_media_branch_last_pc = branch_pc;
		}
		logerror("ASR10STATE049D pc=%06x rw=R previous=%02x current=%02x phase=%s\n",
			pc, previous_byte, current_byte, m_seen_insert_disk_prompt ? "post_insert_disk" : "boot");
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
	case 0x28: return "port_b_data_lrclk_bit3_candidate";
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
	case 0x60: return "unknown_0x60_busy_bit0_candidate";
	default: return "internal_register_unknown";
	}
}


const char *asr10_boot_state::fdc_state_field_name(u32 address)
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


bool asr10_boot_state::is_fdc_state_field(u32 address)
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
	case 0x00f88280:
	case 0x00f88284:
	case 0x00f882de:
	case 0x00f87ede:
	case 0x00f87ee4:
	case 0x00f8c14a:
	case 0x00f8c16a:
	case 0x00f8932e:
	case 0x00f89798:
	case 0x00f8f302:
	case 0x00f9268c:
	case 0x00f94314:
	{
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		logerror("ASR10_ERROR_WATCH pc=%06x previous_pc=%06x opcode=%04x "
			"d0=%08x d1=%08x d2=%08x d3=%08x sr=%04x sp=%06x "
			"stack0=%08x stack1=%08x stack2=%08x lowmem_00c0=%04x lowmem_0cda=%04x "
			"lowmem_04c6=%04x lowmem_04c8=%04x lowmem_04ca=%04x lowmem_04cc=%04x\n",
			pc, m_last_distinct_pc, read_code_word(pc),
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
			u16(m_maincpu->state_int(M68K_SR)), sp,
			read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
			m_lowmem_shadow[0x00c0 >> 1], m_lowmem_shadow[0x0cda >> 1],
			m_lowmem_shadow[0x04c6 >> 1], m_lowmem_shadow[0x04c8 >> 1],
			m_lowmem_shadow[0x04ca >> 1], m_lowmem_shadow[0x04cc >> 1]);
		break;
	}
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

// hängning
void asr10_boot_state::log_pc_summary(const char *reason, u32 pc)
{
	const u16 opcode = read_code_word(pc);
	u32 branch_target = 0xffffffffU;
	u32 branch_pc = 0xffffffffU;
	u32 accessed_address = 0xffffffffU;
	s32 tested_bit = -1;
	u16 poll_value = 0xffff;
	u16 poll_mem_mask = 0;
	u16 shadow_last_write = 0xffff;
	u8 relevant_byte = 0xff;
	s32 bit_state = -1;
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

	const u32 poll_pc = (branch_target != 0xffffffffU && branch_target <= pc) ? branch_target : pc;
	const u16 poll_opcode = read_code_word(poll_pc);
	if (poll_opcode == 0x0839)
	{
		tested_bit = read_code_word(poll_pc + 2) & 7;
		accessed_address = (u32(read_code_word(poll_pc + 4)) << 16) | read_code_word(poll_pc + 6);
	}
	else if (poll_opcode == 0x1039)
	{
		accessed_address = (u32(read_code_word(poll_pc + 2)) << 16) | read_code_word(poll_pc + 4);
	}
	else if ((poll_opcode & 0xfff8) == 0x0810)
	{
		accessed_address = m_maincpu->state_int(M68K_A0 + (poll_opcode & 7)) & 0x00ffffff;
		tested_bit = read_code_word(poll_pc + 2) & 7;
	}

	if (accessed_address != 0xffffffffU)
	{
		const trace_slot *poll_slot = nullptr;
		for (trace_slot const &slot : m_trace_slots)
		{
			if (slot.repeat_count && !slot.write && slot.address == accessed_address &&
				(!poll_slot || slot.repeat_count > poll_slot->repeat_count))
				poll_slot = &slot;
		}
		if (poll_slot)
		{
			poll_value = poll_slot->data;
			poll_mem_mask = poll_slot->mem_mask;
			shadow_last_write = poll_slot->last_write;
			relevant_byte = (poll_mem_mask & 0xff00) ? u8(poll_value >> 8) : u8(poll_value);
			if (tested_bit >= 0)
				bit_state = BIT(relevant_byte, tested_bit & 7);
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
	if (pc == 0x00f89cd4)
	{
		loop_guess = "panel_input_poll_delay";
	}
	if (!strcmp(loop_guess, "unknown") && accessed_address != 0xffffffffU)
	{
		loop_guess = "device_or_memory_poll";
	}

	logerror("ASR10HANG reason=%s pc=%06x previous_pc=%06x opcode=%04x accessed_address=%08x "
		"poll_address=%08x tested_bit=%d region_guess=%s poll_value=%04x poll_mem_mask=%04x "
		"relevant_byte=%02x bit_state=%d shadow_last_write=%04x loop_guess=%s d3=%08x "
		"branch_pc=%06x branch_kind=%s branch_target=%06x pc_repeat_count=%u poll_count=%u\n",
		reason, pc, m_last_distinct_pc, opcode, accessed_address,
		accessed_address, tested_bit, address_region_guess(accessed_address), poll_value, poll_mem_mask,
		relevant_byte, bit_state, shadow_last_write, loop_guess, u32(m_maincpu->state_int(M68K_D3)), branch_pc,
		branch_kind, branch_target, m_pc_repeat_count, u32(m_pc_poll_count));
	dump_repeated_accesses();
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::prompt_select_poll)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const bool media_branch_pc =
			pc == 0x00fb9182 || pc == 0x00fb9192 || pc == 0x00fb91a6 ||
			pc == 0x00fb91ba || pc == 0x00fb91c8 || pc == 0x00fb91d2;
	if (media_branch_pc)
	{
		if (pc != m_media_branch_last_pc)
		{
			log_media_branch(pc);
			m_media_branch_last_pc = pc;
		}
	}
	else
		m_media_branch_last_pc = 0xffffffff;
	static constexpr u32 countdown_pcs[] = {
		0x00fb91a8, 0x00fb91ac, 0x00fb91b4, 0x00fb91b6,
		0x00fb91cc, 0x00fb91ce, 0x00fb91d0, 0x00fb91d4
	};
	for (u8 index = 0; index < std::size(countdown_pcs); index++)
	{
		if (pc == countdown_pcs[index] && !(m_04b0_countdown_trace_mask & (1U << index)))
		{
			m_04b0_countdown_trace_mask |= 1U << index;
			log_04b0_countdown(pc, 'P', m_lowmem_shadow[0x04b0 >> 1], m_lowmem_shadow[0x04b0 >> 1]);
			break;
		}
	}
	if (pc == 0x00fb9490 || pc == 0x00fb9494 || pc == 0x00fb9496 ||
		pc == 0x00fb94ae || pc == 0x00fb94b2 || pc == 0x00fb94b4 ||
		pc == 0x00fb94da || pc == 0x00fb94e0)
	{
		log_prompt_select(pc);
	}

	if (m_prompt_select_trace_mask & 0x80)
		m_prompt_select_timer->adjust(attotime::never);
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


void asr10_boot_state::floppy_drives(device_slot_interface &device)
{
	device.option_add_internal("35hd", FLOPPY_35_HD);
}


void asr10_boot_state::floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
	fr.add(FLOPPY_ASR10IMG_FORMAT);
	fr.add(FLOPPY_ESQIMG_FORMAT);
	fr.add(FLOPPY_HFE_FORMAT);
}


static INPUT_PORTS_START(asr10_boot)
INPUT_PORTS_END


void asr10_boot_state::asr10_boot(machine_config &config)
{
	M68000(config, m_maincpu, XTAL(16'000'000)); // 68000-compatible stand-in for likely MC68302-family board
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);

	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder for boot tracing

	// The uPD72069 sees this child connector as drive 0 via the conventional "fdc:0" tag.
	// Mounted HFE media changes Recalibrate/Sense from 68,00 (not ready) to 20,00.
	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);
}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
