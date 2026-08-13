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
#include "asr10_boot_defs.h"
#include "main.h"

#include "cpu/m68000/m68000.h"
#include "imagedev/floppy.h"
#include "machine/mc68302.h"
#include "machine/mc68681.h"
#include "machine/upd765.h"

#include "esqpanel.h"
#include "formats/esq16_dsk.h"
#include "sound/es5506.h"
#include "cpu/es5510/es5510.h"
#include "formats/hxchfe_dsk.h"

#include "asr10_boot.lh"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

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
		, m_duart(*this, "duart")
		, m_panel(*this, "panel")
		, m_rom(*this, "maincpu")
		, m_es5506_host(*this, "es5506_host")
		, m_es5510_host(*this, "es5510_host")
	        , m_display(*this, "digit%u", 0U)
	{
	}

	void asr10_boot(machine_config &config) ATTR_COLD;

private:
	void es5506_wavetable_map(address_map &map) ATTR_COLD;
	void es5506_unpopulated_wavetable_map(address_map &map) ATTR_COLD;
	static constexpr u32 ROM_MASK = 0x0003ffff;
	static constexpr u32 LOWMEM_WORDS = 0x00100000 / 2;
	static constexpr u32 LOWMEM_LOG_END = 0x00000200;
	static constexpr u32 PROBE_OR_ALIAS_REGION_COUNT = 4;
	static constexpr u32 TRACE_SLOT_COUNT = 64;
	static constexpr u32 PANEL_TEXT_LENGTH = 64;
	static constexpr u32 MAX_PC_POLLS = 4'000'000;

	static constexpr bool ASR10_MISSING_FDC_RATE_SOURCE = true;
	static constexpr bool ASR10_DIAG_PANEL_B = true;
	static constexpr u32 ASR10_DISPLAY_LENGTH = 22;
	using trace_region = asr10_boot_defs::trace_region;
	using trace_slot = asr10_boot_defs::trace_slot;
	enum class panel_byte_role : u8
	{
		SERIAL,
		RING_CONTROL,
		DIRECT_TEXT_PREFIX,
		TEXT_PAYLOAD
	};
	static u16 ascii_to_14seg(u8 character) { return asr10_boot_defs::ascii_to_14seg(character); }

	required_device<mc68302_device> m_maincpu;
	required_device<upd72069_device> m_fdc;
	required_device<floppy_connector> m_floppy_connector;
	required_device<scn2681_device> m_duart;
	required_device<asr10panel_device> m_panel;
	required_memory_region m_rom;

	optional_device<es5506_device> m_es5506_host;
	optional_device<es5510_device> m_es5510_host;

	output_finder<ASR10_DISPLAY_LENGTH> m_display;
	std::array<u8, ASR10_DISPLAY_LENGTH> m_display_chars{};
	u8 m_display_position = 0;

	emu_timer *m_pc_timer = nullptr;
	emu_timer *m_prompt_select_timer = nullptr;
	emu_timer *m_lrclk_timer = nullptr;
	bool m_lrclk_level = false;
	memory_passthrough_handler m_hook_fc2068_tap;
	bool m_fc60b0_verified = false;
	memory_passthrough_handler m_hook_fc2d40_read_tap;
	memory_passthrough_handler m_hook_fc2d40_write_tap;
	memory_passthrough_handler m_hook_fc3000_read_tap;
	memory_passthrough_handler m_hook_fc3000_write_tap;
	memory_passthrough_handler m_hook_fc222e_read_tap;
	memory_passthrough_handler m_hook_fc222e_write_tap;
	memory_passthrough_handler m_hook_fc226e_read_tap;
	memory_passthrough_handler m_hook_fc226e_write_tap;

	// CS3 window access oracle (0xFC4000-0xFC5FFF: FDC, DUART, SCSI
	// candidate) -- same known/known_unimplemented/unknown scheme as
	// mc68302_device's SIB-window oracle (docs/asr10/PLAN.md fas 3).
	// `known` = a real device backs this address (FDC/DUART). `known_
	// unimplemented` = a documented board-level candidate register with
	// no real device behind it (the SCSI candidate). `unknown` = plain
	// unaddressed .ram() -- nothing claims this address at all.
	//
	// NOTE on indexing: install_read_tap/write_tap's `offset` callback
	// parameter is the raw CPU byte address here (verified empirically:
	// a read at $FC4809 delivered offset=0x00fc4808), not a word-shifted
	// index relative to the tap's own addrstart the way the mc68302
	// device's internal_r/w handlers work. classify_cs3_offset() and the
	// storage array are byte-indexed accordingly, 0x2000 entries for the
	// full 0xFC4000-0xFC5FFF span.
	enum class cs3_access_class : u8 { known, known_unimplemented, unknown };
	static cs3_access_class classify_cs3_offset(u16 byte_offset);
	struct cs3_access_class_counts { u32 known = 0; u32 known_unimplemented = 0; u32 unknown = 0; };
	cs3_access_class_counts cs3_distinct_offset_counts() const;
	struct cs3_offset_hit { u32 address = 0; u32 count = 0; };
	std::vector<cs3_offset_hit> cs3_top_accessed_offsets(unsigned max_entries) const;
	void cs3_access_summary();
	std::array<u32, 0x2000> m_cs3_access_count{};
	memory_passthrough_handler m_cs3_read_tap;
	memory_passthrough_handler m_cs3_write_tap;

	u32 m_fc2d40_cluster_count = 0;
	u32 m_fc3000_cluster_count = 0;
	std::unique_ptr<u16[]> m_lowmem_shadow;
	u16 m_probe_or_alias_region_shadow[PROBE_OR_ALIAS_REGION_COUNT][2]{};
	u16 m_m68302_internal_shadow[0x80]{};
	u8 m_fdc_last_aux_command = 0;
	u8 m_fdc_last_msr = 0;
	u8 m_fdc_last_fifo_read = 0;
	u8 m_fdc_last_fifo_write = 0;
	u32 m_fdc_data_rate = 250000;
	u8 m_fdc_data_rate_source = 0;
	bool m_floppy_is_loaded = false;
	bool m_floppy_is_active = false;
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
	u8 m_duart_io = 0;
	std::array<u16, 8> m_analog_values{};
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
	u32 m_fdc_cmd46_total_fifo_reads = 0;
	u32 m_fdc_cmd46_msr_exm_seen_count = 0;
	u8 m_fdc_cmd46_last_msr_before_result = 0;
	std::array<u8, 64> m_fdc_cmd46_first_data_bytes{};
	bool m_fdc_cmd46_first_data_logged = false;
	bool m_fdc_cmd46_result_complete = false;
	u8 m_prompt_select_trace_mask = 0;
	u8 m_04b0_countdown_trace_mask = 0;
	u32 m_media_branch_last_pc = 0xffffffff;
	u16 m_scsi_asr_shadow[0x10]{};
	std::array<trace_slot, TRACE_SLOT_COUNT> m_trace_slots{};
	char m_panel_text[PANEL_TEXT_LENGTH]{};
	u32 m_panel_text_length = 0;
	u8 m_panel_transport_pending_marker = 0;
	u8 m_panel_receive_role = u8(panel_byte_role::SERIAL);
	std::array<u8, 0x40> m_panel_ring_byte_roles{};
	bool m_panel_direct_text_active = false;
	// filesystem-browser-map.md 4.15: display-timeline reconstruction from
	// reset, not gated on the (too-late) f880fc landmark. Tracks the PC of
	// the byte that started the current message and the PC of the most
	// recent byte, so a completed message's producer PC range is known.
	u32 m_panel_msg_first_pc = 0xffffffffU;
	u32 m_panel_msg_last_pc = 0xffffffffU;
	bool m_seen_loading_system_prompt = false;
	u32 m_post_loading_panel_write_count = 0;
	u32 m_post_loading_fdc_access_count = 0;
	bool m_insert_disk_decision_logged = false;
	bool m_error009_origin_logged = false;
	bool m_error032_origin_logged = false;
	u32 m_lrclk_trace_count = 0;
	u32 m_fc6829_trace_count = 0;
	bool m_task1_string_scan_logged = false;
	u32 m_esp_first_pass_write_seq = 0;
	u32 m_post_lrclk_poll_count = 0;
	bool m_post_lrclk_disassembly_logged = false;
	u32 m_f87f96_queue_read_count = 0;
	u32 m_f87f96_queue_write_count = 0;
	u32 m_f87f96_queue_rte_count = 0;
	bool m_f87f96_code_dump_logged = false;
	bool m_f880_queue_code_dump_logged = false;
	bool m_f8ce_queue_code_dump_logged = false;
	bool m_queue_rte_after_pending = false;
	u32 m_queue_rte_after_count = 0;
	u32 m_queue_rte_before_pc = 0xffffffff;
	u32 m_queue_rte_last_return_pc = 0xffffffff;

	u32 m_runtime_dispatch_entry_count = 0;
	u32 m_timer_candidate_trace_count = 0;
	u32 m_iack_trace_count = 0;
	bool m_error139_d0_candidate_logged = false;
	u16 m_queue_rte_before_fc6814 = 0;
	u16 m_queue_rte_before_fc6816 = 0;
	u16 m_queue_rte_before_fc6818 = 0;
	u32 m_fc681x_trace_count = 0;
	bool m_fc681x_code_dump_logged = false;
	bool m_fc681x_00bf_code_dump_logged = false;
	bool m_fc681x_0067_code_dump_logged = false;
	bool m_fc6816_service_setter_dump_logged = false;
	bool m_fc6816_service_2400_set_by_runtime = false;
	bool m_fc6816_service_0d06_set_after_runtime = false;
	u32 m_fc6816_service_setter_rte_count = 0;
	u32 m_fc6816_service_0d06_rte_count = 0;
	u32 m_fc6816_service_setter_pc = 0xffffffff;
	u32 m_fc6816_service_0d06_pc = 0xffffffff;
	u32 m_last_fc68_pc = 0xffffffff;
	u32 m_last_fc68_address = 0xffffffff;
	u16 m_last_fc68_data = 0;
	u16 m_last_fc68_mem_mask = 0;
	u16 m_last_fc68_shadow = 0;
	bool m_last_fc68_write = false;
	u32 m_recent_queue_pc = 0xffffffff;
	u32 m_recent_queue_address = 0xffffffff;
	u32 m_recent_queue_record_base = 0xffffffff;
	u32 m_recent_queue_slot = 0xffffffff;
	u16 m_recent_queue_previous = 0;
	u16 m_recent_queue_current = 0;
	u16 m_recent_queue_data = 0;
	u16 m_recent_queue_mem_mask = 0;
	bool m_recent_queue_write = false;
	bool m_recent_queue_handler_clear = false;
	bool m_seen_insert_disk_prompt = false;
	u64 m_pc_poll_count = 0;
	u64 m_panel_b_seq = 0;
	u64 m_panel_b_last_rhrb_seq = 0;
	u32 m_panel_b_last_ring_write_address = 0xffffffffU;
	u32 m_panel_b_last_ring_write_pc = 0xffffffffU;
	u32 m_panel_b_last_parser_pc = 0xffffffffU;
	u8 m_panel_b_last_ring_write_byte = 0;
	bool m_panel_b_last_ring_write_valid = false;
	bool m_panel_receive_live_active = false;
	u32 m_panel_receive_live_srb_reads = 0;
	u32 m_panel_receive_live_rhrb_reads = 0;
	u32 m_panel_receive_live_queue_calls = 0;
	u32 m_panel_receive_live_fifo_overrun_pushes = 0;
	attotime m_panel_receive_live_last_access_time = attotime::never;
	bool m_panel_receive_live_window_dumped = false;
	u32 m_gen_counter = 0;
	std::array<u8, 128> m_gen_thrb_bytes{};
	u8 m_gen_thrb_count = 0;
	bool m_node_89a2_logged = false;
	bool m_slot0_0202_logged = false;
	bool m_fdc_os_cmd_active = false;
	u8 m_fdc_os_cmd_opcode = 0;
	u8 m_fdc_os_cmd_expected_len = 0;
	u8 m_fdc_os_cmd_len = 0;
	std::array<u8, 9> m_fdc_os_cmd_bytes{};
	u32 m_fdc_os_cmd_pc = 0;
	bool m_divzero_frame_logged = false;
	std::array<u8, 64> m_es5506_host_seen_mask{}; // bit0=read seen, bit1=write seen, per device offset
	u32 m_es5506_host_access_count = 0;
	u32 m_esp_select_commit_log_count = 0;
	// filesystem-browser-map.md 4.25 (observation-only round): identifying
	// the retry-exhaustion object seen at a3=~0x010722, distinct from the
	// already-fixed fff9bca0 table.
	u32 m_esp_f973f0_entry_log_count = 0;
	bool m_esp_other_table_first_retry_captured = false;
	bool m_esp_other_table_verify_captured = false;
	bool m_primary_slot_snapshot_logged = false;
	u32 m_last_pc = 0xffffffffU;
	u32 m_last_distinct_pc = 0xffffffffU;
	u32 m_pc_repeat_count = 0;
	u32 m_pc_change_count = 0;
	u32 m_dispatcher_hits = 0;
	u32 m_context_hits[20]{};

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void mem_map(address_map &map) ATTR_COLD;
	void cpu_space_map(address_map &map) ATTR_COLD;

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
	u16 upd72069_fdc_r(offs_t offset, u16 mem_mask = ~0);
	void upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 scsi_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void scsi_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(pc_poll);
	TIMER_CALLBACK_MEMBER(prompt_select_poll);
	TIMER_CALLBACK_MEMBER(lrclk_toggle);
	u8 maincpu_iack_r(u8 level);

	bool probe_or_alias_region_index(u32 address, u32 &index, u32 &word_index) const;
	u16 probe_or_alias_region_r_at(u32 base, offs_t offset, u16 mem_mask);
	void probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask);
	void trace_access(trace_region region, bool write, u32 address, u16 data, u16 mem_mask, u16 last_write);

	void panel_receive_byte(u8 data);
	void flush_panel_text();
	std::string current_display_text() const;
	void note_panel_ring_store(u32 pc, u32 ring_address, u8 byte);
	panel_byte_role consume_panel_ring_role(u32 pc);
	bool is_bounded_panel_ring_control_candidate(u32 pc, u32 return_pc, u32 previous_pc) const;
	void maincpu_instruction_hook(u32 pc);
	void panel_receive_live_summary();
	void mc68302_access_summary();


























	void scan_for_ascii_string(const char *tag, u32 start, u32 end, const char *needle);
	u16 read_highview_word(u32 address) const;
































































	std::string dump_cpu_registers() const;
	u16 analog_r();
	void analog_w(offs_t offset, u16 data);
	void duart_output(u8 data);
	// ES5510 host select/commit: FC3101/FC3141/FC3181 are each a
	// single-word map range, so the `offset` MAME's address_map passes to
	// an .rw() handler installed there is always 0 (relative to that
	// range's own base) -- it is NOT the absolute ES5510 host offset
	// (0x80/0xa0/0xc0/0xe0). These four thin wrappers supply the fixed
	// absolute offset explicitly; they do not forward the map-relative
	// offset at all. u8 read/write (no mem_mask parameter) deliberately
	// matches es5510_device::host_r/host_w's own narrow-handler signature
	// -- a u16-returning handler here made MAME treat this as a
	// bus-width-matching (not narrower) handler, which .umask16() over a
	// single-word range then rejected at machine-config time ("incorrect
	// granularity for 0-bit chip selection").
	u8 es5510_host_read_select_r(offs_t offset);
	void es5510_host_read_select_w(offs_t offset, u8 data);
	u8 es5510_host_write_select_gpr_r(offs_t offset);
	void es5510_host_write_select_gpr_w(offs_t offset, u8 data);
	u8 es5510_host_write_select_instr_r(offs_t offset);
	void es5510_host_write_select_instr_w(offs_t offset, u8 data);
	// filesystem-browser-map.md 4.27/4.28: host offset 0xe0 is stock
	// es5510_device's "Write select - GPR + INSTR" (its own host_w case
	// 0xe0 comment) -- commits BOTH gpr_latch (via write_reg) AND
	// instr_latch (if data<0xa0) to the same index in one write, distinct
	// from 0xa0 (GPR only) and 0xc0 (INSTR only). Firmware record type 1
	// uses this combined path at FC31C1 -- proven from f97450's static
	// disassembly: D4 stays at its default 0x1c0 for type 1 (only type 2
	// overrides to 0x180/0xc0; only types 3/4 override to 0x140/0xa0).
	u8 es5510_host_write_select_gpr_instr_r(offs_t offset);
	void es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data);
	static const char *es5506_register_name(u32 cpu_displacement);
	u8 lowmem_byte(u32 address) const;
	u16 lowmem_word(u32 address) const;
	u32 lowmem_long(u32 address) const;
	u32 panel_ready_slot_count() const;






	void update_floppy_inputs();
	void floppy_loaded(bool loaded);
	void floppy_load(floppy_image_device *floppy);
	void floppy_unload(floppy_image_device *floppy);
	u32 read_stack_long(u32 address);
	u16 read_program_word(u32 address);
	static void floppy_drives(device_slot_interface &device);
	static void floppy_formats(format_registration &fr);
	static bool likely_rom_address(u32 address);
	static const char *address_region_guess(u32 address) { return asr10_boot_defs::address_region_guess(address); }
	static const char *trace_detail(trace_region region, u32 address);
	static const char *region_name(trace_region region) { return asr10_boot_defs::region_name(region); }
	static const char *m68302_register_name(u32 address) { return asr10_boot_defs::m68302_register_name(address); }
	static const char *fdc_state_field_name(u32 address) { return asr10_boot_defs::fdc_state_field_name(address); }
	static bool is_fdc_state_field(u32 address) { return asr10_boot_defs::is_fdc_state_field(address); }
	u16 read_code_word(u32 address) const;
	u16 read_loaded_word(u32 address) const;
	u32 read_loaded_long(u32 address) const;

	void clear_display();
	void set_display_text(const char *text);
};


void asr10_boot_state::machine_start()
{
	if (floppy_image_device *const floppy = m_floppy_connector->get_device())
	{
		floppy->setup_load_cb(floppy_image_device::load_cb(&asr10_boot_state::floppy_load, this));
		floppy->setup_unload_cb(floppy_image_device::unload_cb(&asr10_boot_state::floppy_unload, this));
		m_floppy_is_loaded = floppy->exists();
		m_floppy_is_active = !floppy->mon_r();
		update_floppy_inputs();
	}

	m_pc_timer = timer_alloc(FUNC(asr10_boot_state::pc_poll), this);
	m_prompt_select_timer = timer_alloc(FUNC(asr10_boot_state::prompt_select_poll), this);
	m_lrclk_timer = timer_alloc(FUNC(asr10_boot_state::lrclk_toggle), this);
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::panel_receive_live_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::mc68302_access_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::cs3_access_summary, this));
	// Removed (4.26): four single-address opcode-fetch taps formerly here
	// (f8834a/f88352/006800/00680a) never fired in any live capture across
	// this whole investigation -- opcode fetch on this core goes through a
	// cache-typed fast path that bypasses passthrough-tap dispatch
	// entirely; only genuine DATA accesses (reads/writes through the
	// dispatch-backed accessor) reach an install_read_tap/install_write_tap
	// callback. FC2068-FC206F below are genuine DATA reads performed by the
	// movep.l instruction at FC60B0, which uses the dispatch-backed
	// accessor, and remain reliable. FC2068/6A/6C/6E fall in the plain
	// .ram() block (0xfc0000-0xfc3fff); no existing handler/diagnostic
	// covers them, so this is the narrowest possible addition for that
	// purpose.
	m_hook_fc2068_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc2068, 0x00fc206f, "hook_fc2068_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			const u32 address = 0x00fc2068 + offset * 2;
			if (!m_fc60b0_verified)
			{
				m_fc60b0_verified = true;
				const u16 w0 = read_program_word(0x00fc60b0);
				const u16 w1 = read_program_word(0x00fc60b2);
				const u16 w2 = read_program_word(0x00fc60b4);
				logerror("ASR10_FC2001_TRACE event=fc60b0_verify runtime_word0=%04x runtime_word1=%04x "
					"runtime_word2=%04x expected_word0=0548 expected_word1=0068 expected_word2=4e75 "
					"match=%u\n",
					w0, w1, w2, (w0 == 0x0548 && w1 == 0x0068 && w2 == 0x4e75) ? 1 : 0);

				// One-shot: dump the live DPRAM chunk (FC6000-FC67FF, 2KB) so
				// sibling movep-style access thunks can be located offline by
				// pattern search, instead of adding more runtime hooks.
				std::string dpram_hex;
				for (u32 dpram_addr = 0x00fc6000; dpram_addr < 0x00fc6800; dpram_addr += 2)
				{
					const u16 w = read_program_word(dpram_addr);
					dpram_hex += util::string_format("%02x%02x", u8(w >> 8), u8(w));
				}
				logerror("ASR10_FC2001_TRACE event=dpram_dump start=fc6000 len=2048 hex=%s\n",
					dpram_hex.c_str());
			}
			logerror("ASR10_FC2001_TRACE event=fc2xxx_read pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x accessing_bits_0_7=%u accessing_bits_8_15=%u\n",
				pc, address, offset, mem_mask, data,
				ACCESSING_BITS_0_7 ? 1 : 0, ACCESSING_BITS_8_15 ? 1 : 0);
		});
	// Narrow, log-only, bounded (first 32 accesses/cluster) taps to correlate
	// the three static reference clusters (FC2001-relative, FC2D40-FC2D7F,
	// FC3001-FC31xx) plus the FC222E/FC226E block-copy addresses at runtime.
	// No data is modified; all four ranges are plain .ram() with no existing
	// custom handler/diagnostic.
	m_hook_fc2d40_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc2d40, 0x00fc2d7f, "hook_fc2d40_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			if (m_fc2d40_cluster_count >= 32)
				return;
			m_fc2d40_cluster_count++;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc2d40_read pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, 0x00fc2d40 + offset, offset, mem_mask, data, m_fc2d40_cluster_count);
		});
	m_hook_fc2d40_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc2d40, 0x00fc2d7f, "hook_fc2d40_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			if (m_fc2d40_cluster_count >= 32)
				return;
			m_fc2d40_cluster_count++;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc2d40_write pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, 0x00fc2d40 + offset, offset, mem_mask, data, m_fc2d40_cluster_count);
		});
	m_hook_fc3000_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc3000, 0x00fc31ff, "hook_fc3000_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			m_fc3000_cluster_count++;
			if (m_fc3000_cluster_count > 64 && (m_fc3000_cluster_count & (m_fc3000_cluster_count - 1)))
				return;
			logerror("ASR10_CLUSTER_TRACE event=fc3000_read pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, offset, offset, mem_mask, data, m_fc3000_cluster_count);
		});
	m_hook_fc3000_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc3000, 0x00fc31ff, "hook_fc3000_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			m_fc3000_cluster_count++;
			// TASK4 correction: this tap previously hard-capped at 32
			// events and silently dropped everything after, which is
			// exactly why later (download-time) FC3000-range writes were
			// never observed in prior sessions' captures -- not because
			// they don't happen. Rate-limit to power-of-2 counts instead
			// of a hard stop, matching this file's other high-frequency
			// taps, so long runs stay legible without losing later activity.
			if (m_fc3000_cluster_count > 64 && (m_fc3000_cluster_count & (m_fc3000_cluster_count - 1)))
				return;
			logerror("ASR10_CLUSTER_TRACE event=fc3000_write pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, offset, offset, mem_mask, data, m_fc3000_cluster_count);
		});
	// Removed (4.26): standalone opcode-fetch taps formerly installed here
	// at f973f0/f97580/f97574 never fired in any live capture, for the
	// same reason the four pre-existing call-chain taps never fired (see
	// the note near FC2068 above) -- opcode fetch bypasses passthrough-tap
	// dispatch entirely on this core. f973f0/f97580/f97574 detection is
	// done via the reliable DATA-access paths instead: see the lowmem_w
	// 0x0e7e/0x0e8c cases and the fc3000_read_tap's pc==0xf97574 check.
	m_hook_fc222e_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc222e, 0x00fc222f, "hook_fc222e_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc222e_read pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc222e_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc222e, 0x00fc222f, "hook_fc222e_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc222e_write pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc226e_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc226e, 0x00fc226f, "hook_fc226e_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc226e_read pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc226e_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc226e, 0x00fc226f, "hook_fc226e_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc226e_write pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	// CS3 window access oracle (PLAN.md fas 3): pure observation, no data
	// modified, covers the whole 0xFC4000-0xFC5FFF window regardless of
	// which sub-range (FDC/DUART/SCSI-candidate/plain RAM) answers.
	m_cs3_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc4000, 0x00fc5fff, "cs3_access_oracle_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			m_cs3_access_count[offset & 0x1fff]++;
		});
	m_cs3_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc4000, 0x00fc5fff, "cs3_access_oracle_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			m_cs3_access_count[offset & 0x1fff]++;
		});
	// Phase 1 host-port fingerprint observation:
	// observation-only taps layered over the real es5506_device::read/write
	// mapping installed in mem_map(). These are genuine DATA accesses (via
	// MOVEP), not opcode fetches, so (unlike the four call-chain taps
	// above) the dispatch-backed tap mechanism does fire here -- already
	// proven earlier this session for other FC-range data taps.
	if (m_es5506_host.found())
	{
		logerror("ASR10_ES5506_HOST event=device_instantiated clock=16000000 note=provisional_uncalibrated\n");
		m_maincpu->space(AS_PROGRAM).install_read_tap(
			0x00fc2000, 0x00fc207f, "hook_es5506_host_read_tap",
			[this] (offs_t offset, u16 &data, u16 mem_mask)
			{
				if (machine().side_effects_disabled())
					return;
				if (!ACCESSING_BITS_0_7)
					return; // even lane is unmapped by design (odd-lane-only adapter)
				const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
				const u32 address = offset + 1; // offset is the absolute even (word)
					// address per this session's confirmed tap semantics; the
					// odd/low byte (the only one wired) is offset+1.
				const u32 disp = address - 0x00fc2001;
				const u32 device_offset = disp / 2;
				const u32 seen_index = device_offset & 0x3f;
				const bool first_seen = !(m_es5506_host_seen_mask[seen_index] & 1);
				m_es5506_host_seen_mask[seen_index] |= 1;
				m_es5506_host_access_count++;
				// TASK1 investigative addition: the FC60xx thunks are bare
				// `movep+rts`, so at the MOVEP itself SP still holds the
				// caller's return address -- reveals which routine called
				// into this thunk, since `pc` here is always the thunk
				// address, not the caller.
				const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
				std::string stack_words;
				for (u32 w = 0; w < 12; ++w)
				{
					if (w) stack_words += ',';
					const u32 wa = sp + w * 2;
					stack_words += util::string_format("%06x:%04x", wa, read_program_word(wa));
				}
				logerror("ASR10_ES5506_HOST event=host_read pc=%06x sp=%06x a0=%06x a1=%06x a2=%06x d2=%08x "
					"stack=\"%s\" "
					"address=%06x adapter_disp=%02x "
					"logical_offset=%02x case_index=%u register=%s data=%02x mem_mask=%04x "
					"first_seen=%u access_count=%u\n",
					pc, sp, u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
					u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_D2)),
					stack_words, address, disp, device_offset, device_offset / 4,
					es5506_register_name(disp), u8(data), mem_mask,
					first_seen ? 1 : 0, m_es5506_host_access_count);
			});
		m_maincpu->space(AS_PROGRAM).install_write_tap(
			0x00fc2000, 0x00fc207f, "hook_es5506_host_write_tap",
			[this] (offs_t offset, u16 &data, u16 mem_mask)
			{
				if (machine().side_effects_disabled())
					return;
				if (!ACCESSING_BITS_0_7)
					return;
				const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
				const u32 address = offset + 1; // offset is the absolute even (word)
					// address per this session's confirmed tap semantics; the
					// odd/low byte (the only one wired) is offset+1.
				const u32 disp = address - 0x00fc2001;
				const u32 device_offset = disp / 2;
				const u32 seen_index = device_offset & 0x3f;
				const bool first_seen = !(m_es5506_host_seen_mask[seen_index] & 2);
				m_es5506_host_seen_mask[seen_index] |= 2;
				m_es5506_host_access_count++;
				const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
				const u32 caller_pc = read_stack_long(sp);
				logerror("ASR10_ES5506_HOST event=host_write pc=%06x caller_pc=%06x address=%06x adapter_disp=%02x "
					"logical_offset=%02x case_index=%u register=%s data=%02x mem_mask=%04x "
					"first_seen=%u access_count=%u\n",
					pc, caller_pc, address, disp, device_offset, device_offset / 4,
					es5506_register_name(disp), u8(data), mem_mask,
					first_seen ? 1 : 0, m_es5506_host_access_count);
			});
	}
	m_lowmem_shadow = make_unique_clear<u16[]>(LOWMEM_WORDS);

	// output_finder in this MAME tree derives from device_resolver_base and
	// resolves itself automatically (like required_device); there is no
	// resolve() member to call here.
	save_item(NAME(m_display_chars));
	save_item(NAME(m_display_position));

	save_pointer(NAME(m_lowmem_shadow), LOWMEM_WORDS);
	save_item(NAME(m_probe_or_alias_region_shadow));
	save_item(NAME(m_m68302_internal_shadow));
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
	save_item(NAME(m_fdc_cmd46_total_fifo_reads));
	save_item(NAME(m_fdc_cmd46_msr_exm_seen_count));
	save_item(NAME(m_fdc_cmd46_last_msr_before_result));
	save_item(NAME(m_fdc_cmd46_first_data_bytes));
	save_item(NAME(m_fdc_cmd46_first_data_logged));
	save_item(NAME(m_fdc_cmd46_result_complete));
	save_item(NAME(m_prompt_select_trace_mask));
	save_item(NAME(m_04b0_countdown_trace_mask));
	save_item(NAME(m_media_branch_last_pc));
	save_item(NAME(m_scsi_asr_shadow));
	save_item(NAME(m_panel_text));
	save_item(NAME(m_panel_text_length));
	save_item(NAME(m_panel_transport_pending_marker));
	save_item(NAME(m_panel_receive_role));
	save_item(NAME(m_panel_ring_byte_roles));
	save_item(NAME(m_panel_direct_text_active));
	save_item(NAME(m_panel_msg_first_pc));
	save_item(NAME(m_panel_msg_last_pc));
	save_item(NAME(m_seen_loading_system_prompt));
	save_item(NAME(m_post_loading_panel_write_count));
	save_item(NAME(m_post_loading_fdc_access_count));
	save_item(NAME(m_insert_disk_decision_logged));
	save_item(NAME(m_error009_origin_logged));
	save_item(NAME(m_error032_origin_logged));
	save_item(NAME(m_lrclk_trace_count));
	save_item(NAME(m_fc6829_trace_count));
	save_item(NAME(m_task1_string_scan_logged));
	save_item(NAME(m_post_lrclk_poll_count));
	save_item(NAME(m_post_lrclk_disassembly_logged));
	save_item(NAME(m_f87f96_queue_read_count));
	save_item(NAME(m_f87f96_queue_write_count));
	save_item(NAME(m_f87f96_queue_rte_count));
	save_item(NAME(m_f87f96_code_dump_logged));
	save_item(NAME(m_f880_queue_code_dump_logged));
	save_item(NAME(m_f8ce_queue_code_dump_logged));
	save_item(NAME(m_queue_rte_after_pending));
	save_item(NAME(m_queue_rte_after_count));
	save_item(NAME(m_queue_rte_before_pc));
	save_item(NAME(m_queue_rte_last_return_pc));
	save_item(NAME(m_runtime_dispatch_entry_count));
	save_item(NAME(m_timer_candidate_trace_count));
	save_item(NAME(m_iack_trace_count));
	save_item(NAME(m_error139_d0_candidate_logged));
	save_item(NAME(m_queue_rte_before_fc6814));
	save_item(NAME(m_queue_rte_before_fc6816));
	save_item(NAME(m_queue_rte_before_fc6818));
	save_item(NAME(m_fc681x_trace_count));
	save_item(NAME(m_fc681x_code_dump_logged));
	save_item(NAME(m_fc681x_00bf_code_dump_logged));
	save_item(NAME(m_fc681x_0067_code_dump_logged));
	save_item(NAME(m_fc6816_service_setter_dump_logged));
	save_item(NAME(m_fc6816_service_2400_set_by_runtime));
	save_item(NAME(m_fc6816_service_0d06_set_after_runtime));
	save_item(NAME(m_fc6816_service_setter_rte_count));
	save_item(NAME(m_fc6816_service_0d06_rte_count));
	save_item(NAME(m_fc6816_service_setter_pc));
	save_item(NAME(m_fc6816_service_0d06_pc));
	save_item(NAME(m_last_fc68_pc));
	save_item(NAME(m_last_fc68_address));
	save_item(NAME(m_last_fc68_data));
	save_item(NAME(m_last_fc68_mem_mask));
	save_item(NAME(m_last_fc68_shadow));
	save_item(NAME(m_last_fc68_write));
	save_item(NAME(m_recent_queue_pc));
	save_item(NAME(m_recent_queue_address));
	save_item(NAME(m_recent_queue_record_base));
	save_item(NAME(m_recent_queue_slot));
	save_item(NAME(m_recent_queue_previous));
	save_item(NAME(m_recent_queue_current));
	save_item(NAME(m_recent_queue_data));
	save_item(NAME(m_recent_queue_mem_mask));
	save_item(NAME(m_recent_queue_write));
	save_item(NAME(m_recent_queue_handler_clear));
	save_item(NAME(m_seen_insert_disk_prompt));
	save_item(NAME(m_pc_poll_count));
	save_item(NAME(m_panel_b_seq));
	save_item(NAME(m_panel_b_last_rhrb_seq));
	save_item(NAME(m_panel_b_last_ring_write_address));
	save_item(NAME(m_panel_b_last_ring_write_pc));
	save_item(NAME(m_panel_b_last_parser_pc));
	save_item(NAME(m_panel_b_last_ring_write_byte));
	save_item(NAME(m_panel_b_last_ring_write_valid));
	save_item(NAME(m_lrclk_level));
	save_item(NAME(m_gen_counter));
	save_item(NAME(m_gen_thrb_bytes));
	save_item(NAME(m_gen_thrb_count));
	save_item(NAME(m_node_89a2_logged));
	save_item(NAME(m_slot0_0202_logged));
	save_item(NAME(m_fdc_os_cmd_active));
	save_item(NAME(m_fdc_os_cmd_opcode));
	save_item(NAME(m_fdc_os_cmd_expected_len));
	save_item(NAME(m_fdc_os_cmd_len));
	save_item(NAME(m_fdc_os_cmd_bytes));
	save_item(NAME(m_fdc_os_cmd_pc));
	save_item(NAME(m_divzero_frame_logged));
	save_item(NAME(m_es5506_host_seen_mask));
	save_item(NAME(m_es5506_host_access_count));
	save_item(NAME(m_esp_select_commit_log_count));
	save_item(NAME(m_primary_slot_snapshot_logged));
	save_item(NAME(m_fc60b0_verified));
	save_item(NAME(m_fc2d40_cluster_count));
	save_item(NAME(m_fc3000_cluster_count));
	save_item(NAME(m_last_pc));
	save_item(NAME(m_last_distinct_pc));
	save_item(NAME(m_pc_repeat_count));
	save_item(NAME(m_pc_change_count));
	save_item(NAME(m_dispatcher_hits));
	save_item(NAME(m_context_hits));
}


void asr10_boot_state::update_floppy_inputs()
{
	// [DISPROVEN] as a complete ASR-10 Disk Ready model: V3.50 does not
	// boot to FILE 1 with IP0 driven solely by floppy loaded && motor active;
	// it remains in the same PLEASE INSERT DISK / $FB8D6C loop seen with no
	// useful IP0 transition. Kept visible so the failed model is not mistaken
	// for a verified hardware source.
	(void)0;
}


void asr10_boot_state::floppy_loaded(bool loaded)
{
	m_floppy_is_loaded = loaded;
	if (floppy_image_device *const floppy = m_floppy_connector->get_device())
		m_floppy_is_active = !floppy->mon_r();
	update_floppy_inputs();
}


void asr10_boot_state::floppy_load(floppy_image_device *floppy)
{
	floppy_loaded(true);
}


void asr10_boot_state::floppy_unload(floppy_image_device *floppy)
{
	floppy_loaded(false);
}


void asr10_boot_state::machine_reset()
{
	if (floppy_image_device *const floppy = m_floppy_connector->get_device())
	{
		m_floppy_is_loaded = floppy->exists();
		m_floppy_is_active = !floppy->mon_r();
		update_floppy_inputs();
	}

	m_duart_io = 0;
	m_analog_values[0] = 0x8000; // neutral/unassigned
	m_analog_values[1] = 0x8000; // neutral/unassigned
	m_analog_values[2] = 0x8000; // neutral/unassigned
	m_analog_values[3] = 0x8000; // Data Entry, centered
	m_analog_values[4] = 0x8000; // Input Level, centered
	m_analog_values[5] = 0xffc0; // Volume, full
	m_analog_values[6] = 0x8000; // neutral/unassigned
	m_analog_values[7] = 0x8000; // neutral/unassigned

	m_seen_insert_disk_prompt = false;

	m_panel_text_length = 0;
	m_panel_transport_pending_marker = 0;
	m_panel_receive_role = u8(panel_byte_role::SERIAL);
	m_panel_direct_text_active = false;
	m_display_chars.fill(' ');
	m_display_position = 0;
	m_panel_ring_byte_roles.fill(u8(panel_byte_role::SERIAL));

	set_display_text("----------------------");

	for (u32 index = 0; index < ASR10_DISPLAY_LENGTH; index++)
		m_display[index] = 0xffff;
	clear_display();
	set_display_text("----------------------");

	m_seen_loading_system_prompt = false;
	m_post_loading_panel_write_count = 0;
	m_post_loading_fdc_access_count = 0;
	m_insert_disk_decision_logged = false;
	m_error009_origin_logged = false;
	m_error032_origin_logged = false;
	m_lrclk_trace_count = 0;
	m_fc6829_trace_count = 0;
	m_task1_string_scan_logged = false;
	m_esp_first_pass_write_seq = 0;
	m_esp_select_commit_log_count = 0;
	m_esp_f973f0_entry_log_count = 0;
	m_esp_other_table_first_retry_captured = false;
	m_esp_other_table_verify_captured = false;
	m_post_lrclk_poll_count = 0;
	m_post_lrclk_disassembly_logged = false;
	m_f87f96_queue_read_count = 0;
	m_f87f96_queue_write_count = 0;
	m_f87f96_queue_rte_count = 0;
	m_f87f96_code_dump_logged = false;
	m_f880_queue_code_dump_logged = false;
	m_f8ce_queue_code_dump_logged = false;
	m_queue_rte_after_pending = false;
	m_queue_rte_after_count = 0;
	m_queue_rte_before_pc = 0xffffffff;
	m_queue_rte_last_return_pc = 0xffffffff;
	m_runtime_dispatch_entry_count = 0;
	m_timer_candidate_trace_count = 0;
	m_iack_trace_count = 0;
	m_error139_d0_candidate_logged = false;
	m_queue_rte_before_fc6814 = 0;
	m_queue_rte_before_fc6816 = 0;
	m_queue_rte_before_fc6818 = 0;
	m_fc681x_trace_count = 0;
	m_fc681x_code_dump_logged = false;
	m_fc681x_00bf_code_dump_logged = false;
	m_fc681x_0067_code_dump_logged = false;
	m_fc6816_service_setter_dump_logged = false;
	m_fc6816_service_2400_set_by_runtime = false;
	m_fc6816_service_0d06_set_after_runtime = false;
	m_fc6816_service_setter_rte_count = 0;
	m_fc6816_service_0d06_rte_count = 0;
	m_fc6816_service_setter_pc = 0xffffffff;
	m_fc6816_service_0d06_pc = 0xffffffff;
	m_last_fc68_pc = 0xffffffff;
	m_last_fc68_address = 0xffffffff;
	m_last_fc68_data = 0;
	m_last_fc68_mem_mask = 0;
	m_last_fc68_shadow = 0;
	m_last_fc68_write = false;
	m_recent_queue_pc = 0xffffffff;
	m_recent_queue_address = 0xffffffff;
	m_recent_queue_record_base = 0xffffffff;
	m_recent_queue_slot = 0xffffffff;
	m_recent_queue_previous = 0;
	m_recent_queue_current = 0;
	m_recent_queue_data = 0;
	m_recent_queue_mem_mask = 0;
	m_recent_queue_write = false;
	m_recent_queue_handler_clear = false;
	std::fill(std::begin(m_panel_text), std::end(m_panel_text), 0);
	m_pc_poll_count = 0;
	m_panel_b_seq = 0;
	m_panel_b_last_rhrb_seq = 0;
	m_panel_b_last_ring_write_address = 0xffffffffU;
	m_panel_b_last_ring_write_pc = 0xffffffffU;
	m_panel_b_last_parser_pc = 0xffffffffU;
	m_panel_b_last_ring_write_byte = 0;
	m_panel_b_last_ring_write_valid = false;
	m_panel_receive_live_active = false;
	m_panel_receive_live_srb_reads = 0;
	m_panel_receive_live_rhrb_reads = 0;
	m_panel_receive_live_queue_calls = 0;
	m_panel_receive_live_fifo_overrun_pushes = 0;
	m_panel_receive_live_last_access_time = attotime::never;
	m_panel_receive_live_window_dumped = false;
	// Board-level LRCLK into PB3 (GPIO input, docs/mc68302/pin-function-map.md):
	// external to the 68302, always running once the machine is up, not a
	// register-driven behavior. Rate is [Hypothesis]: PLAN.md section 0's
	// Y3 = 33.8688 MHz crystal is annotated "audio clock, 768 x 44.1 kHz",
	// so a straight /768 divider would put the audio word clock at
	// 44.1 kHz exactly -- not separately measured in this tree.
	m_lrclk_level = false;
	m_lrclk_timer->adjust(attotime::from_hz(44100), 0, attotime::from_hz(44100));
	m_gen_counter = 0;
	m_gen_thrb_bytes.fill(0);
	m_gen_thrb_count = 0;
	m_node_89a2_logged = false;
	m_slot0_0202_logged = false;
	m_fdc_os_cmd_active = false;
	m_fdc_os_cmd_opcode = 0;
	m_fdc_os_cmd_expected_len = 0;
	m_fdc_os_cmd_len = 0;
	m_fdc_os_cmd_bytes.fill(0);
	m_fdc_os_cmd_pc = 0;
	m_divzero_frame_logged = false;
	m_es5506_host_seen_mask.fill(0);
	m_es5506_host_access_count = 0;
	m_primary_slot_snapshot_logged = false;
	m_fc60b0_verified = false;
	m_fc2d40_cluster_count = 0;
	m_fc3000_cluster_count = 0;
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
	m_fdc_cmd46_total_fifo_reads = 0;
	m_fdc_cmd46_msr_exm_seen_count = 0;
	m_fdc_cmd46_last_msr_before_result = 0;
	m_fdc_cmd46_first_data_bytes.fill(0);
	m_fdc_cmd46_first_data_logged = false;
	m_fdc_cmd46_result_complete = false;
	m_prompt_select_trace_mask = 0;
	m_04b0_countdown_trace_mask = 0;
	m_media_branch_last_pc = 0xffffffff;
	std::fill(std::begin(m_scsi_asr_shadow), std::end(m_scsi_asr_shadow), 0);
	m_trace_slots = {};

	// Coarse landmark polling only. This does not replace instruction tracing.
	m_pc_timer->adjust(attotime::zero, 0, attotime::from_ticks(64, m_maincpu->clock()));

	logerror("ASR10BOOT reset: expected SP=$00000300 PC=$0000000c from ROM vectors\n");
	logerror("ASR10_MAINCPU_INPUT_LINE_DRIVER source=harness default_set_input_line_calls=0 "
		"iack_map=installed_returns_autovectors_by_default pc_timer=diagnostic_poll "
		"prompt_select_timer=diagnostic_poll fc6850_fc6852_timer_binding=none\n");
	(void)0;
}

void asr10_boot_state::clear_display()
{
	m_display_chars.fill(' ');
	m_display_position = 0;

	for (u32 index = 0; index < ASR10_DISPLAY_LENGTH; index++)
		m_display[index] = 0;
}

void asr10_boot_state::set_display_text(const char *text)
{
	clear_display();

	for (u32 index = 0;
		index < ASR10_DISPLAY_LENGTH && text[index];
		index++)
	{
		const u8 character = u8(text[index]);

		m_display_chars[index] = character;
		m_display[index] = ascii_to_14seg(character);
		m_display_position = index + 1;
	}
}


std::string asr10_boot_state::current_display_text() const
{
	std::string text;
	text.reserve(ASR10_DISPLAY_LENGTH);
	for (u8 character : m_display_chars)
		text.push_back(char(character ? character : ' '));
	while (!text.empty() && text.back() == ' ')
		text.pop_back();
	return text;
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

	map(0xf00000, 0xf7ffff).ram();
	map(0xf80000, 0xfbffff).rw(FUNC(asr10_boot_state::high_alias_r), FUNC(asr10_boot_state::high_alias_w));
	{
		// Phase 1 host-port fingerprint mapping: NOT board-proven -- see
		// docs/asr10/es5506-chain-verification.md. Narrow adapter
		// owns only FC2000-FC207F; FC2080+ (and FC2Dxx/FC30xx elsewhere)
		// are untouched .ram(), matching the exact 0x80-byte,
		// .umask16(0x00ff) convention already proven in esqkt.cpp/
		// macrossp.cpp/ssv.cpp for this same device.
		map(0xfc0000, 0xfc1fff).ram();
		map(0xfc2000, 0xfc207f).rw(m_es5506_host, FUNC(es5506_device::read), FUNC(es5506_device::write)).umask16(0x00ff);
		map(0xfc2080, 0xfc2fff).ram();

		// ES5510 host window (filesystem-browser-map.md 4.24):
		// FC3000-FC31FF is the proven ES5510 host window (4.22/4.23 -- the
		// EFFECT DOWNLOAD FAILED / ERROR 032 collision is this range being
		// plain, passive .ram() with no select/commit semantics). Route
		// only the offsets proven meaningful in es5510_device::host_r/
		// host_w -- the 0x00-0x1f latch/register block (byte addresses
		// FC3001-FC303F) and the three select/commit registers 0x80/0xa0/
		// 0xc0 (FC3101, FC3141, FC3181) -- to a real device; every other
		// address in this 512-byte window stays plain .ram(), matching
		// prior (unproven) behavior exactly and not blindly replacing the
		// whole window. host_offset = (cpu_byte_address - 0xFC3001) >> 1;
		// the containing 16-bit word address is one less (e.g. FC3001's
		// word slot is FC3000, FC3181's word slot is FC3180). Byte-lane
		// convention (.umask16(0x00ff), low/odd lane only) matches the
		// proven ES5506 host adapter above; MAME's normal
		// word/byte bus-width shim (not any driver-side special case)
		// makes this transparent to both ordinary move.b and MOVEP's
		// spaced byte accesses. Real precedent for this exact mapping
		// (same device, same .umask16(0x00ff) idiom, whole host_r/host_w
		// window) is esq5505.cpp's map(0x260000, 0x2601ff).rw(m_esp,
		// FUNC(es5510_device::host_r), FUNC(es5510_device::host_w))
		// .umask16(0x00ff); this round deliberately narrows that to only
		// the evidenced offsets, per this investigation's acceptance
		// criteria, and can be widened later if evidence demands it.
		map(0xfc3000, 0xfc303f).rw(m_es5510_host, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);
		map(0xfc3040, 0xfc30ff).ram();
		// FC3100-FC3101/FC3140-FC3141/FC3180-FC3181/FC31C0-FC31C1
		// cannot map directly to host_r/host_w: each is a single-word
		// range, so the map-relative offset MAME supplies is always 0,
		// not the absolute ES5510 host offset (0x80/0xa0/0xc0/0xe0)
		// the firmware intends. Route through the fixed-offset
		// wrappers instead.
		map(0xfc3100, 0xfc3101).rw(FUNC(asr10_boot_state::es5510_host_read_select_r), FUNC(asr10_boot_state::es5510_host_read_select_w)).umask16(0x00ff);
		map(0xfc3102, 0xfc313f).ram();
		map(0xfc3140, 0xfc3141).rw(FUNC(asr10_boot_state::es5510_host_write_select_gpr_r), FUNC(asr10_boot_state::es5510_host_write_select_gpr_w)).umask16(0x00ff);
		map(0xfc3142, 0xfc317f).ram();
		map(0xfc3180, 0xfc3181).rw(FUNC(asr10_boot_state::es5510_host_write_select_instr_r), FUNC(asr10_boot_state::es5510_host_write_select_instr_w)).umask16(0x00ff);
		map(0xfc3182, 0xfc31bf).ram();
		// filesystem-browser-map.md 4.27/4.28: host offset 0xe0
		// ("Write select - GPR + INSTR", es5510.cpp host_w case 0xe0)
		// -- proven required by firmware record type 1 (f97450's
		// static default D4=0x1c0, unmapped and falling through to
		// plain .ram() until this round). Only types 2/3/4 override to
		// 0xc0/0xa0; type 1 is the only one using 0xe0.
		map(0xfc31c0, 0xfc31c1).rw(FUNC(asr10_boot_state::es5510_host_write_select_gpr_instr_r), FUNC(asr10_boot_state::es5510_host_write_select_gpr_instr_w)).umask16(0x00ff);
		map(0xfc31c2, 0xfc31ff).ram();

		map(0xfc3200, 0xfc3fff).ram();
	}
	map(0xfc4000, 0xfc4003).rw(FUNC(asr10_boot_state::upd72069_fdc_r), FUNC(asr10_boot_state::upd72069_fdc_w));
	map(0xfc4004, 0xfc47ff).ram();
	map(0xfc4800, 0xfc481f).rw(FUNC(asr10_boot_state::duart_panel_asr_candidate_r), FUNC(asr10_boot_state::duart_panel_asr_candidate_w));
	map(0xfc4820, 0xfc4fff).ram();
	map(0xfc5000, 0xfc501f).rw(FUNC(asr10_boot_state::scsi_asr_candidate_r), FUNC(asr10_boot_state::scsi_asr_candidate_w));
	// 0xFC6000-0xFC6FFF (the MC68302 internal 4KB window) is owned by
	// m_maincpu itself now -- installed dynamically on BAR write, see
	// mc68302_device::install_internal_window(). This plain RAM range is
	// the neutral fallback for everything else; the device's dynamic
	// install shadows its own slice of it once the ROM programs BAR.
	map(0xfc5020, 0xffffff).ram();
}


void asr10_boot_state::cpu_space_map(address_map &map)
{
	// M68000 interrupt acknowledge cycles. This preserves the default
	// autovector behavior unless an ASR boot-harness vector experiment is on.
	map(0xfffff3, 0xfffff3).lr8(NAME([this]() { return maincpu_iack_r(1); }));
	map(0xfffff5, 0xfffff5).lr8(NAME([this]() { return maincpu_iack_r(2); }));
	map(0xfffff7, 0xfffff7).lr8(NAME([this]() { return maincpu_iack_r(3); }));
	map(0xfffff9, 0xfffff9).lr8(NAME([this]() { return maincpu_iack_r(4); }));
	map(0xfffffb, 0xfffffb).lr8(NAME([this]() { return maincpu_iack_r(5); }));
	map(0xfffffd, 0xfffffd).lr8(NAME([this]() { return maincpu_iack_r(6); }));
	map(0xffffff, 0xffffff).lr8(NAME([this]() { return maincpu_iack_r(7); }));
}


void asr10_boot_state::es5506_wavetable_map(address_map &map)
{
	map(0x000000, 0x1fffff).ram();
}

void asr10_boot_state::es5506_unpopulated_wavetable_map(address_map &map)
{
	map(0x000000, 0x1fffff).noprw();
}


u8 asr10_boot_state::maincpu_iack_r(u8 level)
{
	const u8 autovector = m68000_base_device::autovector(level);
	u8 vector = autovector;
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u16 fc6812 = m_m68302_internal_shadow[0x12 >> 1];
	const u16 fc6814_before = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816_before = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818 = m_m68302_internal_shadow[0x18 >> 1];
	const u16 fc684a = m_m68302_internal_shadow[0x4a >> 1];
	const u16 fc6850 = m_m68302_internal_shadow[0x50 >> 1];
	const u16 fc6852 = m_m68302_internal_shadow[0x52 >> 1];
	bool custom_vector = false;

	if (level == 6)
	{
		// docs/asr10/PLAN.md fas 3 steg 2 (minimal slice): the external
		// IRQ6 vector-supply formula now lives in the mc68302 device
		// itself, not in a driver-side shadow. See
		// mc68302_device::irq6_ack_vector().
		vector = m_maincpu->irq6_ack_vector();
		custom_vector = true;
	}


	m_iack_trace_count++;
	logerror("ASR10_M68K_IACK count=%u irq_level=%u default_autovector=%02x returned_vector=%02x "
		"custom_vector=%u pc=%06x sr=%04x sr_mask=%u fc6812=%04x "
		"fc6814_before=%04x fc6814_after=%04x fc6816_before=%04x fc6816_after=%04x fc6818=%04x "
		"fc684a=%04x fc6850=%04x fc6852=%04x "
		"dispatcher_count=%u panel=\"%s\"\n",
		m_iack_trace_count, level, autovector, vector, custom_vector ? 1 : 0,
		pc, sr, (sr >> 8) & 7, fc6812,
		fc6814_before, m_m68302_internal_shadow[0x14 >> 1],
		fc6816_before, m_m68302_internal_shadow[0x16 >> 1], fc6818,
		fc684a, fc6850, fc6852,
		m_runtime_dispatch_entry_count, m_panel_text);

	return vector;
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

	if (!m_maincpu->cs0_covers(0))
	{
		const u16 data = m_lowmem_shadow[offset] & mem_mask;
		if (byte_address == 0x04ee && !machine().side_effects_disabled())
			(void)0;
		else if (byte_address == 0x049c && !machine().side_effects_disabled())
			(void)0;
		else if (m_seen_insert_disk_prompt && is_fdc_state_field(byte_address) && !machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10FDCSTATE_READ time=%s pc=%06x addr=%06x field=%s value=%04x mem_mask=%04x "
				"last_command=%02x phase=post_insert_disk\n",
				machine().time().to_string(), pc, byte_address, fdc_state_field_name(byte_address),
				data, mem_mask, m_fdc_last_aux_command);
		}
		if (!machine().side_effects_disabled())
			(void)0;
		if (!machine().side_effects_disabled())
			(void)0;
		// filesystem-browser-map.md 4.15: early-epoch instrumentation,
		// gated purely on `enabled` (from reset) -- see the matching
		// block in lowmem_w for the gate-correction rationale.
		return data;
	}

	const u8 *rom = m_rom->base();
	const u32 rom_offset = byte_address & ROM_MASK;
	return (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
}


u8 asr10_boot_state::lowmem_byte(u32 address) const
{
	const u16 word = m_lowmem_shadow[(address >> 1) & (LOWMEM_WORDS - 1)];
	return BIT(address, 0) ? u8(word) : u8(word >> 8);
}


u16 asr10_boot_state::lowmem_word(u32 address) const
{
	return m_lowmem_shadow[(address >> 1) & (LOWMEM_WORDS - 1)];
}


u32 asr10_boot_state::lowmem_long(u32 address) const
{
	return (u32(lowmem_word(address)) << 16) | lowmem_word(address + 2);
}

u32 asr10_boot_state::panel_ready_slot_count() const
{
	u32 ready_slots = 0;
	const u16 slot_base = lowmem_word(0x00c6);
	const u16 slot_end = lowmem_word(0x00c8);
	const u32 span = (slot_end >= slot_base) ? (slot_end - slot_base) : 0;
	const u32 slot_count = span / 0x16;
	for (u32 slot = 0; slot < slot_count && slot < 128; slot++)
	{
		const u32 base = slot_base + slot * 0x16;
		if (lowmem_byte(base + 2) != lowmem_byte(base + 3))
			ready_slots++;
	}
	return ready_slots;
}









TIMER_CALLBACK_MEMBER(asr10_boot_state::lrclk_toggle)
{
	m_lrclk_level = !m_lrclk_level;
	m_maincpu->set_pb_input(3, m_lrclk_level);
}




std::string asr10_boot_state::dump_cpu_registers() const
{
	return util::string_format(
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
		"a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x",
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u32(m_maincpu->state_int(M68K_A4)), u32(m_maincpu->state_int(M68K_A5)),
		u32(m_maincpu->state_int(M68K_A6)));
}


const char *asr10_boot_state::es5506_register_name(u32 cpu_displacement)
{
	// Per es5506-chain-verification.md: with the odd-lane/16-bit-bus
	// convention proven in esqkt.cpp/macrossp.cpp/ssv.cpp, the CPU
	// displacement equals the chip's own datasheet register byte address
	// directly (case labels in es5506.cpp are written as "N/8" where N is
	// exactly this displacement). PAR/IRQV/PAGE are identical across all
	// three page banks (es5506.cpp:1421/1498/1522 etc); the others differ
	// by bank, and we cannot read the device's private m_current_page, so
	// both interpretations are named. Displacements not aligned to 8 are
	// the low half-word of a 4-byte register (movep.w instead of movep.l).
	const u32 aligned = cpu_displacement & ~7U;
	const bool low_half = (cpu_displacement & 7U) == 4;
	const char *base_name = nullptr;
	switch (aligned)
	{
	case 0x00: base_name = "CR(low)/CR(high)/CH0L(test)"; break;
	case 0x08: base_name = "FC(low)/START(high)/CH0R(test)"; break;
	case 0x10: base_name = "LVOL(low)/END(high)/CH1L(test)"; break;
	case 0x18: base_name = "LVRAMP(low)/ACCUM(high)/CH1R(test)"; break;
	case 0x20: base_name = "RVOL(low)/O4n-1(high)/CH2L(test)"; break;
	case 0x28: base_name = "RVRAMP(low)/O3n-1(high)/CH2R(test)"; break;
	case 0x30: base_name = "ECOUNT(low)/O3n-2(high)/CH3L(test)"; break;
	case 0x38: base_name = "K2(low)/O2n-1(high)/CH3R(test)"; break;
	case 0x40: base_name = "K2RAMP(low)/O2n-2(high)/CH4L(test)"; break;
	case 0x48: base_name = "K1(low)/O1n-1(high)/CH4R(test)"; break;
	case 0x50: base_name = "K1RAMP(low)/W_ST(high)/CH5L(test)"; break;
	case 0x58: base_name = "ACTV(low)/W_END(high)/CH5-6R(test)"; break;
	case 0x60: base_name = "MODE(low)/LR_END(high)/EMPTY(test)"; break;
	case 0x68: return "PAR(all banks)";
	case 0x70: return "IRQV(all banks)";
	case 0x78: return "PAGE(all banks)";
	default: return "unknown/outside_register_file";
	}
	static thread_local std::string buf;
	buf = low_half ? (std::string(base_name) + "[low-halfword]") : base_name;
	return buf.c_str();
}


u16 asr10_boot_state::analog_r()
{
	const u8 channel = m_duart_io & 7;
	const u16 value = (m_analog_values[channel] >> 6) & 0x03ff;

	return value;
}

void asr10_boot_state::analog_w(offs_t offset, u16 data)
{
	m_analog_values[offset & 7] = data;
}

void asr10_boot_state::duart_output(u8 data)
{
	m_duart_io = data;
}


// ES5510 host select/commit wrappers (filesystem-browser-map.md 4.24 TASK
// 2/3). FC3100-FC3101, FC3140-FC3141 and FC3180-FC3181 are
// each installed as their own single-word address_map range, so the
// `offset` MAME hands to an .rw() handler there is always 0 (relative to
// that range's own base address) -- never the absolute ES5510 host offset
// (0x80/0xa0/0xc0) the firmware intends. Mapping these three ranges
// directly to es5510_device::host_r/host_w (as FC3000-FC303F correctly is,
// since its offsets 0x00-0x1f fall out of that range's own base by
// construction) silently forwarded offset 0 for all three registers --
// an ASR-10 adapter address-decode bug, not a stock-device defect. Each
// wrapper below ignores the map-relative offset entirely and supplies the
// fixed absolute host offset explicitly.
u8 asr10_boot_state::es5510_host_read_select_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0x80);
}

void asr10_boot_state::es5510_host_read_select_w(offs_t offset, u8 data)
{
	m_es5510_host->host_w(0x80, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xa0);
}

void asr10_boot_state::es5510_host_write_select_gpr_w(offs_t offset, u8 data)
{
	m_es5510_host->host_w(0xa0, data);
}

u8 asr10_boot_state::es5510_host_write_select_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xc0);
}

void asr10_boot_state::es5510_host_write_select_instr_w(offs_t offset, u8 data)
{
	m_es5510_host->host_w(0xc0, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xe0);
}

void asr10_boot_state::es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data)
{
	m_es5510_host->host_w(0xe0, data);
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
	if ((byte_address == 0x0dd6 || byte_address == 0x0df2) && !machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10_DIVIDER_TASK2 event=%s pc=%06x address=%06x previous=%04x new=%04x mem_mask=%04x "
			"d0=%08x d1=%08x d2=%08x\n",
			byte_address == 0x0dd6 ? "store_0dd6_rate_param" : "store_0df2_divider_result",
			pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)));
	}
	if constexpr (ASR10_DIAG_PANEL_B)
	{
		if (!machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (!m_node_89a2_logged && mem_mask == 0xffff && m_lowmem_shadow[offset] == 0x89a2)
			{
				m_node_89a2_logged = true;
				logerror("ASR10_NODE_89A2_FIRST event=node_type_write pc=%06x "
					"node_plus02_address=%06x node_base_guess=%06x type=89a2\n",
					pc, byte_address, byte_address >= 2 ? byte_address - 2 : 0);
			}
			if (!m_slot0_0202_logged && byte_address == 0x23d6 && m_lowmem_shadow[offset] == 0x0202)
			{
				m_slot0_0202_logged = true;
				logerror("ASR10_SLOT0_0202_FIRST event=state_write pc=%06x slot0_state=0202 "
					"head=%04x tail=%04x\n",
					pc, lowmem_word(0x23e4), lowmem_word(0x23e6));
			}
			if (pc == 0x00f89a7a && byte_address <= 0x03b7 && byte_address + 1 >= 0x0378)
			{
				if (ACCESSING_BITS_8_15 && byte_address >= 0x0378 && byte_address <= 0x03b7)
				{
					m_panel_b_last_ring_write_valid = true;
					m_panel_b_last_ring_write_address = byte_address;
					m_panel_b_last_ring_write_byte = u8(m_lowmem_shadow[offset] >> 8);
					m_panel_b_last_ring_write_pc = pc;
					note_panel_ring_store(pc, byte_address, m_panel_b_last_ring_write_byte);
				}
				if (ACCESSING_BITS_0_7 && byte_address + 1 >= 0x0378 && byte_address + 1 <= 0x03b7)
				{
					m_panel_b_last_ring_write_valid = true;
					m_panel_b_last_ring_write_address = byte_address + 1;
					m_panel_b_last_ring_write_byte = u8(m_lowmem_shadow[offset]);
					m_panel_b_last_ring_write_pc = pc;
					note_panel_ring_store(pc, byte_address + 1, m_panel_b_last_ring_write_byte);
				}
			}
			if (byte_address == 0x03bc && ACCESSING_BITS_8_15)
			{
				if (pc == 0x00f89a8a)
				{
					if (m_panel_b_last_ring_write_valid)
						note_panel_ring_store(pc, m_panel_b_last_ring_write_address, m_panel_b_last_ring_write_byte);
					(void)0;
				}
				else if (pc == 0x00f89ab8)
				{
					(void)0;
				}
			}
			if (byte_address == 0x03c4 && ACCESSING_BITS_0_7 && pc == 0x00f89ace)
			{
				logerror("ASR10_DIAG_PANEL_B event=PANEL_F89ACE_CLEAR pc=%06x previous_03c5=%02x "
					"current_03c5=%02x count_03bc=%02x parser_state_03c0=%04x slot0_state=%04x "
					"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
					pc, u8(previous), lowmem_byte(0x03c5), lowmem_byte(0x03bc), lowmem_word(0x03c0),
					lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
			}
			if (pc == 0x00f89ac2 && byte_address == 0x23d6)
			{
				(void)0;
				m_gen_counter++;
				{
					std::string gen_hex;
					for (u8 hex_index = 0; hex_index < m_gen_thrb_count; hex_index++)
					{
						if (hex_index)
							gen_hex += ' ';
						gen_hex += util::string_format("%02x", m_gen_thrb_bytes[hex_index]);
					}
					logerror("ASR10_GEN_TRACKING GEN=%u slot0_state=%04x head=%04x tail=%04x "
						"count_03bc=%02x flag_03c5=%02x\n",
						m_gen_counter, m_lowmem_shadow[offset], lowmem_word(0x23e4), lowmem_word(0x23e6),
						lowmem_byte(0x03bc), lowmem_byte(0x03c5));
					logerror("ASR10_GEN_TRACKING GEN=%u THRB_HEX=%s panel=\"%s\"\n",
						m_gen_counter, gen_hex.c_str(), m_panel_text);
					m_gen_thrb_count = 0;
				}
				}
		}
	}
	if (byte_address == 0x0d06 && (m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff) == 0x0000bf22 &&
		ACCESSING_BITS_8_15 && u8(previous >> 8) == 0x00 && u8(m_lowmem_shadow[offset] >> 8) == 0xff)
	{
		m_fc6816_service_0d06_set_after_runtime = true;
		m_fc6816_service_0d06_rte_count = m_f87f96_queue_rte_count;
		m_fc6816_service_0d06_pc = 0x0000bf22;
	}
	(void)0;
	(void)0;
	if (m_fdc_cmd46_result_complete && byte_address >= 0x04c6 && byte_address <= 0x04cc)
		(void)0;
	if (byte_address == 0x04b0)
		(void)0;
	if (byte_address == 0x00c0)
		(void)0;
	if (byte_address == 0x04ee)
		(void)0;
	else if (byte_address == 0x049c)
		(void)0;
	else if (byte_address == 0x04c6)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		if (ACCESSING_BITS_8_15 && pc == 0x00fb8db2 && m_fdc_last_aux_command == 0xf3 &&
			(read_stack_long(sp) & 0x00ffffff) == 0x00fb7c78)
		{
			(void)0;
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

	// filesystem-browser-map.md 4.15: early-epoch instrumentation, gated
	// purely on `enabled` (from reset) -- f880fc/seen_f880fc is proven too
	// late for the calibration-transition search (it is a PC inside trap
	// #6's own body, not a universal post-tuning landmark).

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
	if (m_maincpu->cs0_covers(0))
		trace_access(trace_region::HIGH_ROM_ALIAS, false, runtime_address_24, data, mem_mask, data);
	return data;
}


void asr10_boot_state::high_alias_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00f80000 | (offset << 1);
	trace_access(trace_region::HIGH_ROM_ALIAS, true, address, data, mem_mask, data);
}


u16 asr10_boot_state::upd72069_fdc_r(offs_t offset, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	(void)0;
	++m_fdc_trace_sequence;
	++m_fdc_transaction_access;

	u8 raw_data = 0;
	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1)
	{
		raw_data = m_fdc->msr_r();
		m_fdc_last_msr = raw_data;
		if (m_fdc_cmd0e_active && m_fdc_txn_read_count && !(raw_data & 0x40))
			(void)0;
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
					(void)0;
				else if (pc == 0x00fb8da8)
					(void)0;
			}
		}
	}
	else if ((address & 3) == 3)
	{
		const u8 device_data = m_fdc->fifo_r();
		if (m_fdc_cmd46_active)
		{
			m_fdc_cmd46_total_fifo_reads++;
			if (BIT(m_fdc_last_msr, 5))
			{
				if (m_fdc_cmd46_transaction == 1 && m_fdc_cmd46_msr_exm_seen_count < m_fdc_cmd46_first_data_bytes.size())
					m_fdc_cmd46_first_data_bytes[m_fdc_cmd46_msr_exm_seen_count] = device_data;
				m_fdc_cmd46_msr_exm_seen_count++;
			}
			else
				m_fdc_cmd46_last_msr_before_result = m_fdc_last_msr;
		}
		raw_data = device_data;
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
				(void)0;
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
	if (m_seen_loading_system_prompt && !machine().side_effects_disabled())
	{
		m_post_loading_fdc_access_count++;
		logerror("ASR10_POST_LOADING_FDC pc=%06x addr=%06x rw=R data=%04x mem_mask=%04x "
			"last_aux=%02x txn=%u txn_access=%u detail=%s access_count=%u panel=\"%s\"\n",
			pc, address, result, mem_mask, m_fdc_last_aux_command, m_fdc_transaction,
			m_fdc_transaction_access, detail, m_post_loading_fdc_access_count, m_panel_text);
	}
	trace_access(trace_region::UPD72069_FDC_CANDIDATE, false, address, result, mem_mask, m_fdc_last_aux_command);
	return result;
}


void asr10_boot_state::upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	(void)0;
	++m_fdc_trace_sequence;

	const char *detail = "upd72069_register_unknown";
	if ((address & 3) == 1 && ACCESSING_BITS_0_7)
	{
		if (m_fdc_txn_summary_active)
		{
			if (m_fdc_last_aux_command == 0x88 || m_fdc_last_aux_command == 0xf3)
				(void)0;
			(void)0;
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
		if (m_fdc_last_aux_command == 0x0e || m_fdc_last_aux_command == 0x1e ||
			m_fdc_last_aux_command == 0x2e || m_fdc_last_aux_command == 0x3e ||
			m_fdc_last_aux_command == 0x4e || m_fdc_last_aux_command == 0x5e ||
			m_fdc_last_aux_command == 0x6e || m_fdc_last_aux_command == 0x7e ||
			m_fdc_last_aux_command == 0x8e || m_fdc_last_aux_command == 0x9e ||
			m_fdc_last_aux_command == 0xae || m_fdc_last_aux_command == 0xbe ||
			m_fdc_last_aux_command == 0xce || m_fdc_last_aux_command == 0xde ||
			m_fdc_last_aux_command == 0xee || m_fdc_last_aux_command == 0xfe)
		{
			if (floppy_image_device *const floppy = m_floppy_connector->get_device())
			{
				m_floppy_is_loaded = floppy->exists();
				m_floppy_is_active = !floppy->mon_r();
				update_floppy_inputs();
			}
		}
		if (ASR10_MISSING_FDC_RATE_SOURCE && m_fdc_last_aux_command == 0x88)
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
				"meaning=%s missing_fdc_rate_source=%u effective_data_rate=%u "
				"drive_attached=%u media_mounted=%u ready=%u motor=%u density=%s\n",
				m_fdc_last_aux_command, m_fdc_transaction, pc, m_fdc_last_aux_command,
				sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
				m_fdc_last_aux_command == 0x88 ? "control_data_rate_250000_missing_source_forces_500000" : "precompensation",
				ASR10_MISSING_FDC_RATE_SOURCE && m_fdc_last_aux_command == 0x88 ? 1 : 0,
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
		if (m_seen_loading_system_prompt && !machine().side_effects_disabled())
		{
			if (!m_fdc_os_cmd_active)
			{
				m_fdc_os_cmd_active = true;
				m_fdc_os_cmd_opcode = u8(data);
				m_fdc_os_cmd_len = 1;
				m_fdc_os_cmd_bytes[0] = u8(data);
				m_fdc_os_cmd_pc = pc;
				switch (m_fdc_os_cmd_opcode & 0x1f)
				{
				case 0x03:
				case 0x0f:
					m_fdc_os_cmd_expected_len = 3;
					break;
				case 0x04:
				case 0x07:
				case 0x0a:
					m_fdc_os_cmd_expected_len = 2;
					break;
				case 0x05:
				case 0x06:
				case 0x09:
				case 0x0c:
				case 0x11:
				case 0x19:
				case 0x1d:
					m_fdc_os_cmd_expected_len = 9;
					break;
				case 0x0d:
					m_fdc_os_cmd_expected_len = 6;
					break;
				default:
					m_fdc_os_cmd_expected_len = 1;
					break;
				}
			}
			else if (m_fdc_os_cmd_len < m_fdc_os_cmd_bytes.size())
			{
				m_fdc_os_cmd_bytes[m_fdc_os_cmd_len] = u8(data);
				m_fdc_os_cmd_len++;
			}
			if (m_fdc_os_cmd_active && m_fdc_os_cmd_len >= m_fdc_os_cmd_expected_len)
			{
				const u8 os_cmd_track = (m_fdc_os_cmd_expected_len >= 3) ? m_fdc_os_cmd_bytes[2] : 0;
				const u8 os_cmd_sector = (m_fdc_os_cmd_expected_len == 9) ? m_fdc_os_cmd_bytes[4] : 0;
				logerror("ASR10_FDC_OS event=command cmd=%02x track=%u sector=%u pc=%06x len=%u\n",
					m_fdc_os_cmd_opcode, os_cmd_track, os_cmd_sector, m_fdc_os_cmd_pc, m_fdc_os_cmd_len);
				m_fdc_os_cmd_active = false;
			}
		}
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
			m_fdc_cmd46_total_fifo_reads = 0;
			m_fdc_cmd46_msr_exm_seen_count = 0;
			m_fdc_cmd46_last_msr_before_result = 0;
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
	if (m_seen_loading_system_prompt && !machine().side_effects_disabled())
	{
		m_post_loading_fdc_access_count++;
		logerror("ASR10_POST_LOADING_FDC pc=%06x addr=%06x rw=W data=%04x mem_mask=%04x "
			"last_aux=%02x last_fifo_write=%02x txn=%u txn_access=%u detail=%s access_count=%u panel=\"%s\"\n",
			pc, address, data, mem_mask, m_fdc_last_aux_command, m_fdc_last_fifo_write,
			m_fdc_transaction, m_fdc_transaction_access, detail, m_post_loading_fdc_access_count, m_panel_text);
	}

	trace_access(trace_region::UPD72069_FDC_CANDIDATE, true, address, data, mem_mask, m_fdc_last_aux_command);
}


u16 asr10_boot_state::duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	const u32 address = 0x00fc4800 | (offset << 1);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	(void)0;

	// The real SCN2681 register file (ACR, CTU/CTL preload and start/stop
	// counter commands, MR/CR/SR, RHR/THR) lives in m_duart now. Panel reply
	// bytes are injected into channel B's RX FIFO, not shadowed here.
	const int fifo_before_read = m_duart->m_chanB->rx_fifo_count();
	u16 raw_data = ACCESSING_BITS_0_7 ? m_duart->read(word) : 0;
	if (!machine().side_effects_disabled() && m_panel_receive_live_active && ACCESSING_BITS_0_7)
	{
		const bool is_srb = address == 0x00fc4812;
		const bool is_rhrb = address == 0x00fc4816;
		if (is_srb || is_rhrb)
		{
			const attotime now = machine().time();
			const double delta_ms = (m_panel_receive_live_last_access_time == attotime::never)
				? -1.0
				: (now - m_panel_receive_live_last_access_time).as_double() * 1000.0;
			m_panel_receive_live_last_access_time = now;
			if (is_srb)
				m_panel_receive_live_srb_reads++;
			else
				m_panel_receive_live_rhrb_reads++;
			osd_printf_info("ASR10_PANEL_RECEIVE_LIVE event=duart_read time=%s delta_ms=%.3f "
				"pc=%06x reg=%s address=%06x data=%02x fifo_before=%d fifo_after=%d "
				"srb_reads=%u rhrb_reads=%u\n",
				now.to_string(), delta_ms, pc, is_srb ? "SRB" : "RHRB",
				address | 1, u8(raw_data), fifo_before_read, m_duart->m_chanB->rx_fifo_count(),
				m_panel_receive_live_srb_reads, m_panel_receive_live_rhrb_reads);
		}
	}
	if (address == 0x00fc4808 && ACCESSING_BITS_0_7)
	{
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
	const u16 data = raw_data & mem_mask;
	if constexpr (ASR10_DIAG_PANEL_B)
	{
		if (!machine().side_effects_disabled() && address == 0x00fc4816 && ACCESSING_BITS_0_7)
			(void)0;
	}
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
	trace_access(trace_region::DUART_PANEL_ASR_CANDIDATE, false, address | (ACCESSING_BITS_0_7 ? 1 : 0), data, mem_mask, data);
	return data;
}


void asr10_boot_state::duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	const u32 address = (0x00fc4800 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	(void)0;
	if (ACCESSING_BITS_0_7)
		m_duart->write(word, u8(data));
	if (!machine().side_effects_disabled() && ACCESSING_BITS_0_7 &&
		(address == 0x00fc480b || address == 0x00fc4819))
	{
		osd_printf_info("ASR10_PANEL_RECEIVE_LIVE event=duart_write time=%s pc=%06x "
			"reg=%s address=%06x value=%02x irq_pending=%u\n",
			machine().time().to_string(), pc,
			address == 0x00fc480b ? "IMR" : "IVR",
			address, u8(data), m_duart->irq_pending() ? 1 : 0);
	}
	if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
	{
		const u8 character = u8(data);
		(void)0;
		m_panel_receive_role = u8(panel_byte_role::SERIAL);
		if (!machine().side_effects_disabled())
		{
			if (pc == 0x00f89aa4)
			{
				m_panel_receive_role = u8(consume_panel_ring_role(pc));
			}
			else if (pc == 0x00f89c48)
			{
				const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
				if ((read_stack_long(sp) & 0x00ffffff) == 0x00f89c9a)
				{
					m_panel_direct_text_active = true;
					m_panel_receive_role = u8(panel_byte_role::DIRECT_TEXT_PREFIX);
				}
			}
			else if (pc == 0x00f89cb0 && m_panel_direct_text_active)
			{
				m_panel_receive_role = u8(panel_byte_role::TEXT_PAYLOAD);
				const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
				auto const disable_side_effects = machine().disable_side_effects();
				if (m_maincpu->space(AS_PROGRAM).read_byte(a2) == 0)
					m_panel_direct_text_active = false;
			}
		}
		if (!machine().side_effects_disabled() && m_gen_thrb_count < m_gen_thrb_bytes.size())
			m_gen_thrb_bytes[m_gen_thrb_count++] = character;
		if (m_seen_loading_system_prompt && !machine().side_effects_disabled())
		{
			m_post_loading_panel_write_count++;
			logerror("ASR10_POST_LOADING_PANEL_WRITE pc=%06x data=%02x printable=%u char='%c' count=%u\n",
				pc, character, character >= 0x20 && character <= 0x7e ? 1 : 0,
				character >= 0x20 && character <= 0x7e ? char(character) : '.',
				m_post_loading_panel_write_count);
		}
		panel_receive_byte(character);
		m_panel_receive_role = u8(panel_byte_role::SERIAL);
	}
	trace_access(trace_region::DUART_PANEL_ASR_CANDIDATE, true, address, data, mem_mask, data);
}



void asr10_boot_state::panel_receive_byte(u8 data)
{
	const panel_byte_role role = panel_byte_role(m_panel_receive_role);

	if (m_panel_transport_pending_marker)
	{
		m_panel_transport_pending_marker = 0;
		return;
	}

	if (data >= 0x77 && data <= 0x7c)
	{
		flush_panel_text();
		m_panel_transport_pending_marker = data;
		return;
	}

	if (role == panel_byte_role::RING_CONTROL || role == panel_byte_role::DIRECT_TEXT_PREFIX)
	{
		flush_panel_text();
		return;
	}

	if (data < 0x20 || data > 0x7e)
	{
		flush_panel_text();
		return;
	}

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;

	// Ny textsekvens: töm den visuella displayen.
	if (m_panel_text_length == 0)
	{
		clear_display();
		m_panel_msg_first_pc = pc;
	}
	m_panel_msg_last_pc = pc;

	// Visa samma tecken som diagnostikbufferten tar emot.
	if (m_display_position < ASR10_DISPLAY_LENGTH)
	{
		m_display_chars[m_display_position] = data;
		m_display[m_display_position] = ascii_to_14seg(data);
		m_display_position++;
	}

	if (m_panel_text_length == PANEL_TEXT_LENGTH - 1)
		flush_panel_text();

	m_panel_text[m_panel_text_length++] = char(data);
	m_panel_text[m_panel_text_length] = 0;

	if (!m_panel_receive_live_active && strstr(m_panel_text, "FILE 1  TUTORIAL BNK"))
	{
		m_panel_receive_live_active = true;
		osd_printf_info("ASR10_PANEL_RECEIVE_LIVE event=active display=\"%s\"\n", m_panel_text);
	}
	if (m_panel_receive_live_active && !m_panel_receive_live_window_dumped)
	{
		m_panel_receive_live_window_dumped = true;
		auto const disable_side_effects = machine().disable_side_effects();
		for (const u32 base : { 0x00ffb0bcU, 0x0000b0bcU, 0x00ffb0d4U, 0x0000b0d4U, 0x00ffb0b0U })
		{
			char hex[129]{};
			for (u32 index = 0; index != 64; index++)
			{
				const u8 byte = m_maincpu->space(AS_PROGRAM).read_byte(base + index);
				snprintf(&hex[index * 2], 3, "%02x", byte);
			}
			osd_printf_info("ASR10_PANEL_RECEIVE_LIVE_DUMP base=%06x len=64 hex=%s\n",
				base & 0x00ffffff, hex);
		}
	}

	(void)0;

	if (!m_insert_disk_decision_logged && strstr(m_panel_text, "PLEASE INSERT DISK"))
	{
		m_insert_disk_decision_logged = true;
		(void)0;

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
		// filesystem-browser-map.md 4.15 TASK 1: unconditional (not gated
		// on the too-late f880fc landmark) display-timeline entry, active
		// whenever the PTI flag is on, from reset.
		if (strstr(m_panel_text, "LOADING SYSTEM"))
		{
			m_seen_loading_system_prompt = true;
			m_post_loading_panel_write_count = 0;
			m_post_loading_fdc_access_count = 0;
			logerror("ASR10PHASE phase=post_loading_system_panel pc=%06x\n",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff);
		}
		if (strstr(m_panel_text, "ERROR 139 - REBOOT ?"))
		{
			(void)0;
		}
		if (strstr(m_panel_text, "EFFECT DOWNLOAD FAILED"))
		{
			(void)0;
		}
		if (strstr(m_panel_text, "ERROR 032 - REBOOT ?"))
		{
			(void)0;
		}
		if (!m_task1_string_scan_logged &&
			(strstr(m_panel_text, "EFFECT DOWNLOAD FAILED") || strstr(m_panel_text, "ERROR 032 - REBOOT ?")))
		{
			m_task1_string_scan_logged = true;
			scan_for_ascii_string("rom_effect_download_failed", 0x00f80000, 0x00fbffff, "EFFECT DOWNLOAD FAILED");
			scan_for_ascii_string("lowmem_effect_download_failed", 0x00000000, 0x000fffff, "EFFECT DOWNLOAD FAILED");
			scan_for_ascii_string("rom_error_032", 0x00f80000, 0x00fbffff, "ERROR 032");
			scan_for_ascii_string("lowmem_error_032", 0x00000000, 0x000fffff, "ERROR 032");
			scan_for_ascii_string("rom_error_prefix", 0x00f80000, 0x00fbffff, "ERROR ");
			scan_for_ascii_string("lowmem_error_prefix", 0x00000000, 0x000fffff, "ERROR ");
			scan_for_ascii_string("rom_reboot", 0x00f80000, 0x00fbffff, "REBOOT ?");
			scan_for_ascii_string("lowmem_reboot", 0x00000000, 0x000fffff, "REBOOT ?");
			(void)0;
			(void)0;
		}
	}

	m_panel_text_length = 0;
	m_insert_disk_decision_logged = false;
	m_panel_text[0] = 0;
}




void asr10_boot_state::note_panel_ring_store(u32 pc, u32 ring_address, u8 byte)
{
	if (machine().side_effects_disabled() || ring_address < 0x0378 || ring_address > 0x03b7)
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 return_pc = read_stack_long(sp) & 0x00ffffff;
	panel_byte_role role = panel_byte_role::SERIAL;

	if (is_bounded_panel_ring_control_candidate(pc, return_pc, m_last_distinct_pc))
		role = panel_byte_role::RING_CONTROL;

	m_panel_ring_byte_roles[ring_address - 0x0378] = u8(role);
}


asr10_boot_state::panel_byte_role asr10_boot_state::consume_panel_ring_role(u32 pc)
{
	if (!machine().side_effects_disabled() && pc == 0x00f89aa4)
	{
		const u32 ring_address = lowmem_word(0x03ba);
		if (ring_address >= 0x0378 && ring_address <= 0x03b7)
		{
			const u32 index = ring_address - 0x0378;
			const panel_byte_role role = panel_byte_role(m_panel_ring_byte_roles[index]);
			m_panel_ring_byte_roles[index] = u8(panel_byte_role::SERIAL);
			return role;
		}
	}

	return panel_byte_role::SERIAL;
}


bool asr10_boot_state::is_bounded_panel_ring_control_candidate(u32 pc, u32 return_pc, u32 previous_pc) const
{
	if (return_pc != 0x00f89a70)
		return false;

	if (pc == 0x00f89a8a)
		return true;

	if (pc != 0x00f89a7a)
		return false;

	if (previous_pc == 0x00f893a8)
		return false;

	if (previous_pc >= 0x00f8a7dc && previous_pc <= 0x00f8a808)
		return false;

	return previous_pc >= 0x00ff0000;
}


void asr10_boot_state::maincpu_instruction_hook(u32 pc)
{
	pc &= 0x00ffffff;
}


void asr10_boot_state::panel_receive_live_summary()
{
	osd_printf_info("ASR10_PANEL_RECEIVE_LIVE_SUMMARY active=%u fc4813_srb_reads=%u "
		"fc4817_rhrb_reads=%u queue_calls=%u fifo_overrun_pushes=%u\n",
		m_panel_receive_live_active ? 1 : 0,
		m_panel_receive_live_srb_reads,
		m_panel_receive_live_rhrb_reads,
		m_panel_receive_live_queue_calls,
		m_panel_receive_live_fifo_overrun_pushes);
}


void asr10_boot_state::mc68302_access_summary()
{
	// PLAN.md fas 3's acceptance oracle: every access to the internal
	// SIB window is known/internal_ram/known_unimplemented/unknown. Raw
	// access counts (known_access_count() etc.) are dominated by tight
	// poll loops hitting a handful of addresses and are not useful on
	// their own -- distinct addresses touched, and the hottest few, are.
	// internal_ram (0x000-0x3ff, plain dual-port RAM -- the ROM's own
	// supervisor stack lives there) is split out from known_unimplemented
	// because it was inflating that count by 13M+ hits with no register
	// guesswork behind it at all.
	const auto distinct = m_maincpu->distinct_offset_counts();
	osd_printf_info("ASR10_MC68302_ACCESS_SUMMARY known=%u internal_ram=%u known_unimplemented=%u unknown=%u "
		"distinct_known=%u distinct_internal_ram=%u distinct_known_unimplemented=%u distinct_unknown=%u\n",
		m_maincpu->known_access_count(), m_maincpu->internal_ram_access_count(),
		m_maincpu->known_unimplemented_access_count(), m_maincpu->unknown_access_count(),
		distinct.known, distinct.internal_ram, distinct.known_unimplemented, distinct.unknown);

	unsigned rank = 0;
	for (const auto &hit : m_maincpu->top_accessed_offsets(5))
	{
		osd_printf_info("ASR10_MC68302_ACCESS_TOP rank=%u offset=%04x count=%u\n",
			++rank, hit.byte_offset, hit.count);
	}
}


// CS3 window (0xFC4000-0xFC5FFF): FDC 0xfc4000-3, unaddressed RAM
// 0xfc4004-47ff, DUART 0xfc4800-481f, unaddressed RAM 0xfc4820-4fff,
// SCSI candidate 0xfc5000-501f, unaddressed RAM 0xfc5020-5fff.
// byte_offset is 0-based from 0xfc4000 (see the class-body note on the
// tap's offset convention -- this is a raw byte offset, not word-shifted).
asr10_boot_state::cs3_access_class asr10_boot_state::classify_cs3_offset(u16 byte_offset)
{
	if (byte_offset <= 0x0003) return cs3_access_class::known;               // FDC
	if (byte_offset <= 0x07ff) return cs3_access_class::unknown;             // 0xfc4004-47ff
	if (byte_offset <= 0x081f) return cs3_access_class::known;               // DUART
	if (byte_offset <= 0x0fff) return cs3_access_class::unknown;             // 0xfc4820-4fff
	if (byte_offset <= 0x101f) return cs3_access_class::known_unimplemented; // SCSI candidate
	return cs3_access_class::unknown;                                       // 0xfc5020-5fff
}

asr10_boot_state::cs3_access_class_counts asr10_boot_state::cs3_distinct_offset_counts() const
{
	cs3_access_class_counts result;
	for (size_t offset = 0; offset < m_cs3_access_count.size(); offset++)
	{
		if (!m_cs3_access_count[offset])
			continue;
		switch (classify_cs3_offset(u16(offset)))
		{
		case cs3_access_class::known: result.known++; break;
		case cs3_access_class::known_unimplemented: result.known_unimplemented++; break;
		case cs3_access_class::unknown: result.unknown++; break;
		}
	}
	return result;
}

std::vector<asr10_boot_state::cs3_offset_hit> asr10_boot_state::cs3_top_accessed_offsets(unsigned max_entries) const
{
	std::vector<cs3_offset_hit> hits;
	hits.reserve(m_cs3_access_count.size());
	for (size_t offset = 0; offset < m_cs3_access_count.size(); offset++)
		if (m_cs3_access_count[offset])
			hits.push_back({0x00fc4000 + u32(offset), m_cs3_access_count[offset]});

	const size_t keep = std::min<size_t>(max_entries, hits.size());
	std::partial_sort(hits.begin(), hits.begin() + keep, hits.end(),
		[](const cs3_offset_hit &a, const cs3_offset_hit &b) { return a.count > b.count; });
	hits.resize(keep);
	return hits;
}

void asr10_boot_state::cs3_access_summary()
{
	// Board-level counterpart to mc68302_access_summary(): the same
	// known/known_unimplemented/unknown oracle, but for the CS3 window
	// (FDC, DUART, SCSI candidate) instead of the 68302's internal SIB
	// window. Shows which DUART registers the ROM actually touches,
	// which OR-patterns it writes, and how much of the window is pure
	// guesswork (the SCSI candidate) versus a real device.
	const auto distinct = cs3_distinct_offset_counts();
	u32 known = 0, known_unimplemented = 0, unknown = 0;
	for (size_t offset = 0; offset < m_cs3_access_count.size(); offset++)
	{
		const u32 count = m_cs3_access_count[offset];
		if (!count)
			continue;
		switch (classify_cs3_offset(u16(offset)))
		{
		case cs3_access_class::known: known += count; break;
		case cs3_access_class::known_unimplemented: known_unimplemented += count; break;
		case cs3_access_class::unknown: unknown += count; break;
		}
	}
	osd_printf_info("ASR10_CS3_ACCESS_SUMMARY known=%u known_unimplemented=%u unknown=%u "
		"distinct_known=%u distinct_known_unimplemented=%u distinct_unknown=%u\n",
		known, known_unimplemented, unknown,
		distinct.known, distinct.known_unimplemented, distinct.unknown);

	unsigned rank = 0;
	for (const auto &hit : cs3_top_accessed_offsets(5))
	{
		osd_printf_info("ASR10_CS3_ACCESS_TOP rank=%u address=%06x count=%u\n",
			++rank, hit.address, hit.count);
	}
}




u32 asr10_boot_state::read_stack_long(u32 address)
{
	auto const disable_side_effects = machine().disable_side_effects();
	return m_maincpu->space(AS_PROGRAM).read_dword(address & 0x00ffffff);
}


u16 asr10_boot_state::read_program_word(u32 address)
{
	auto const disable_side_effects = machine().disable_side_effects();
	return m_maincpu->space(AS_PROGRAM).read_word(address & 0x00ffffff);
}


bool asr10_boot_state::likely_rom_address(u32 address)
{
	address &= 0x00ffffff;
	return address >= 0x00f80000 && address <= 0x00fbffff;
}







// filesystem-browser-map.md 4.23 TASK 1: every first-pass upload byte
// write for the 0xfff9bca0-tagged object, so the exact write that
// precedes the 4.22 verify-pass mismatch at FC300F can be found directly
// (not correlated by nearest timestamp). A3 has already been
// post-incremented by the time this fires, so the source ROM address is
// (A3-1); the destination is the CPU-visible byte address the tap
// itself computed.

// filesystem-browser-map.md 4.24 TASK 7: proves the adapter forwards the
// select/commit writes (f97776's "move.b D1,(A0,D4.w)", D4=0x100/0x140/0x180
// selecting FC3101/FC3141/FC3181) with the correct record index into the
// real es5510_device, for the two indices (0 and 58) whose collision this
// round's integration is meant to resolve. host_offset is derived with the
// same formula used throughout this section:
// (cpu_byte_address - 0xFC3001) >> 1.

// filesystem-browser-map.md 4.24 TASK 4: proves, from inside the wrapper
// itself (not just an independent recomputation from address), that the
// single-word FC3101/FC3141/FC3181 ranges' map-relative offset (always 0)
// is being discarded and the fixed absolute ES5510 host offset (0x80/0xa0/
// 0xc0) is what actually reaches host_w. Bounded to record indices 0 and
// 58 by the caller, so this adds no per-retry volume across a 180s run.

// filesystem-browser-map.md 4.25 (observation-only): renders a bounded
// live-memory window as both hex bytes and printable ASCII, via the
// side-effect-free general bus reader (read_program_word) so it works
// regardless of which region (ROM, lowmem, FC-range, etc.) base_address
// falls in. Read word-at-a-time; length_bytes is rounded up to the next
// even number if odd.

// filesystem-browser-map.md 4.26 TASK 3: bounded (cap 20), unconditional
// per-invocation entry-state dump for f973f0, whichever table/object it
// is processing. known_fixed_table distinguishes the already-fixed
// fff9bca0 table from any other. This is triggered by the write of the
// FIRST word of f973f0's own first instruction ("move.l A3,$e7e.w") --
// an entry-state observation, not a perfect pre-instruction hook. A3 is
// read directly from the CPU register (always complete/reliable) and
// used as the entry-time table pointer; lowmem $0e7e is NOT reconstructed
// as a complete longword here, since only the high word of the move.l
// may have been written to memory at the moment this fires (the low word
// write, to byte address 0x0e80, is a separate, later bus cycle).
// caller_return_address is read directly off the stack (SP at this PC
// still holds the return address, since f973f0 is entered via a plain
// bsr/jsr and no nested call has happened yet).

// filesystem-browser-map.md 4.26 TASK 2: one-shot (first occurrence
// only), fires at the FIRST retry-increment (retry_number becomes 1) for
// a table other than the already-fixed fff9bca0 -- NOT terminal retry
// exhaustion (renamed from log_esp_other_table_retry to make this
// explicit; a prior round's name and comments incorrectly implied this
// was the exhaustion/give-up point). Dumps full register/lowmem state
// plus bounded live-memory windows around every plausible base-pointer
// candidate. Does not assume A3 is the object base -- reports A3 minus
// each candidate base so the cursor-vs-base question can be read
// directly from the numbers.

// filesystem-browser-map.md 4.26 TASK 1: one-shot (first REAL mismatch
// only) capture of the f97574 compare ("cmp.b (A6),D2") when the CURRENT
// table is NOT the already-fixed fff9bca0 table. Fed the actual bus-read
// value/mem_mask from the caller (the FC3000-range read tap), not
// rereard via read_program_word -- a prior round's version fired on the
// first COMPARE regardless of outcome, consuming its one-shot even on a
// match. This version checks observed against D2 (the expected value)
// BEFORE touching the one-shot flag, and returns without logging or
// consuming it when the compare actually matches.

// filesystem-browser-map.md 4.26 TASK 6: single consolidated per-event
// logger for the HALL REVERB (table $0e8e==0x00010400) type-1/record-0
// GPR transaction, called from every relevant hook site (FC3000 write
// tap, FC3000 read tap, lowmem_w's 0x0e8c/0x0e8a cases) with an `event`
// tag identifying which step this is. Bounded by a hard event cap (not a
// per-attempt cap), covering roughly attempts 1 through terminal retry
// with headroom; the ordered sequence of events (write_latch_00/01/02,
// write_select_gpr_0xa0, read_select_0x80, read_latch_00/01/02,
// verify_compare, retry_increment, give_up_flag_set) reconstructs the
// full latch->commit->select->readback->retry chain when read in order.
// Does not reach into es5510_device internals (no public accessor for
// gpr[] exists without a select+read cycle, and adding one would change
// the device's public interface) -- "stock GPR entry after commit" is
// observed the same way the firmware itself observes it: via the
// subsequent read-select + latch readback.












// Proves (or disproves) that the active stack is where the JSR/TRAP#9/RTS
// hooks assume it is, before trusting their (null or non-null) results.
// M68K_SP is the currently-active stack pointer (USP if the CPU is in user
// mode, ISP/MSP if supervisor) per m68kcommon.h; M68K_USP/M68K_ISP are the
// two register values directly, regardless of which is currently active,
// so a user/supervisor mismatch is visible even if M68K_SP alone would
// hide it.


// Fires from lowmem_w, pc-gated on one of slot 0's six named `jsr $xxxx.w`
// sites (filesystem-browser-map.md 4.9's authoritative disassembly). Each
// such jsr is a genuine 4-byte instruction pushing a 4-byte (long) return
// address, so it triggers *two* word-write calls to lowmem_w at the same
// pc (high word then low word, same as the 0077ae positive control) --
// this function is called once per word, `word_count` therefore counts
// words, not instructions. `usp_minus_observed` reports USP-observed_address
// as a plain fact (not an assumed-correct formula -- whether MAME's core
// exposes USP pre- or post-decrement at this callback was not established
// by the earlier positive control, which only compared addresses across
// two separate log lines, not against a live USP read at the write
// itself); the actual relationship should be read off this field, not
// asserted. `expected_word` is the high or low half of pc+4 as appropriate
// for whichever of USP or USP-2 the observed address matches (if neither,
// reported against USP itself, so the mismatch is still visible).
// `target` is the known, constant sign-extended jsr operand
// (absolute-short addressing, not indirect -- no register read is needed
// to know where it goes, only whether it goes there at all).




// 002b1a (`cmpa.w $35e.w,A5`) and 002b26 (`move.w $31c.w,D5`) -- the two
// genuine reads inside 002b14's node-classify sequence (see
// filesystem-browser-map.md 4.10 for the authoritative disassembly and
// the corrected register usage: A2/A5, not "D2.w" as informally assumed
// before disassembling). Logs D0-D3/A2/A5 (full 32-bit and the low word
// separately), the value actually read, USP, and active slot/node
// context, so the branch this read feeds can be reconstructed rather
// than assumed.


// trap #4 at 002b2a. TRAP always pushes a genuine exception frame
// (SR+PC) regardless of addressing context, so it's tappable the same
// way as trap #9 (see 4.8) even though this specific call site is
// register-only right up to the trap. Vector 36 (32+4); handler address
// at lowmem $90 (36*4).


// Vectors 34 (trap #2), 44 (trap #12), 46 (trap #14) -- read and dumped
// once, unconditionally at the f880fc gate, independent of whether
// 007000 (which issues all three) ever actually executes. A static
// ROM-wide search for the code that populates these vector-table slots
// found only false-positive byte matches (same pattern as the 6b1c/6cf2/
// d10a search in 4.10) -- the setup is evidently not simple per-vector
// immediate stores, so the handler addresses are obtained by reading the
// already-populated table live instead.


// filesystem-browser-map.md 4.19: mandatory run-config header, logged
// once per machine_reset(), unconditionally (not gated on any experiment
// flag) so every capture is self-describing. Enumerates every known
// ASR10_* environment flag (requested env string, parsed/effective
// value, default when unset), ROM/floppy identity, and what is
// statically known about the DUART/IRQ6 wiring at this point (the
// counter's actual period/IACK vector are runtime facts that only exist
// once the firmware arms the counter -- see the ASR10_DUART_COUNTER
// arm-time log lines for those, not duplicated here).

// filesystem-browser-map.md 4.14 TASK 1/2: a one-shot scan of every
// autovector (1-7, offsets 0x64-0x7c) and every trap vector (0-15,
// offsets 0x80-0xbc), to identify which vector (if any) owns the three
// newly-found static "bra $f87f80" handler fragments (f880b6, f880d6/
// f88108 -- found to fall through into one another with no vector-driven
// frame-build of their own) and f87f76 (a fourth f87f80-continuation
// entry point found in the f87f1a-f87fd0 static map with zero call/branch
// references anywhere in the ROM).


// filesystem-browser-map.md 4.14 TASK 3: entry-id -> name lookup for the
// nine distinct static paths found into f87f3e/f87f80 (5 direct f87f3e
// callers, plus the 4 trap-vector paths -- trap #1 direct, the trap
// #5/#6 fall-through chain, trap #7, and trap #15 -- that reach f87f80
// without going through f87f3e at all).

// Common full-register snapshot logged at every distinct entry point.


// f87f3e's own body entry (tapped at f87f40's genuine write, since f87f3e
// itself is `move USP,A0`, register-only). Confirms *some* caller of the
// five reached here, and reports which one via the stash -- consuming it
// so a later, unrelated hit can't be misattributed.


// The four trap-vector paths that reach f87f80 WITHOUT going through
// f87f3e (trap #1 direct fall-through, the #5/#6 chain, #7, #15). Each
// re-stashes its own identity as the "last entry" so f87f82's correlation
// below attributes correctly even for these non-f87f3e paths.


// f87f82 (`move.w A0,($e,A2)`) -- the shared, caller-agnostic proxy.
// Assigns a monotonic sequence number and reports the correlated entry
// only if one is stashed and not yet consumed (i.e. genuinely proven to
// precede this specific hit, not just "the nearest earlier log line").


// filesystem-browser-map.md 4.14 TASK 4: trap #15 specifics -- source PC
// (fixed, f88056, but logged for uniformity), D0 (the bit number),
// $b6a.w (active slot pointer), and slot +2/+3 immediately before this
// tap (the "after" values are whatever the *next* tap -- f87f80_direct
// or f87f3e_body_entry -- observes, since trap #15's own two bset
// instructions have no memory reference of their own to tap directly;
// bset on an absolute/indexed memory operand IS a genuine read-modify-
// write, but the two bset instructions here target the same byte pair
// tested by the entry-stash snapshot taken at this same PC, so the
// before/after split is: "before" = this tap, at f88056 itself, prior to
// either bset; "after" is reported by whichever f87f80/f87f3e tap
// follows).


// f880d6 (trap #6)'s first genuine access (`move.w ($10,A2),D0`, a read)
// fires unconditionally whether reached via trap #5's fall-through or
// trap #6's own vector directly. If a fresh, unconsumed trap-#5 stash
// (id 2) already precedes it, this is the normal chain -- leave it
// attributed to trap #5 and just consume it here instead of at f87f80,
// since f880d6 is the true unconditional convergence point for that
// chain. Otherwise, trap #6 was entered directly, bypassing trap #5:
// stash a distinct id (10) so it is not misattributed.


// filesystem-browser-map.md 4.15 TASK 3: trap #8 (vector 40, f8812c) --
// `movea.w $b6a.w,A0 / move.w D0,(A0) / rte`: writes D0 into the
// *currently active* slot's own +0 field. Gated purely on `enabled`
// (from reset), per the gate correction -- f880fc/seen_f880fc is proven
// too late (it is a PC inside trap #6's own body) to bound this search.


// filesystem-browser-map.md 4.15 TASK 3: trap #9 (vector 41, f88138) --
// enqueues node A5 onto an explicit queue A1 (head +0x10, tail +0x12),
// clearing bit 7 of A1+2 once posted. Unlike trap #6/#7/#15, A1 is an
// explicit argument, not $b6a.w -- this is the "post to an arbitrary
// target" primitive, the leading candidate for what actually wakes a
// specific target slot (including slot 0) rather than the caller's own.


// filesystem-browser-map.md 4.15 TASK 3: the "$00ca-list" -- a table of
// 26-byte (0x1a) records from $ca.w to $cc.w, each with a countdown at
// +0x14 and a callback function pointer at +0x16, serviced once per tick
// (f88338-f88360, already statically mapped in 4.13's site-1 context).



// filesystem-browser-map.md 4.15 TASK 3/7: broad net on slot 0's own
// +2/+3 ready-flag bytes specifically (address computed live from $c6.w,
// slot 0 being the table's first entry), independent of which PC writes
// them -- directly answers "other scheduler-ready-bit changes affecting
// slot 0" and TASK 7's "is an expected later request for slot 0 absent".



// Not a hook on trap #2's actual RTE -- that instruction has no memory
// reference to tap. 7004 is the first write reached *after* rte, and only
// on the path where the carry-clear (success) branch at 7002 was taken;
// this is used purely as a proxy observation that trap #2 succeeded, not
// as a direct measurement of the trap's own return.




// filesystem-browser-map.md 4.13: the three static call sites of `trap #d`
// (trap #13) found outside its own vector-46 nested caller -- f883ac,
// f88df8, f89b54. Static analysis established all three load A1 from the
// same lowmem cell $dc.w immediately before the trap. Entry tap logs full
// register state, the node at A5, and the queue header at A1, all *before*
// the trap runs (i.e. before anything here could be side-effected by it).


// Return-side observation: sites 0/1 tap the genuine lowmem write at the
// instruction immediately after the trap (f883ae, f88dfa); site 2 has no
// write there (f89b56 is a bare `rts`), so it is tapped as a stack-pop
// *read* from low_rom_or_lowmem_r instead -- same technique already used
// for the ae20 slot-0 early-return proxy (4.9).


// Shared, caller-agnostic proxy for "execution reached the dispatcher's
// context-save continuation at f87f80" -- f87f80 itself is register-only
// (`move USP,A0`), so f87f82 (`move.w A0,($e,A2)`, a genuine write) is
// tapped instead. Not tied to any one of the three call sites above; a
// hit here only proves *some* path reached this shared code, correlated
// with the call-site counts above by proximity/timing, not causally.




// filesystem-browser-map.md 4.16 TASK 3/6: the three confirmed static
// producers of node+2=0x16 (f88e2a, f9068a, f942f2), all reached via
// `trap #3` allocation (alloc without the count-limit check, 4.14). Gated
// purely on `enabled` (from reset), not seen_f880fc, per the 4.15
// correction. Dumps the surrounding gate bytes ($17e.w, $8434.w/$8435.w)
// as plain facts, not asserted as *the* gating condition for all three
// (only f88e2a's own static gate, $17e.w, was confirmed by disassembly).


// filesystem-browser-map.md 4.16 TASK 4/6: the one confirmed static
// producer of node+2=0xd10a, found via disk-image byte search (RAM
// 0x012f88, +0x2600 delta verified). Unlike the 0x16/0x89a2 producers,
// this one targets slot 0 via a *hardcoded* immediate (#$23d4), not a
// variable ($d6.w/$d8.w) -- logged as a plain fact, not generalized.


// filesystem-browser-map.md 4.16 TASK 3/6: broad net on $d6.w, the
// confirmed destination of both f88e2a's and f942f2's node+2=0x16 posts
// (parallel to $dc.w's treatment in 4.13).



// filesystem-browser-map.md 4.17 TASK 2: every write to lowmem $00ca
// itself (the candidate list-root cell).


// filesystem-browser-map.md 4.17 TASK 3: every write to $14d4 (countdown)
// or $14d6-$14d9 (long callback pointer) -- the object corrected this
// round to be reached via a SEPARATE tail case (12f6a) from the
// unconditional-branch d10a producer (12f7e), not directly preceding it.


// filesystem-browser-map.md 4.17 TASK 1/4: the five distinct entry points
// in the 012efc-012f94 dispatcher family (see the corrected control-flow
// account: 12f66's arm-timer tail loops back to 12f3a, NOT into 12f7e --
// they are proven-separate branches, not a sequential pair).


// filesystem-browser-map.md 4.18: one full 22-byte slot-record dump, used
// by the ae12/ae1a/ae20 taps and the system-wide snapshot at ae20.

// filesystem-browser-map.md 4.18 TASK 1: tapped at ae12, trap #5's own
// exception-frame push. D0 is always 0 on entry here (established); logs
// the FULL slot-0 record before trap #5/#6 run at all.


// filesystem-browser-map.md 4.18 TASK 1/2: tapped at ae1a, `jsr $87f2.w`'s
// own genuine return-address push -- fires ONLY when ae18's `beq` was NOT
// taken (i.e. bit 7 of D0, post-AND, was set). D0 here is whatever trap
// #5/#6 left it as (established: overwritten at f880e0 to the popped
// queue-head pointer, or 0 if the queue was empty) -- NOT preserved from
// entry, and NOT restored by the trap mechanism.


// filesystem-browser-map.md 4.18 TASK 1/3/4: tapped at ae20 (the `rts`
// stack-pop read) -- fires when ae18's `beq` WAS taken (bit 7 of D0 was
// clear). Full slot-0 record, A5/node state, the return address on the
// stack, and a system-wide snapshot of every scheduler slot plus the
// known queue/free-list/timer cells.




// Both known "trap #9" call sites are in loaded overlay RAM, not static
// ROM (a ROM-wide search found none) -- these two were found live in
// filesystem-browser-map.md 4.5 (slot 1's own resume code, and slot 0's
// ffa26e->00711e tail-jump target) and are not claimed to be exhaustive;
// any other call site would need to be found the same way, live, first.



// Milestones A/B are content-triggered, one-shot, from inside panel_receive_byte
// (the same accumulation path already used for the "PLEASE INSERT DISK"
// one-shot decision log). Exact trigger, stated plainly so it can be
// checked rather than trusted: A fires the instant a 'T' is appended while
// the assembled text is <=2 characters long (matches the observed
// single-leading-control-char prefix "fTUNING KBD..."); B fires the instant
// a 'K' is appended while the assembled text is between 4 and 7 characters
// (matches the observed "f    KEYBOARD TUNED" prefix). Neither is a
// content-independent guarantee -- a different leading-control-byte count,
// or an unrelated T-/K-word at the same short length, would trigger it
// early or not at all. Documented here and in the map rather than silently
// assumed precise.

// Same shape as dump_loaded_code_range(), but resolves the 0xfc6900-0xffffff
// high-view RAM region via read_highview_word() (a genuine CPU-space read)
// instead of returning the 0xffff placeholder read_loaded_word() gives for
// that range.

void asr10_boot_state::scan_for_ascii_string(const char *tag, u32 start, u32 end, const char *needle)
{
	auto const disable_side_effects = machine().disable_side_effects();
	const size_t needle_len = strlen(needle);
	if (needle_len == 0 || end <= start || end - start < needle_len)
		return;
	u32 matches = 0;
	for (u32 cursor = start; cursor <= end - needle_len; cursor++)
	{
		bool match = true;
		for (size_t i = 0; i < needle_len && match; i++)
		{
			const u32 addr = cursor + u32(i);
			const u16 word = read_loaded_word(addr & ~u32(1));
			const u8 byte = (addr & 1) ? u8(word) : u8(word >> 8);
			if (byte != u8(needle[i]))
				match = false;
		}
		if (match)
		{
			logerror("ASR10_TASK1_STRING_SCAN tag=%s needle=\"%s\" address=%06x match=%u\n",
				tag, needle, cursor, matches);
			matches++;
		}
	}
	logerror("ASR10_TASK1_STRING_SCAN_DONE tag=%s needle=\"%s\" total_matches=%u range=%06x_%06x\n",
		tag, needle, matches, start, end);
}




















u16 asr10_boot_state::scsi_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	const u32 address = 0x00fc5000 | (offset << 1);

	u16 data = 0;
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
			return "status_control_candidate_no_scsi";
		if ((address & 0x1f) == 0x03)
			return "data_scratch_candidate_no_scsi";
		return "scsi_register_unknown";
	}
	return "register_unknown";
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


u16 asr10_boot_state::read_loaded_word(u32 address) const
{
	address &= 0x00ffffff;
	if (address < 0x00100000 && !m_maincpu->cs0_covers(0))
		return m_lowmem_shadow[(address >> 1) & (LOWMEM_WORDS - 1)];
	return read_code_word(address);
}


// read_code_word()/read_loaded_word() only resolve the ROM chip
// (<=ROM_MASK or the CPU's 0xf80000-0xfbffff alias window) and lowmem
// (<0x100000 via m_lowmem_shadow); anything else falls through to a 0xffff
// placeholder -- not because that memory is empty, but because those two
// helpers never learned about the *other* real backing store this driver's
// memory map installs: `map(0xfc6900, 0xffffff).ram();` in mem_map() is a
// plain, directly-backed RAM region covering every "high-view" address this
// investigation has hit (ffc85a, ff9106, ffa67e, ffb242, etc.). This function
// reads that region through the actual CPU address space (genuine bus read,
// side effects suppressed) instead of a host-side shadow array, since a
// plain .ram() region has no shadow to read and no side effects to suppress
// in the first place -- disable_side_effects() is kept only as a guard
// against a future map change extending a handler into this range.
u16 asr10_boot_state::read_highview_word(u32 address) const
{
	address &= 0x00ffffff;
	if (address >= 0x00fc6900)
	{
		auto const disable_side_effects = machine().disable_side_effects();
		return m_maincpu->space(AS_PROGRAM).read_word(address & ~u32(1));
	}
	return read_loaded_word(address);
}


u32 asr10_boot_state::read_loaded_long(u32 address) const
{
	return (u32(read_loaded_word(address)) << 16) | read_loaded_word(address + 2);
}

// hängning

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
			(void)0;
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
			(void)0;
			break;
		}
	}
	if (pc == 0x00fb9490 || pc == 0x00fb9494 || pc == 0x00fb9496 ||
		pc == 0x00fb94ae || pc == 0x00fb94b2 || pc == 0x00fb94b4 ||
		pc == 0x00fb94da || pc == 0x00fb94e0)
	{
		(void)0;
	}

	if (m_prompt_select_trace_mask & 0x80)
		m_prompt_select_timer->adjust(attotime::never);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::pc_poll)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_pc_poll_count++;
	if constexpr (ASR10_DIAG_PANEL_B)
	{
		if (pc == 0x00ffb286 || pc == 0x00ffb32e || pc == 0x00ffb3cc ||
			pc == 0x00ffb3e4 || pc == 0x00ffb424 || pc == 0x00f89aec)
			m_panel_b_last_parser_pc = pc;
	}
	const u32 d0 = u32(m_maincpu->state_int(M68K_D0));
	if (m_seen_loading_system_prompt && !m_error139_d0_candidate_logged && (d0 == 0x0000008b || d0 == 0xffffff8b))
	{
		const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
		const u16 sr = u16(m_maincpu->state_int(M68K_SR));
		logerror("ASR10_ERROR139_D0_CANDIDATE pc=%06x previous_pc=%06x opcode=%04x d0=%08x "
			"sr=%04x sr_mask=%u sp=%06x stack0=%08x stack1=%08x stack2=%08x "
			"fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x fc6850=%04x fc6852=%04x "
			"dispatcher_count=%u panel=\"%s\"\n",
			pc, m_last_distinct_pc, read_loaded_word(pc), d0,
			sr, (sr >> 8) & 7, sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
			m_m68302_internal_shadow[0x12 >> 1], m_m68302_internal_shadow[0x14 >> 1],
			m_m68302_internal_shadow[0x16 >> 1], m_m68302_internal_shadow[0x18 >> 1],
			m_m68302_internal_shadow[0x50 >> 1], m_m68302_internal_shadow[0x52 >> 1],
			m_runtime_dispatch_entry_count, m_panel_text);
		m_error139_d0_candidate_logged = true;
	}
	if (pc >= 0x00f87f40 && pc <= 0x00f87fd0 && pc != m_last_pc)
	{
		const char *semantic = "dispatcher_body";
		if (pc == 0x00f87f40)
			semantic = "dispatcher_context_save_entry";
		else if (pc == 0x00f87f92)
			semantic = "dispatcher_queue_scan_base_load";
		else if (pc == 0x00f87f96)
			semantic = "dispatcher_queue_scan_compare_byte2";
		else if (pc == 0x00f87fb0)
			semantic = "dispatcher_queue_clear_equalize";
		else if (pc == 0x00f87fc0)
			semantic = "dispatcher_rte";
		else if (pc == 0x00f87fd0)
			semantic = "dispatcher_scan_continue";
		m_runtime_dispatch_entry_count++;
		if (m_runtime_dispatch_entry_count <= 128 || !(m_runtime_dispatch_entry_count & (m_runtime_dispatch_entry_count - 1)))
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			logerror("ASR10_RUNTIME_DISPATCH pc=%06x previous_pc=%06x opcode=%04x semantic=%s "
				"last_rte_return_pc=%06x sr=%04x sr_mask=%u sp=%06x stack0=%08x stack1=%08x "
				"queue_base=%04x recent_queue_pc=%06x recent_queue_slot=%u recent_queue_current=%04x "
				"fc6814=%04x fc6816=%04x fc6818=%04x count=%u\n",
				pc, m_last_pc, read_loaded_word(pc), semantic, m_queue_rte_last_return_pc,
				u16(m_maincpu->state_int(M68K_SR)), (u16(m_maincpu->state_int(M68K_SR)) >> 8) & 7,
				sp, read_stack_long(sp), read_stack_long(sp + 4), m_lowmem_shadow[0x00c6 >> 1],
				m_recent_queue_pc, m_recent_queue_slot, m_recent_queue_current,
				m_m68302_internal_shadow[0x14 >> 1], m_m68302_internal_shadow[0x16 >> 1],
				m_m68302_internal_shadow[0x18 >> 1], m_runtime_dispatch_entry_count);
		}
	}
	(void)0;
	(void)0;
	(void)0;
	if (m_queue_rte_after_pending && pc != m_queue_rte_before_pc)
	{
		const u16 fc6814_after = m_m68302_internal_shadow[0x14 >> 1];
		const u16 fc6816_after = m_m68302_internal_shadow[0x16 >> 1];
		const u16 fc6818_after = m_m68302_internal_shadow[0x18 >> 1];
		logerror("ASR10_F87F96_QUEUE_RTE_AFTER rte_count=%u pc=%06x previous_rte_pc=%06x "
			"fc6814_before=%04x fc6814_after=%04x fc6814_changed=%04x "
			"fc6816_before=%04x fc6816_after=%04x fc6816_changed=%04x "
			"fc6818_before=%04x fc6818_after=%04x fc6818_changed=%04x "
			"fc6814_bits_3_1_0=%u%u%u fc6816_bits_15_14_13_10_7=%u%u%u%u%u "
			"last_fc68_pc=%06x last_fc68_addr=%06x last_fc68_rw=%c last_fc68_data=%04x last_fc68_shadow=%04x\n",
			m_queue_rte_after_count, pc, m_queue_rte_before_pc,
			m_queue_rte_before_fc6814, fc6814_after, m_queue_rte_before_fc6814 ^ fc6814_after,
			m_queue_rte_before_fc6816, fc6816_after, m_queue_rte_before_fc6816 ^ fc6816_after,
			m_queue_rte_before_fc6818, fc6818_after, m_queue_rte_before_fc6818 ^ fc6818_after,
			BIT(fc6814_after, 3), BIT(fc6814_after, 1), BIT(fc6814_after, 0),
			BIT(fc6816_after, 15), BIT(fc6816_after, 14), BIT(fc6816_after, 13),
			BIT(fc6816_after, 10), BIT(fc6816_after, 7),
			m_last_fc68_pc, m_last_fc68_address, m_last_fc68_write ? 'W' : 'R',
			m_last_fc68_data, m_last_fc68_shadow);
		m_queue_rte_after_pending = false;
	}

	if (pc != m_last_pc)
	{
		m_last_distinct_pc = m_last_pc;
		m_pc_repeat_count = 1;
		m_pc_change_count++;
		if (m_pc_change_count <= 256)
			logerror("ASR10PC pc=%06x previous_pc=%06x opcode=%04x\n", pc, m_last_distinct_pc, read_code_word(pc));
		else if (m_pc_change_count == 257)
			logerror("ASR10PC further transitions suppressed; final loop summary remains enabled\n");
		(void)0;
		(void)0;
		(void)0;
		(void)0;
		(void)0;
		m_last_pc = pc;
	}
	else
	{
		m_pc_repeat_count++;
	}

	// if (m_pc_poll_count > MAX_PC_POLLS)
	// {
	// 	log_pc_summary("max_poll_count", pc);
	// 	machine().schedule_exit();
	// }
	if (m_pc_poll_count > MAX_PC_POLLS)
	{
		(void)0;
		m_pc_timer->adjust(attotime::never);
		return;
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
	// fas 3 steg 1 (docs/asr10/PLAN.md): plumbing only -- BAR/SCR, Port B
	// PIO, the FC6860 busy register, and known/known-unimplemented/unknown
	// access classification. No interrupt controller, timer, IDMA, or
	// communications processor yet; see docs/mc68302/.
	MC68302(config, m_maincpu, XTAL(16'000'000));
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &asr10_boot_state::cpu_space_map);
	m_maincpu->set_instruction_execute_callback(FUNC(asr10_boot_state::maincpu_instruction_hook));

	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder for boot tracing
	m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w));

	// The uPD72069 sees this child connector as drive 0 via the conventional "fdc:0" tag.
	// Mounted HFE media changes Recalibrate/Sense from 68,00 (not ready) to 20,00.
	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);

	// U20, per docs/hardware-identity.md. X1 is derived, not a separate
	// crystal: the board has no 3.6864MHz part (Y1=16MHz, Y2=30.476MHz,
	// Y3=33.8688MHz), and ES5701's own reconstruction (sources/es5701.vhd)
	// divides the 16MHz system clock by two on-chip; docs/asr10/PLAN.md
	// section 3 hypothesizes a further /2 (spare 74HC74/74F74 flip-flops
	// on the board) yields 16/4 = 4.000MHz into X1, matching the 4MHz X1
	// this same SCN2681 runs at on esqkt.cpp/esq5505.cpp. Channel B is
	// wired to an ASR-10-specific esqpanel-derived device. The hand-written
	// taps in duart_panel_asr_candidate_r/w remain for ASR-10 boot/status
	// responses and diagnostics while the panel model is brought up. The IRQ
	// output, however, is a real pin on a real device --
	// docs/asr10/duart-imr.md found it genuinely pending (counter/timer
	// ready) 159/160 of the time and never wired to anything. Wired here
	// the same way esq5505.cpp wires its own SCN2681 (irq_cb() ->
	// set_inputline), replacing the old m_panel_c_isr/imr shadow that
	// could only ever see RX-ready, never counter/timer. Landed together
	// with mc68302_device::irq6_ack_vector() (docs/asr10/PLAN.md fas 3
	// steg 2, minimal slice) -- see docs/asr10/duart-irq6-wiring.md for
	// why the irq_cb wiring alone regresses the boot without it.
	SCN2681(config, m_duart, XTAL(16'000'000) / 4);
	m_duart->irq_cb().set_inputline(m_maincpu, 6);
	m_duart->b_tx_cb().set(m_panel, FUNC(asr10panel_device::rx_w));
	m_duart->outport_cb().set(FUNC(asr10_boot_state::duart_output));
	// set_clocks() maps to IP3/IP4/IP5/IP6. With CSRA/CSRB selector $E,
	// mc68681.cpp uses IP3/16 for channel A and IP5/16 for channel B.
	m_duart->set_clocks(500'000, 500'000, 1'000'000, 1'000'000);

	ASR10PANEL(config, m_panel);
	m_panel->write_tx().set(m_duart, FUNC(scn2681_device::rx_b_w));
	m_panel->write_analog().set(FUNC(asr10_boot_state::analog_w));

	// Phase 1 host-port fingerprint mapping. Not board-proven: see
	// docs/asr10/es5506-chain-verification.md.
	// Provisional/uncalibrated: no ASR-10-specific clock citation exists
	// for this chip in any driver; es550x_device::device_start() divides
	// by clock() to compute m_sample_rate, so a nonzero clock is required
	// simply to construct the device.
	es5506_device &es5506_host(ES5506(config, m_es5506_host, XTAL(16'000'000)));
	es5506_host.set_addrmap(0, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.set_addrmap(1, &asr10_boot_state::es5506_unpopulated_wavetable_map);
	es5506_host.set_addrmap(2, &asr10_boot_state::es5506_unpopulated_wavetable_map);
	es5506_host.set_addrmap(3, &asr10_boot_state::es5506_unpopulated_wavetable_map);

	es5506_host.read_port_cb().set(FUNC(asr10_boot_state::analog_r));

	// ES5510 host window (filesystem-browser-map.md 4.24):
	// instantiate a stock es5510_device purely as a host-interface
	// register bank for the FC3000-FC31FF select/commit protocol proven
	// in 4.22/4.23. set_disable() keeps it out of the scheduler's execute
	// list entirely -- the same idiom esqasr.cpp uses for this exact chip
	// on this exact board family (ES5510(config, m_esp, XTAL(10'000'000));
	// m_esp->set_disable();). This is deliberate, not a placeholder:
	// es5510_device::host_r()/host_w() (es5510.cpp) are pure register/
	// latch/gpr/instr-array state manipulation and do not call
	// execute_run() or otherwise depend on the device's own instruction
	// stream; memory_space_config() also returns an empty space_config_
	// vector, so no internal addrmap is required either. No IRQ wiring:
	// nothing in the proven upload/verify sequence (f973f0-f97776)
	// touches an ESP interrupt. Host-interface correctness is this
	// round's criterion, not audio output.
	// Provisional/uncalibrated clock: 10MHz matches the real
	// ASR-10/ESQ-1-family precedent (esqasr.cpp and esq5505.cpp both
	// use XTAL(10'000'000) / 10_MHz_XTAL for this exact chip); not
	// derived from ASR-10 schematics this round, and irrelevant to
	// host_r()/host_w() correctness since the device never executes.
	es5510_device &es5510_host(ES5510(config, m_es5510_host, XTAL(10'000'000)));
	es5510_host.set_disable();

}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


// CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", MACHINE_NO_SOUND)
CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", 0)
