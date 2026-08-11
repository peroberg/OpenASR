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

	// True forces the ROM into the SCSI-installed/searching path;
	// False lets the ROM fall through to "PLEASE INSERT DISK"
	static constexpr bool ASR10_FAKE_SCSI_INSTALLED = false;
	static constexpr u8 ASR10_DUART_INPUT_CHANGE_STUB = 0x00;
	static constexpr bool ASR10_EXPERIMENT_STUB_DUART_INPUT_CHANGE_BIT4_AT_FB7C84 = true;
	static constexpr bool ASR10_LOG_FDC_ACCESS = false;
	static constexpr bool ASR10_LOG_FDC_04B0_CONTEXT = false;
	static constexpr bool ASR10_LOG_PANEL_BYTES = false;
	static constexpr bool ASR10_EXPERIMENT_CMD88_RATE_500K = true;
	static constexpr bool ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B = false;
	static constexpr bool ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480 = false;
	static constexpr bool ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER = false;
	static constexpr bool ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ = false;
	static constexpr u8 ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ_LEVEL = 1;
	static constexpr bool ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR = false;
	static constexpr u8 ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL = 1;
	static constexpr u8 ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE = 0x40;
	static constexpr u16 ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK = 0x2400;
	static constexpr bool ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT = false;
	static constexpr bool ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR = false;
	static constexpr u32 ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP = 1024;
	static constexpr bool ASR10_EXPERIMENT_PANEL_REBOOT_CONFIRM_RAW_21 = false;
	static constexpr bool ASR10_EXPERIMENT_STUB_CMD1E_RESULTS = false;
	static constexpr u8 ASR10_STUB_CMD1E_RESULT_BYTE0 = 0x00;
	static constexpr u8 ASR10_STUB_CMD1E_RESULT_BYTE1 = 0x00;
	static constexpr bool ASR10_EXPERIMENT_STUB_CMD0E_RESULT = false;
	static constexpr u8 ASR10_STUB_CMD0E_RESULT_BYTE = 0x00;
	static constexpr bool ASR10_DIAG_PANEL_B = true;
	static constexpr u32 ASR10_DISPLAY_LENGTH = 22;
	static constexpr u32 ASR10_PANEL_DESCRIPTOR_TRACE_LIMIT = 64;
	using trace_region = asr10_boot_defs::trace_region;
	using trace_slot = asr10_boot_defs::trace_slot;
	enum class panel_byte_role : u8
	{
		SERIAL,
		RING_CONTROL,
		DIRECT_TEXT_PREFIX,
		TEXT_PAYLOAD
	};
	struct pc_profile_window
	{
		const char *name = nullptr;
		bool active = false;
		bool done = false;
		attotime start = attotime::never;
		attotime end = attotime::never;
		u64 instructions = 0;
		u64 samples = 0;
		u32 stop_samples = 0;
		u32 fdc_reads = 0;
		u32 fdc_writes = 0;
		std::unordered_map<u32, u32> pc_counts;
		std::array<u32, 64> recent_pcs{};
		u32 recent_pos = 0;
	};
	enum class step0_region : u8
	{
		ROM,
		DPRAM,
		PERIPHERAL,
		LOW_RAM,
		HIGH_RAM,
		OTHER,
		COUNT
	};
	struct region_handoff_sample
	{
		u32 from = 0;
		u32 to = 0;
		attotime first_time = attotime::never;
	};
	struct rx_event_slot_activity
	{
		u32 ready_samples = 0;
		u32 writes = 0;
		u16 last_previous = 0;
		u16 last_current = 0;
		u16 last_mem_mask = 0;
		u32 last_write_pc = 0xffffffffU;
	};
	static u16 ascii_to_14seg(u8 character) { return asr10_boot_defs::ascii_to_14seg(character); }

	required_device<mc68302_device> m_maincpu;
	required_device<upd72069_device> m_fdc;
	required_device<floppy_connector> m_floppy_connector;
	required_device<scn2681_device> m_duart;
	required_memory_region m_rom;

	optional_device<es5506_device> m_es5506_host;
	optional_device<es5510_device> m_es5510_host;

	output_finder<ASR10_DISPLAY_LENGTH> m_display;
	std::array<u8, ASR10_DISPLAY_LENGTH> m_display_chars{};
	u8 m_display_position = 0;

	emu_timer *m_pc_timer = nullptr;
	emu_timer *m_prompt_select_timer = nullptr;
	emu_timer *m_synth_68302_timer_irq_timer = nullptr;
	emu_timer *m_panel_autorespond_timer = nullptr;
	emu_timer *m_panel_sweep_timer = nullptr;
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
	bool m_panel_submission_trace_enabled = false;
	u32 m_panel_diag_ring_control_role_count = 0;
	u32 m_panel_diag_direct_text_begin_count = 0;
	u32 m_panel_diag_direct_text_end_count = 0;
	static constexpr u8 PANEL_DESCRIPTOR_STACK_LIMIT = 16;
	std::array<u32, PANEL_DESCRIPTOR_STACK_LIMIT> m_panel_descriptor_stack_raw_a2{};
	std::array<u32, PANEL_DESCRIPTOR_STACK_LIMIT> m_panel_descriptor_stack_identity{};
	std::array<u32, PANEL_DESCRIPTOR_STACK_LIMIT> m_panel_descriptor_stack_entry_pc{};
	u8 m_panel_descriptor_stack_depth = 0;
	u8 m_panel_descriptor_stack_overflow_depth = 0;
	u8 m_panel_descriptor_max_depth = 0;
	u32 m_panel_diag_descriptor_entry_count = 0;
	u32 m_panel_diag_descriptor_return_count = 0;
	u32 m_panel_diag_descriptor_outer_entry_count = 0;
	u32 m_panel_diag_descriptor_outer_return_count = 0;
	u32 m_panel_diag_descriptor_nested_entry_count = 0;
	u32 m_panel_diag_descriptor_nested_return_count = 0;
	u32 m_panel_diag_descriptor_unmatched_return_count = 0;
	u32 m_panel_diag_descriptor_stack_overflow_count = 0;
	u32 m_panel_diag_path_a_begin_count = 0;
	u32 m_panel_diag_path_a_end_count = 0;
	u32 m_panel_diag_path_a_identity_match_count = 0;
	u32 m_panel_diag_path_a_identity_mismatch_count = 0;
	u32 m_panel_diag_descriptor_trace_count = 0;
	bool m_root_directory_trace_enabled = false;
	bool m_root_directory_no_inst_seen = false;
	u32 m_root_directory_direct_text_count = 0;
	u32 m_root_directory_table_first_word_write_count = 0;
	u16 m_root_directory_first_zero_index = 0xffff;
	u16 m_root_directory_nonzero_first_word_count = 0;
	static constexpr u32 ROOT_DIRECTORY_HISTORY_LIMIT = 64;
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_pc{};
	std::array<u16, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_opcode{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_a0{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_a2{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_d0{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_d1{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_history_sp{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_pc{};
	std::array<u16, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_opcode{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_previous{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_current{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_d0{};
	std::array<u32, ROOT_DIRECTORY_HISTORY_LIMIT> m_root_directory_a2_change_a0{};
	u32 m_root_directory_last_a2 = 0xffffffff;
	u32 m_root_directory_a2_change_pos = 0;
	u32 m_root_directory_a2_change_count = 0;
	u32 m_root_directory_history_pos = 0;
	u32 m_root_directory_history_count = 0;
	u32 m_root_directory_16c4_entry_count = 0;
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
	bool m_seen_error_reboot_prompt = false;
	bool m_panel_reboot_confirm_injected = false;
	bool m_error009_origin_logged = false;
	bool m_error032_origin_logged = false;
	u32 m_lrclk_trace_count = 0;
	u32 m_fc6829_trace_count = 0;
	bool m_gpio_stage1_trace_enabled = false;
	u32 m_gpio_stage1_trace_count = 0;
	bool m_gpio_stage1_gate_pass_logged = false;
	bool m_task1_string_scan_logged = false;
	bool m_download_trace_enabled = false;
	bool m_download_retry_loop_dump_logged = false;
	u32 m_esp_first_pass_write_seq = 0;
	bool m_fdc_synth_tc_enabled = false;
	bool m_fdc_synth_tc_pulsed_this_txn = false;
	bool m_disk_sig_trace_enabled = false;
	bool m_fc3000_verify_trace_enabled = false;
	bool m_fc3000_verify_captured = false;
	struct fc3000_ring_entry
	{
		u32 pc = 0;
		u32 address = 0;
		u16 data = 0;
		u16 mem_mask = 0;
		bool write = false;
	};
	std::array<fc3000_ring_entry, 10> m_fc3000_verify_ring{};
	u32 m_fc3000_verify_ring_pos = 0;
	bool m_gpio_stage1_gate_fail_logged = false;
	u32 m_post_lrclk_poll_count = 0;
	bool m_post_lrclk_disassembly_logged = false;
	u32 m_f87f96_queue_read_count = 0;
	u32 m_f87f96_queue_write_count = 0;
	u32 m_f87f96_queue_rte_count = 0;
	bool m_f87f96_code_dump_logged = false;
	bool m_f880_queue_code_dump_logged = false;
	bool m_f8ce_queue_code_dump_logged = false;
	bool m_queue_rte_after_pending = false;
	bool m_dispatcher_rte_first_pc_pending = false;
	// TUNING KBD stall investigation: one-shot code dumps for the loaded-runtime
	// callback PCs observed in the final RTE burst (slots 1/3/0/4/5) immediately
	// before the dispatcher goes idle forever. These are runtime-loaded (not ROM)
	// so they cannot be read from a static ROM disassembly. Gated as a group
	// behind ASR10_EXPERIMENT_TUNING_STALL_TRACE -- supporting/scheduler-shape
	// diagnostics only; the load-bearing findings for this investigation come
	// from the pre-existing Channel B/FDC hooks, not from these.
	bool m_tuning_stall_trace_enabled = false;
	// Item 6/7 follow-through (filesystem-browser-map.md PASS 2): resolve
	// slot 0's six jump-vector targets, slot 4's five high-view targets,
	// f894a4's own internal vector calls, and slots 1/3's own resume code,
	// all via read_highview_word()/dump_highview_code_range() instead of
	// the 0xffff placeholder path. One-shot state for these is table-driven
	// -- see FSB_DUMP_TARGETS in the .cpp -- rather than one bool per
	// target. Gated on m_fsb.enabled, not m_tuning_stall_trace_enabled,
	// since this is this task's own instrumentation.
	u32 m_tuning_stall_trap7_count = 0;
	u32 m_tuning_stall_trap8_count = 0;
	bool m_dispatcher_rte_iack_seen = false;
	u32 m_queue_rte_after_count = 0;
	u32 m_queue_rte_before_pc = 0xffffffff;
	u32 m_queue_rte_last_return_pc = 0xffffffff;
	u32 m_dispatcher_rte_frame_pc = 0xffffffff;
	u16 m_dispatcher_rte_frame_sr = 0;
	u8 m_dispatcher_rte_iack_vector = 0xff;
	u8 m_dispatcher_rte_iack_level = 0xff;
	u32 m_dispatcher_rte_iack_pc = 0xffffffff;
	u16 m_dispatcher_rte_iack_sr = 0;

	u32 m_runtime_dispatch_entry_count = 0;
	u32 m_timer_candidate_trace_count = 0;
	u32 m_synth_68302_timer_irq_count = 0;
	u32 m_iack_trace_count = 0;
	u32 m_synth_68302_timer_iack_delay_count = 0;
	u32 m_synth_68302_timer_iack_fire_count = 0;
	u32 m_synth_68302_timer_iack_last_fire_callback = 0;
	u32 m_synth_68302_timer_iack_skip_count = 0;
	bool m_synth_68302_timer_irq_vector_dump_logged = false;
	bool m_synth_68302_timer_irq_code_dump_logged = false;
	bool m_synth_68302_timer_iack_runtime_vector_dump_logged = false;
	bool m_synth_68302_timer_iack_armed_logged = false;
	bool m_synth_68302_timer_iack_fired_logged = false;
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
	bool m_fc6816_service_2400_clear_experiment_done = false;
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
	u32 m_panel_autorespond_scheduled_count = 0;
	u32 m_panel_autorespond_injected_count = 0;
	bool m_panel_sweep_enabled = false;
	bool m_panel_sweep_all_enabled = false;
	bool m_panel_sweep_armed = false;
	bool m_panel_sweep_injected = false;
	bool m_panel_sweep_waiting_sample = false;
	bool m_panel_sweep_consumed = false;
	u8 m_panel_sweep_raw = 0;
	u8 m_panel_sweep_consumed_value = 0;
	u16 m_panel_sweep_current = 0;
	u16 m_panel_sweep_end = 0xff;
	u32 m_panel_sweep_dispatch_target = 0xffffffffU;
	char m_panel_sweep_before[PANEL_TEXT_LENGTH]{};
	bool m_panel_b_conversation_enabled = false;
	bool m_panel_b_conversation_done = false;
	u32 m_panel_b_conversation_seq = 0;
	u32 m_panel_b_conversation_thrb = 0;
	u32 m_panel_b_conversation_rhrb = 0;
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
	u32 m_esp_010722_window_write_count = 0;
	// filesystem-browser-map.md 4.26 TASK 6 (observation-only): bounded
	// per-attempt trace of the HALL REVERB (table base $0e8e==0x00010400)
	// type-1/record-0 GPR transaction, scoped narrowly per instruction.
	u32 m_hall_reverb_trace_count = 0;
	u32 m_es5506_diag_par_read_count = 0;
	bool m_primary_slot_snapshot_logged = false;
	bool m_panel_c_parser_trace_enabled = false;
	bool m_panel_c_parser_trace_active = false;
	bool m_panel_c_parser_trace_done = false;
	u32 m_panel_c_parser_trace_count = 0;
	u32 m_panel_c_parser_trace_last_pc = 0xffffffffU;
	u32 m_last_pc = 0xffffffffU;
	u32 m_last_distinct_pc = 0xffffffffU;
	u32 m_pc_repeat_count = 0;
	u32 m_pc_change_count = 0;
	u32 m_dispatcher_hits = 0;
	u32 m_context_hits[20]{};
	bool m_step0_runtime_trace_enabled = false;
	std::array<pc_profile_window, 2> m_step0_pc_profiles{};
	bool m_step0_file1_context_logged = false;
	bool m_step0_irq6_pending_landing = false;
	u8 m_step0_irq6_pending_vector = 0;
	u32 m_step0_irq6_pending_target = 0xffffffffU;
	u32 m_step0_irq6_pending_iack_pc = 0xffffffffU;
	std::array<u32, 256> m_step0_irq6_isr_hist{};
	std::array<u32, 8> m_step0_irq6_isr_bit_hist{};
	std::array<u32, 8> m_step0_irq6_masked_bit_hist{};
	u32 m_step0_irq6_duart_pending_count = 0;
	u32 m_step0_irq6_non_duart_count = 0;
	u32 m_step0_irq6_accept_count = 0;
	u8 m_step0_duart_imr = 0;
	u32 m_step0_thra_writes = 0;
	u32 m_step0_thrb_writes = 0;
	std::unordered_map<u32, u32> m_step0_thra_write_pcs;
	static constexpr u32 STEP0_REGION_COUNT = u32(step0_region::COUNT);
	static constexpr u32 STEP0_HANDOFF_SAMPLES_PER_CLASS = 16;
	std::array<std::array<region_handoff_sample, STEP0_HANDOFF_SAMPLES_PER_CLASS>, STEP0_REGION_COUNT * STEP0_REGION_COUNT> m_step0_region_handoffs{};
	std::array<u32, STEP0_REGION_COUNT * STEP0_REGION_COUNT> m_step0_region_handoff_counts{};
	std::array<u32, STEP0_REGION_COUNT * STEP0_REGION_COUNT> m_step0_region_handoff_truncated{};
	bool m_rx_event_trace_enabled = false;
	bool m_rx_event_lowmem_dumped = false;
	bool m_rx_event_pre_inject_dumped = false;
	bool m_rx_event_after_reported = false;
	u32 m_rx_event_id = 0;
	u16 m_rx_event_slot_base = 0;
	u16 m_rx_event_slot_end = 0;
	u32 m_rx_event_slot_count = 0;
	std::array<rx_event_slot_activity, 128> m_rx_event_slot_activity{};

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
	TIMER_CALLBACK_MEMBER(synth_68302_timer_irq);
	TIMER_CALLBACK_MEMBER(panel_autorespond_fire);
	TIMER_CALLBACK_MEMBER(panel_sweep_fire);
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
	void note_panel_descriptor_entry(u32 pc);
	void note_panel_descriptor_return(u32 pc);
	static u32 normalize_panel_descriptor_identity(u32 address);
	void panel_descriptor_trace(const char *event, u32 pc, u32 raw_a2, u32 identity, u8 depth_before, u8 depth_after, const char *classification);
	void panel_submission_trace(const char *event, const char *kind, u8 data = 0);
	void panel_submission_summary();
	void panel_receive_live_summary();
	void start_pc_profile(pc_profile_window &window, const char *name);
	void sample_pc_profile(pc_profile_window &window, u32 pc);
	void pc_profile_summary();
	bool normalized_rom_handoff_source(u32 pc, u32 &normalized) const;
	static step0_region classify_step0_region(u32 pc);
	static const char *step0_region_name(step0_region region);
	void record_rom_handoff(u32 from, u32 to);
	void rom_handoff_summary();
	void note_step0_fdc_access(bool write);
	void step0_irq6_summary();
	void rx_event_dump_lowmem();
	void rx_event_dump_slots(const char *phase);
	void rx_event_note_slot_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask);
	void rx_event_note_instruction(u32 pc);
	void rx_event_summary();
	void record_root_directory_instruction(u32 pc);

	void log_root_directory_table_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask);
	void root_directory_summary();
	void mc68302_access_summary();

















	bool fc3000_verify_table_match() const;
	// filesystem-browser-map.md 4.25 (observation-only): identifying the
	// retry-exhaustion object at a3=~0x010722, distinct from the
	// already-fixed fff9bca0 table. No fix, no new behavior -- logging
	// only.




	// filesystem-browser-map.md 4.26 TASK 6 (observation-only): bounded
	// per-attempt HALL REVERB (table $0e8e==0x00010400) type-1/record-0
	// GPR transaction trace.
	bool hall_reverb_type1_record0_active() const;









	void scan_for_ascii_string(const char *tag, u32 start, u32 end, const char *needle);
	u16 read_highview_word(u32 address) const;
































































	std::string dump_cpu_registers() const;
	u16 es5506_host_read_par_diag();
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






	void panel_c_parser_trace_stop(const char *reason, u32 pc);
	void panel_c_queue_rx(u8 data, const char *reason, u32 pc);
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
	m_pc_timer = timer_alloc(FUNC(asr10_boot_state::pc_poll), this);
	m_prompt_select_timer = timer_alloc(FUNC(asr10_boot_state::prompt_select_poll), this);
	m_synth_68302_timer_irq_timer = timer_alloc(FUNC(asr10_boot_state::synth_68302_timer_irq), this);
	m_panel_autorespond_timer = timer_alloc(FUNC(asr10_boot_state::panel_autorespond_fire), this);
	m_panel_sweep_timer = timer_alloc(FUNC(asr10_boot_state::panel_sweep_fire), this);
	m_lrclk_timer = timer_alloc(FUNC(asr10_boot_state::lrclk_toggle), this);
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::panel_submission_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::panel_receive_live_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::pc_profile_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::rom_handoff_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::step0_irq6_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::rx_event_summary, this));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::root_directory_summary, this));
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
			if (m_fc3000_verify_trace_enabled)
			{
				// `offset` is already the absolute, even-aligned bus address (the
				// tap/even bus address) -- not relative to the tap's install range,
				// and must never be re-added to 0x00fc3000. The selected CPU byte
				// address is the specific byte the 68000 program addressed: only
				// meaningful for a byte-wide mem_mask (00ff/ff00), not a full word.
				// See docs/asr10/evidence-tree.md for the full coordinate-system note.
				const u32 selected_cpu_byte_address = offset + ((mem_mask & 0x00ff) ? 1u : 0u);
				m_fc3000_verify_ring[m_fc3000_verify_ring_pos % m_fc3000_verify_ring.size()] =
					fc3000_ring_entry{pc, selected_cpu_byte_address, data, mem_mask, false};
				m_fc3000_verify_ring_pos++;
				(void)0;
				// filesystem-browser-map.md 4.26 TASK 1 (observation-only):
				// this is the reliable path for the f97574 compare IF the
				// other-table object's A6 also lands in FC3000-FC31FF (i.e.
				// it is ES5510-related). This tap is a genuine data-read
				// dispatch, proven reliable for the fixed-table case above.
				// If the other object's compare does NOT read from this
				// range, this will not fire for it either -- documented as
				// a limitation in the FINAL REPORT. data/mem_mask are the
				// actual bus-read values from this callback, not reread
				// via read_program_word -- log_esp_other_table_verify
				// itself checks observed against D2 and only logs/consumes
				// its one-shot on a genuine mismatch.
				if (pc == 0x00f97574 && !fc3000_verify_table_match())
					(void)0;
				if (pc == 0x00f97574 && hall_reverb_type1_record0_active())
					(void)0;
			}
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
			if (m_fc3000_verify_trace_enabled)
			{
				// See the read tap above for the tap/even-bus-address vs
				// selected-CPU-byte-address distinction; same formula applies here.
				const u32 selected_cpu_byte_address = offset + ((mem_mask & 0x00ff) ? 1u : 0u);
				m_fc3000_verify_ring[m_fc3000_verify_ring_pos % m_fc3000_verify_ring.size()] =
					fc3000_ring_entry{pc, selected_cpu_byte_address, data, mem_mask, true};
				m_fc3000_verify_ring_pos++;
				// filesystem-browser-map.md 4.23 TASK 1: the first-pass
				// upload write ("f97432: move.b (A3)+,(A6)") for the same
				// 0xfff9bca0-tagged object the verify pass (4.22) later
				// mismatches on. A3 has already been post-incremented by
				// the time this tap fires, so the source ROM byte's
				// address is (A3-1).
				if (pc == 0x00f97432 && fc3000_verify_table_match())
					(void)0;
				// filesystem-browser-map.md 4.24 TASK 7: the select/commit
				// writes (f97776's "move.b D1,(A0,D4.w)") for the two
				// record indices (0 and 58) whose collision this round's
				// ES5510 integration targets. Bounded to those two indices
				// and to the three known select/commit byte addresses so
				// this does not add per-retry log volume across the run.
				if (fc3000_verify_table_match() &&
					(selected_cpu_byte_address == 0x00fc3101 ||
						selected_cpu_byte_address == 0x00fc3141 ||
						selected_cpu_byte_address == 0x00fc3181) &&
					(u8(data) == 0 || u8(data) == 58))
					(void)0;
				// filesystem-browser-map.md 4.26 TASK 6: HALL REVERB
				// (table $0e8e==0x00010400) type-1/record-0 GPR
				// transaction trace -- latch writes (offsets 0x00-0x02,
				// FC3001/FC3003/FC3005) and the write-select-GPR (0xa0,
				// FC3141) / read-select (0x80, FC3101) commits.
				if (hall_reverb_type1_record0_active() &&
					(selected_cpu_byte_address == 0x00fc3001 ||
						selected_cpu_byte_address == 0x00fc3003 ||
						selected_cpu_byte_address == 0x00fc3005 ||
						selected_cpu_byte_address == 0x00fc3007 ||
						selected_cpu_byte_address == 0x00fc3009 ||
						selected_cpu_byte_address == 0x00fc300b ||
						selected_cpu_byte_address == 0x00fc300d ||
						selected_cpu_byte_address == 0x00fc300f ||
						selected_cpu_byte_address == 0x00fc3011 ||
						selected_cpu_byte_address == 0x00fc3101 ||
						selected_cpu_byte_address == 0x00fc3141 ||
						selected_cpu_byte_address == 0x00fc31c1))
				{
					(void)0;
				}
			}
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
	save_item(NAME(m_panel_diag_ring_control_role_count));
	save_item(NAME(m_panel_diag_direct_text_begin_count));
	save_item(NAME(m_panel_diag_direct_text_end_count));
	save_item(NAME(m_panel_descriptor_stack_raw_a2));
	save_item(NAME(m_panel_descriptor_stack_identity));
	save_item(NAME(m_panel_descriptor_stack_entry_pc));
	save_item(NAME(m_panel_descriptor_stack_depth));
	save_item(NAME(m_panel_descriptor_stack_overflow_depth));
	save_item(NAME(m_panel_descriptor_max_depth));
	save_item(NAME(m_panel_diag_descriptor_entry_count));
	save_item(NAME(m_panel_diag_descriptor_return_count));
	save_item(NAME(m_panel_diag_descriptor_outer_entry_count));
	save_item(NAME(m_panel_diag_descriptor_outer_return_count));
	save_item(NAME(m_panel_diag_descriptor_nested_entry_count));
	save_item(NAME(m_panel_diag_descriptor_nested_return_count));
	save_item(NAME(m_panel_diag_descriptor_unmatched_return_count));
	save_item(NAME(m_panel_diag_descriptor_stack_overflow_count));
	save_item(NAME(m_panel_diag_path_a_begin_count));
	save_item(NAME(m_panel_diag_path_a_end_count));
	save_item(NAME(m_panel_diag_path_a_identity_match_count));
	save_item(NAME(m_panel_diag_path_a_identity_mismatch_count));
	save_item(NAME(m_panel_diag_descriptor_trace_count));
	save_item(NAME(m_root_directory_no_inst_seen));
	save_item(NAME(m_root_directory_direct_text_count));
	save_item(NAME(m_root_directory_table_first_word_write_count));
	save_item(NAME(m_root_directory_first_zero_index));
	save_item(NAME(m_root_directory_nonzero_first_word_count));
	save_item(NAME(m_root_directory_history_pc));
	save_item(NAME(m_root_directory_history_opcode));
	save_item(NAME(m_root_directory_history_a0));
	save_item(NAME(m_root_directory_history_a2));
	save_item(NAME(m_root_directory_history_d0));
	save_item(NAME(m_root_directory_history_d1));
	save_item(NAME(m_root_directory_history_sp));
	save_item(NAME(m_root_directory_a2_change_pc));
	save_item(NAME(m_root_directory_a2_change_opcode));
	save_item(NAME(m_root_directory_a2_change_previous));
	save_item(NAME(m_root_directory_a2_change_current));
	save_item(NAME(m_root_directory_a2_change_d0));
	save_item(NAME(m_root_directory_a2_change_a0));
	save_item(NAME(m_root_directory_last_a2));
	save_item(NAME(m_root_directory_a2_change_pos));
	save_item(NAME(m_root_directory_a2_change_count));
	save_item(NAME(m_root_directory_history_pos));
	save_item(NAME(m_root_directory_history_count));
	save_item(NAME(m_root_directory_16c4_entry_count));
	save_item(NAME(m_panel_msg_first_pc));
	save_item(NAME(m_panel_msg_last_pc));
	save_item(NAME(m_seen_loading_system_prompt));
	save_item(NAME(m_post_loading_panel_write_count));
	save_item(NAME(m_post_loading_fdc_access_count));
	save_item(NAME(m_insert_disk_decision_logged));
	save_item(NAME(m_seen_error_reboot_prompt));
	save_item(NAME(m_panel_reboot_confirm_injected));
	save_item(NAME(m_error009_origin_logged));
	save_item(NAME(m_error032_origin_logged));
	save_item(NAME(m_lrclk_trace_count));
	save_item(NAME(m_fc6829_trace_count));
	save_item(NAME(m_gpio_stage1_trace_enabled));
	save_item(NAME(m_gpio_stage1_trace_count));
	save_item(NAME(m_gpio_stage1_gate_pass_logged));
	save_item(NAME(m_gpio_stage1_gate_fail_logged));
	save_item(NAME(m_task1_string_scan_logged));
	save_item(NAME(m_download_trace_enabled));
	save_item(NAME(m_fdc_synth_tc_enabled));
	save_item(NAME(m_fdc_synth_tc_pulsed_this_txn));
	save_item(NAME(m_disk_sig_trace_enabled));
	save_item(NAME(m_fc3000_verify_trace_enabled));
	save_item(NAME(m_fc3000_verify_captured));
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
	save_item(NAME(m_synth_68302_timer_irq_count));
	save_item(NAME(m_iack_trace_count));
	save_item(NAME(m_synth_68302_timer_iack_delay_count));
	save_item(NAME(m_synth_68302_timer_iack_fire_count));
	save_item(NAME(m_synth_68302_timer_iack_last_fire_callback));
	save_item(NAME(m_synth_68302_timer_iack_skip_count));
	save_item(NAME(m_synth_68302_timer_irq_vector_dump_logged));
	save_item(NAME(m_synth_68302_timer_irq_code_dump_logged));
	save_item(NAME(m_synth_68302_timer_iack_runtime_vector_dump_logged));
	save_item(NAME(m_synth_68302_timer_iack_armed_logged));
	save_item(NAME(m_synth_68302_timer_iack_fired_logged));
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
	save_item(NAME(m_fc6816_service_2400_clear_experiment_done));
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
	save_item(NAME(m_panel_autorespond_scheduled_count));
	save_item(NAME(m_panel_autorespond_injected_count));
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
	save_item(NAME(m_es5506_diag_par_read_count));
	save_item(NAME(m_primary_slot_snapshot_logged));
	save_item(NAME(m_fc60b0_verified));
	save_item(NAME(m_fc2d40_cluster_count));
	save_item(NAME(m_fc3000_cluster_count));
	save_item(NAME(m_panel_c_parser_trace_enabled));
	save_item(NAME(m_panel_c_parser_trace_active));
	save_item(NAME(m_panel_c_parser_trace_done));
	save_item(NAME(m_panel_c_parser_trace_count));
	save_item(NAME(m_panel_c_parser_trace_last_pc));
	save_item(NAME(m_last_pc));
	save_item(NAME(m_last_distinct_pc));
	save_item(NAME(m_pc_repeat_count));
	save_item(NAME(m_pc_change_count));
	save_item(NAME(m_dispatcher_hits));
	save_item(NAME(m_context_hits));
}


void asr10_boot_state::machine_reset()
{
	if (m_panel_submission_trace_enabled && m_panel_descriptor_stack_depth)
		logerror("ASR10_PANEL_DESCRIPTOR event=reset_nonempty_stack depth=%u overflow_depth=%u\n",
			m_panel_descriptor_stack_depth, m_panel_descriptor_stack_overflow_depth);

	m_seen_insert_disk_prompt = false;

	m_panel_text_length = 0;
	m_panel_transport_pending_marker = 0;
	m_panel_receive_role = u8(panel_byte_role::SERIAL);
	m_panel_direct_text_active = false;
	m_panel_diag_ring_control_role_count = 0;
	m_panel_diag_direct_text_begin_count = 0;
	m_panel_diag_direct_text_end_count = 0;
	m_panel_descriptor_stack_raw_a2 = {};
	m_panel_descriptor_stack_identity = {};
	m_panel_descriptor_stack_entry_pc = {};
	m_panel_descriptor_stack_depth = 0;
	m_panel_descriptor_stack_overflow_depth = 0;
	m_panel_descriptor_max_depth = 0;
	m_panel_diag_descriptor_entry_count = 0;
	m_panel_diag_descriptor_return_count = 0;
	m_panel_diag_descriptor_outer_entry_count = 0;
	m_panel_diag_descriptor_outer_return_count = 0;
	m_panel_diag_descriptor_nested_entry_count = 0;
	m_panel_diag_descriptor_nested_return_count = 0;
	m_panel_diag_descriptor_unmatched_return_count = 0;
	m_panel_diag_descriptor_stack_overflow_count = 0;
	m_panel_diag_path_a_begin_count = 0;
	m_panel_diag_path_a_end_count = 0;
	m_panel_diag_path_a_identity_match_count = 0;
	m_panel_diag_path_a_identity_mismatch_count = 0;
	m_panel_diag_descriptor_trace_count = 0;
	m_root_directory_no_inst_seen = false;
	m_root_directory_direct_text_count = 0;
	m_root_directory_table_first_word_write_count = 0;
	m_root_directory_first_zero_index = 0xffff;
	m_root_directory_nonzero_first_word_count = 0;
	m_root_directory_history_pc = {};
	m_root_directory_history_opcode = {};
	m_root_directory_history_a0 = {};
	m_root_directory_history_a2 = {};
	m_root_directory_history_d0 = {};
	m_root_directory_history_d1 = {};
	m_root_directory_history_sp = {};
	m_root_directory_a2_change_pc = {};
	m_root_directory_a2_change_opcode = {};
	m_root_directory_a2_change_previous = {};
	m_root_directory_a2_change_current = {};
	m_root_directory_a2_change_d0 = {};
	m_root_directory_a2_change_a0 = {};
	m_root_directory_last_a2 = 0xffffffff;
	m_root_directory_a2_change_pos = 0;
	m_root_directory_a2_change_count = 0;
	m_root_directory_history_pos = 0;
	m_root_directory_history_count = 0;
	m_root_directory_16c4_entry_count = 0;
	{
		const char *const panel_submission_trace = std::getenv("ASR10_DIAG_PANEL_SUBMISSIONS");
		m_panel_submission_trace_enabled =
			panel_submission_trace && panel_submission_trace[0] && panel_submission_trace[0] != '0';
	}
	{
		const char *const root_directory_trace = std::getenv("ASR10_DIAG_ROOT_DIRECTORY");
		m_root_directory_trace_enabled =
			root_directory_trace && root_directory_trace[0] && root_directory_trace[0] != '0';
	}
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
	m_seen_error_reboot_prompt = false;
	m_panel_reboot_confirm_injected = false;
	m_error009_origin_logged = false;
	m_error032_origin_logged = false;
	m_lrclk_trace_count = 0;
	m_fc6829_trace_count = 0;
	{
		const char *const gpio_stage1_trace = std::getenv("ASR10_EXPERIMENT_MC68302_GPIO_TRACE");
		m_gpio_stage1_trace_enabled =
			gpio_stage1_trace && gpio_stage1_trace[0] && gpio_stage1_trace[0] != '0';
	}
	m_gpio_stage1_trace_count = 0;
	m_gpio_stage1_gate_pass_logged = false;
	m_gpio_stage1_gate_fail_logged = false;
	m_task1_string_scan_logged = false;
	{
		const char *const download_trace = std::getenv("ASR10_EXPERIMENT_DOWNLOAD_TRACE");
		m_download_trace_enabled = download_trace && download_trace[0] && download_trace[0] != '0';
	}
	m_download_retry_loop_dump_logged = false;
	m_esp_first_pass_write_seq = 0;
	m_esp_select_commit_log_count = 0;
	m_esp_f973f0_entry_log_count = 0;
	m_esp_other_table_first_retry_captured = false;
	m_esp_other_table_verify_captured = false;
	m_esp_010722_window_write_count = 0;
	m_hall_reverb_trace_count = 0;
	{
		// Diagnostic fallback ONLY: no guest memory-mapped access has been
		// proven to be a real TC strobe, and the FDC transfer loop
		// (fb8aa2-fb8abe/fb8d78) is confirmed programmed I/O with no
		// MC68302 DMA involvement -- see docs/asr10/evidence-tree.md.
		// This pulses tc_w() purely from the HOST'S OWN fifo_r() byte
		// count reaching the expected sector size; it is not a claim
		// about real ASR-10 hardware wiring.
		const char *const synth_tc = std::getenv("ASR10_EXPERIMENT_FDC_SYNTH_TC");
		m_fdc_synth_tc_enabled = synth_tc && synth_tc[0] && synth_tc[0] != '0';
	}
	m_fdc_synth_tc_pulsed_this_txn = false;
	{
		const char *const disk_sig_trace = std::getenv("ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE");
		m_disk_sig_trace_enabled = disk_sig_trace && disk_sig_trace[0] && disk_sig_trace[0] != '0';
	}
	{
		const char *const fc3000_verify_trace = std::getenv("ASR10_EXPERIMENT_FC3000_VERIFY_TRACE");
		m_fc3000_verify_trace_enabled = fc3000_verify_trace && fc3000_verify_trace[0] && fc3000_verify_trace[0] != '0';
	}
	m_fc3000_verify_captured = false;
	m_fc3000_verify_ring.fill(fc3000_ring_entry{});
	m_fc3000_verify_ring_pos = 0;
	{
		const char *const tuning_stall_trace = std::getenv("ASR10_EXPERIMENT_TUNING_STALL_TRACE");
		m_tuning_stall_trace_enabled = tuning_stall_trace && tuning_stall_trace[0] && tuning_stall_trace[0] != '0';
	}
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
	m_synth_68302_timer_irq_count = 0;
	m_iack_trace_count = 0;
	m_synth_68302_timer_iack_delay_count = 0;
	m_synth_68302_timer_iack_fire_count = 0;
	m_synth_68302_timer_iack_last_fire_callback = 0;
	m_synth_68302_timer_iack_skip_count = 0;
	m_synth_68302_timer_irq_vector_dump_logged = false;
	m_synth_68302_timer_irq_code_dump_logged = false;
	m_synth_68302_timer_iack_runtime_vector_dump_logged = false;
	m_synth_68302_timer_iack_armed_logged = false;
	m_synth_68302_timer_iack_fired_logged = false;
	m_error139_d0_candidate_logged = false;
	m_synth_68302_timer_irq_timer->adjust(attotime::never);
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
	m_fc6816_service_2400_clear_experiment_done = false;
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
	m_panel_autorespond_scheduled_count = 0;
	m_panel_autorespond_injected_count = 0;
	m_panel_autorespond_timer->adjust(attotime::never);
	m_panel_sweep_enabled = false;
	m_panel_sweep_all_enabled = false;
	m_panel_sweep_armed = false;
	m_panel_sweep_injected = false;
	m_panel_sweep_waiting_sample = false;
	m_panel_sweep_consumed = false;
	m_panel_sweep_raw = 0;
	m_panel_sweep_consumed_value = 0;
	m_panel_sweep_current = 0;
	m_panel_sweep_end = 0xff;
	m_panel_sweep_dispatch_target = 0xffffffffU;
	std::fill(std::begin(m_panel_sweep_before), std::end(m_panel_sweep_before), 0);
	m_panel_sweep_timer->adjust(attotime::never);
	{
		const char *const panel_b_conversation = std::getenv("ASR10_PANEL_B_CONVERSATION");
		m_panel_b_conversation_enabled =
			panel_b_conversation && panel_b_conversation[0] && panel_b_conversation[0] != '0';
	}
	m_panel_b_conversation_done = false;
	m_panel_b_conversation_seq = 0;
	m_panel_b_conversation_thrb = 0;
	m_panel_b_conversation_rhrb = 0;
	m_panel_receive_live_active = false;
	m_panel_receive_live_srb_reads = 0;
	m_panel_receive_live_rhrb_reads = 0;
	m_panel_receive_live_queue_calls = 0;
	m_panel_receive_live_fifo_overrun_pushes = 0;
	m_panel_receive_live_last_access_time = attotime::never;
	m_panel_receive_live_window_dumped = false;
	if (const char *const sweep_raw = std::getenv("ASR10_PANEL_SWEEP_RAW"); sweep_raw && sweep_raw[0])
	{
		char *end = nullptr;
		const unsigned long parsed = std::strtoul(sweep_raw, &end, 0);
		if (end && *end == 0 && parsed <= 0xff)
		{
			m_panel_sweep_enabled = true;
			m_panel_sweep_raw = u8(parsed);
			osd_printf_info("ASR10_PANEL_SWEEP event=config raw=%02x source=ASR10_PANEL_SWEEP_RAW\n",
				m_panel_sweep_raw);
		}
		else
		{
			osd_printf_info("ASR10_PANEL_SWEEP event=config_invalid value=\"%s\"\n", sweep_raw);
		}
	}
	if (const char *const sweep_all = std::getenv("ASR10_PANEL_SWEEP_ALL");
		sweep_all && sweep_all[0] && sweep_all[0] != '0')
	{
		m_panel_sweep_enabled = true;
		m_panel_sweep_all_enabled = true;
		m_panel_sweep_current = 0;
		m_panel_sweep_end = 0xff;
		if (const char *const sweep_start = std::getenv("ASR10_PANEL_SWEEP_START"); sweep_start && sweep_start[0])
		{
			char *end = nullptr;
			const unsigned long parsed = std::strtoul(sweep_start, &end, 0);
			if (end && *end == 0 && parsed <= 0xff)
				m_panel_sweep_current = u16(parsed);
		}
		if (const char *const sweep_end = std::getenv("ASR10_PANEL_SWEEP_END"); sweep_end && sweep_end[0])
		{
			char *end = nullptr;
			const unsigned long parsed = std::strtoul(sweep_end, &end, 0);
			if (end && *end == 0 && parsed <= 0xff)
				m_panel_sweep_end = u16(parsed);
		}
		if (m_panel_sweep_current > m_panel_sweep_end)
			m_panel_sweep_current = m_panel_sweep_end;
		m_panel_sweep_raw = u8(m_panel_sweep_current);
		osd_printf_info("ASR10_PANEL_SWEEP event=config_all start=%02x end=%02x\n",
			m_panel_sweep_current, m_panel_sweep_end);
	}
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
	m_es5506_diag_par_read_count = 0;
	m_primary_slot_snapshot_logged = false;
	m_fc60b0_verified = false;
	m_fc2d40_cluster_count = 0;
	m_fc3000_cluster_count = 0;
	const char *const panel_c_parser_trace = std::getenv("ASR10_DIAG_PANEL_C_PARSER_TRACE");
	m_panel_c_parser_trace_enabled =
		panel_c_parser_trace && panel_c_parser_trace[0] && panel_c_parser_trace[0] != '0';
	m_panel_c_parser_trace_active = false;
	m_panel_c_parser_trace_done = false;
	m_panel_c_parser_trace_count = 0;
	m_panel_c_parser_trace_last_pc = 0xffffffffU;
	m_last_pc = 0xffffffffU;
	m_last_distinct_pc = 0xffffffffU;
	m_pc_repeat_count = 0;
	m_pc_change_count = 0;
	m_dispatcher_hits = 0;
	std::fill(std::begin(m_context_hits), std::end(m_context_hits), 0);
	{
		const char *const step0_runtime_trace = std::getenv("ASR10_STEP0_RUNTIME_TRACE");
		m_step0_runtime_trace_enabled =
			step0_runtime_trace && step0_runtime_trace[0] && step0_runtime_trace[0] != '0';
	}
	for (auto &profile : m_step0_pc_profiles)
	{
		profile = pc_profile_window{};
		profile.recent_pcs.fill(0xffffffffU);
	}
	m_step0_file1_context_logged = false;
	m_step0_irq6_pending_landing = false;
	m_step0_irq6_pending_vector = 0;
	m_step0_irq6_pending_target = 0xffffffffU;
	m_step0_irq6_pending_iack_pc = 0xffffffffU;
	m_step0_irq6_isr_hist.fill(0);
	m_step0_irq6_isr_bit_hist.fill(0);
	m_step0_irq6_masked_bit_hist.fill(0);
	m_step0_irq6_duart_pending_count = 0;
	m_step0_irq6_non_duart_count = 0;
	m_step0_irq6_accept_count = 0;
	m_step0_duart_imr = 0;
	m_step0_thra_writes = 0;
	m_step0_thrb_writes = 0;
	m_step0_thra_write_pcs.clear();
	m_step0_region_handoffs.fill({});
	m_step0_region_handoff_counts.fill(0);
	m_step0_region_handoff_truncated.fill(0);
	{
		const char *const rx_event_trace = std::getenv("ASR10_RX_EVENT_TRACE");
		m_rx_event_trace_enabled =
			rx_event_trace && rx_event_trace[0] && rx_event_trace[0] != '0';
	}
	m_rx_event_lowmem_dumped = false;
	m_rx_event_pre_inject_dumped = false;
	m_rx_event_after_reported = false;
	m_rx_event_id = m_panel_sweep_raw;
	m_rx_event_slot_base = 0;
	m_rx_event_slot_end = 0;
	m_rx_event_slot_count = 0;
	m_rx_event_slot_activity.fill({});
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

	if constexpr (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR)
	{
		if (level == ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL &&
			(fc6814_before & ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK))
		{
			vector = ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE;
			custom_vector = true;
		}
	}
	if (level == 6)
	{
		// docs/asr10/PLAN.md fas 3 steg 2 (minimal slice): the external
		// IRQ6 vector-supply formula now lives in the mc68302 device
		// itself, not in a driver-side shadow. See
		// mc68302_device::irq6_ack_vector().
		vector = m_maincpu->irq6_ack_vector();
		custom_vector = true;
		if (m_step0_runtime_trace_enabled)
		{
			auto const disable_side_effects = machine().disable_side_effects();
			const u8 duart_isr = u8(m_duart->read(0x05));
			const u8 duart_masked = duart_isr & m_step0_duart_imr;
			const bool duart_pending = m_duart->irq_pending();
			const u32 target = lowmem_long(u32(vector) * 4) & 0x00ffffff;
			m_step0_irq6_pending_landing = true;
			m_step0_irq6_pending_vector = vector;
			m_step0_irq6_pending_target = target;
			m_step0_irq6_pending_iack_pc = pc;
			m_step0_irq6_accept_count++;
			m_step0_irq6_isr_hist[duart_isr]++;
			if (duart_pending)
				m_step0_irq6_duart_pending_count++;
			else
				m_step0_irq6_non_duart_count++;
			for (u32 bit = 0; bit != 8; bit++)
			{
				if (BIT(duart_isr, bit))
					m_step0_irq6_isr_bit_hist[bit]++;
				if (BIT(duart_masked, bit))
					m_step0_irq6_masked_bit_hist[bit]++;
			}
			osd_printf_info("ASR10_STEP0_IRQ6_ACCEPT time=%s iack_pc=%06x vector=%02x target=%06x "
				"source=%s duart_irq_pending=%u duart_isr=%02x duart_imr=%02x duart_masked=%02x count=%u\n",
				machine().time().to_string(), pc, vector, target,
				duart_pending ? "duart" : "non_duart_or_unknown", duart_pending ? 1 : 0,
				duart_isr, m_step0_duart_imr, duart_masked, m_step0_irq6_accept_count);
		}
		if (m_rx_event_trace_enabled)
		{
			auto const disable_side_effects = machine().disable_side_effects();
			const u8 duart_isr = u8(m_duart->read(0x05));
			const u32 target = lowmem_long(u32(vector) * 4) & 0x00ffffff;
			osd_printf_info("ASR10_RX_EVENT event=irq6_accept id=%u time=%s iack_pc=%06x "
				"isr=%02x vector=%02x target=%06x duart_irq_pending=%u imr=%02x masked=%02x\n",
				m_rx_event_id, machine().time().to_string(), pc, duart_isr, vector, target,
				m_duart->irq_pending() ? 1 : 0, m_step0_duart_imr, duart_isr & m_step0_duart_imr);
		}
	}

	if (m_dispatcher_rte_first_pc_pending)
	{
		m_dispatcher_rte_iack_seen = true;
		m_dispatcher_rte_iack_vector = vector;
		m_dispatcher_rte_iack_level = level;
		m_dispatcher_rte_iack_pc = pc;
		m_dispatcher_rte_iack_sr = sr;
		logerror("ASR10_DISPATCHER_RTE_IMMEDIATE_IACK pc=%06x sr=%04x sr_mask=%u "
			"irq_level=%u returned_vector=%02x custom_vector=%u frame_pc=%06x frame_sr=%04x "
			"fc6814_before=%04x fc6816_before=%04x fc6818=%04x rte_count=%u\n",
			pc, sr, (sr >> 8) & 7, level, vector, custom_vector ? 1 : 0,
			m_dispatcher_rte_frame_pc, m_dispatcher_rte_frame_sr,
			fc6814_before, fc6816_before, fc6818, m_f87f96_queue_rte_count);
	}

	m_iack_trace_count++;
	logerror("ASR10_M68K_IACK count=%u irq_level=%u default_autovector=%02x returned_vector=%02x "
		"custom_vector=%u pc=%06x sr=%04x sr_mask=%u fc6812=%04x "
		"fc6814_before=%04x fc6814_after=%04x fc6816_before=%04x fc6816_after=%04x fc6818=%04x "
		"fc684a=%04x fc6850=%04x fc6852=%04x source_mask=%04x source_pending=%u "
		"dispatcher_count=%u panel=\"%s\"\n",
		m_iack_trace_count, level, autovector, vector, custom_vector ? 1 : 0,
		pc, sr, (sr >> 8) & 7, fc6812,
		fc6814_before, m_m68302_internal_shadow[0x14 >> 1],
		fc6816_before, m_m68302_internal_shadow[0x16 >> 1], fc6818,
		fc684a, fc6850, fc6852, ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK,
		(fc6814_before & ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK) ? 1 : 0,
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
			if constexpr (ASR10_LOG_FDC_04B0_CONTEXT)
			{
				if (byte_address == 0x04b0)
					(void)0;
			}
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








void asr10_boot_state::panel_c_parser_trace_stop(const char *reason, u32 pc)
{
	if (!m_panel_c_parser_trace_active)
		return;

	logerror("ASR10_DIAG_PANEL_C_PARSER_TRACE event=stop reason=%s seq=%u pc=%06x "
		"b03c0=%02x b03c4=%02x b03c5=%02x b03c6=%02x b03bc=%02x\n",
		reason, m_panel_c_parser_trace_count, pc, lowmem_byte(0x03c0), lowmem_byte(0x03c4),
		lowmem_byte(0x03c5), lowmem_byte(0x03c6), lowmem_byte(0x03bc));
	m_panel_c_parser_trace_active = false;
	m_panel_c_parser_trace_done = true;
	m_pc_timer->adjust(attotime::zero, 0, attotime::from_ticks(64, m_maincpu->clock()));
}


void asr10_boot_state::panel_c_queue_rx(u8 data, const char *reason, u32 pc)
{
	if (machine().side_effects_disabled())
		return;

	const int fifo_before = m_duart->m_chanB->rx_fifo_count();
	const bool overflow_push = fifo_before >= (MC68681_RX_FIFO_SIZE + 1);
	m_duart->m_chanB->rx_fifo_push(data, 0);
	const int fifo_after = m_duart->m_chanB->rx_fifo_count();
	if (m_rx_event_trace_enabled)
	{
		osd_printf_info("ASR10_RX_EVENT event=push id=%u time=%s reason=%s pc=%06x "
			"value=%02x fifo_before=%d fifo_after=%d overflow_push=%u irq_pending=%u\n",
			m_rx_event_id, machine().time().to_string(), reason, pc, data, fifo_before, fifo_after,
			overflow_push ? 1 : 0, m_duart->irq_pending() ? 1 : 0);
	}
	if (m_panel_receive_live_active)
	{
		m_panel_receive_live_queue_calls++;
		if (overflow_push)
			m_panel_receive_live_fifo_overrun_pushes++;
		osd_printf_info("ASR10_PANEL_RECEIVE_LIVE event=queue_rx time=%s reason=%s pc=%06x "
			"byte=%02x fifo_before=%d fifo_after=%d overflow_push=%u irq_pending=%u queue_calls=%u\n",
			machine().time().to_string(), reason, pc, data, fifo_before, fifo_after,
			overflow_push ? 1 : 0, m_duart->irq_pending() ? 1 : 0, m_panel_receive_live_queue_calls);
	}
	logerror("ASR10_PANEL_AUTORESPOND event=rx_queued reason=%s pc=%06x "
		"rx=%02x source=mc68681_channel_b_fifo slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x count_03bc=%02x\n",
		reason, pc, data,
		lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6),
		lowmem_byte(0x03bc));
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::lrclk_toggle)
{
	m_lrclk_level = !m_lrclk_level;
	m_maincpu->set_pb_input(3, m_lrclk_level);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::panel_autorespond_fire)
{
	const u32 write_pc = u32(param);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_panel_autorespond_injected_count++;
	logerror("ASR10_PANEL_AUTORESPOND event=inject_response seq=%u write_pc=%06x pc=%06x byte=ff "
		"count_03bc=%02x idle_03c5=%02x slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		m_panel_autorespond_injected_count, write_pc, pc, lowmem_byte(0x03bc), lowmem_byte(0x03c5),
		lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));
	panel_c_queue_rx(0xff, "autorespond_fc4817_write", write_pc);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::panel_sweep_fire)
{
	if (!m_panel_sweep_enabled || machine().side_effects_disabled())
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (m_panel_sweep_all_enabled && m_panel_sweep_waiting_sample)
	{
		const std::string after = current_display_text();
		const bool changed = std::strcmp(m_panel_sweep_before, after.c_str()) != 0;
		osd_printf_info("ASR10_PANEL_SWEEP_RESULT raw=%02x consumed=%u consumed_value=%02x "
			"dispatch_target=%06x before=\"%s\" after=\"%s\" changed=%u\n",
			m_panel_sweep_raw, m_panel_sweep_consumed ? 1 : 0, m_panel_sweep_consumed_value,
			m_panel_sweep_dispatch_target, m_panel_sweep_before, after.c_str(), changed ? 1 : 0);
		if (!m_panel_sweep_consumed || changed || m_panel_sweep_current >= m_panel_sweep_end)
		{
			machine().schedule_exit();
			return;
		}
		m_panel_sweep_current++;
		m_panel_sweep_raw = u8(m_panel_sweep_current);
		std::strncpy(m_panel_sweep_before, after.c_str(), PANEL_TEXT_LENGTH - 1);
		m_panel_sweep_before[PANEL_TEXT_LENGTH - 1] = 0;
		m_panel_sweep_consumed = false;
		m_panel_sweep_consumed_value = 0;
		m_panel_sweep_dispatch_target = 0xffffffffU;
		m_panel_sweep_waiting_sample = false;
		m_panel_sweep_timer->adjust(attotime::from_msec(20));
		return;
	}

	if (!m_panel_sweep_all_enabled && m_panel_sweep_injected)
		return;

	if (m_rx_event_trace_enabled)
		osd_printf_info("ASR10_RX_EVENT event=inject id=%u time=%s raw=%02x pc=%06x before=\"%s\"\n",
			m_rx_event_id, machine().time().to_string(), m_panel_sweep_raw, pc, m_panel_sweep_before);
	osd_printf_info("ASR10_PANEL_SWEEP event=inject raw=%02x pc=%06x before=\"%s\"\n",
		m_panel_sweep_raw, pc, m_panel_sweep_before);
	m_panel_sweep_injected = true;
	m_panel_sweep_waiting_sample = true;
	panel_c_queue_rx(m_panel_sweep_raw, "panel_sweep_raw", pc);
	m_panel_sweep_timer->adjust(attotime::from_msec(100));
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


u16 asr10_boot_state::es5506_host_read_par_diag()
{
	static constexpr u16 PAR_DIAGNOSTIC_VALUE = 0x200;
	// Fixed-value PAR plumbing. NOT an analog model, NOT a claim that this
	// value is a real resting position for any physical control.
	m_es5506_diag_par_read_count++;
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	logerror("ASR10_ES5506_HOST event=par_diag_read source=diagnostic_constant value=%03x "
		"read_count=%u caller_pc=%06x\n",
		PAR_DIAGNOSTIC_VALUE, m_es5506_diag_par_read_count, pc);
	return PAR_DIAGNOSTIC_VALUE;
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
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		(void)0;
	m_es5510_host->host_w(0x80, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xa0);
}

void asr10_boot_state::es5510_host_write_select_gpr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		(void)0;
	m_es5510_host->host_w(0xa0, data);
}

u8 asr10_boot_state::es5510_host_write_select_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xc0);
}

void asr10_boot_state::es5510_host_write_select_instr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		(void)0;
	m_es5510_host->host_w(0xc0, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xe0);
}

void asr10_boot_state::es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		(void)0;
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
	if (!machine().side_effects_disabled())
		rx_event_note_slot_write(m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
			byte_address, previous, m_lowmem_shadow[offset], mem_mask);
	if (!machine().side_effects_disabled() && m_panel_sweep_enabled && m_panel_sweep_waiting_sample &&
		byte_address >= 0x03c0 && byte_address <= 0x03c6)
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		osd_printf_info("ASR10_PANEL_SWEEP_TAIL_WRITE raw=%02x time=%s pc=%06x "
			"address=%04x previous=%04x current=%04x mem_mask=%04x\n",
			m_panel_sweep_raw, machine().time().to_string(), pc, byte_address,
			previous, m_lowmem_shadow[offset], mem_mask);
	}
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
	if (m_download_trace_enabled &&
		(byte_address == 0x0e7e || byte_address == 0x0e89 || byte_address == 0x0e9c || byte_address == 0x0e8c ||
			byte_address == 0x0e82 || byte_address == 0x0e8a) &&
		!machine().side_effects_disabled())
	{
		// byte_address == 0x0e9c (word-aligned) covers the odd-address
		// retry counter at $0e9d, which a prior session's tap missed by
		// checking 0x0e9d directly (byte_address here is always even,
		// offset<<1).
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const char *const field =
			byte_address == 0x0e7e ? "table_source_pointer_a3" :
			byte_address == 0x0e89 ? "first_record_byte" :
			byte_address == 0x0e8c ? "record_scratch" :
			byte_address == 0x0e82 ? "saved_sr_slot" :
			byte_address == 0x0e8a ? "loop_done_flag_0e8a" : "retry_counter_0e9c_0e9d";
		logerror("ASR10_TASK3_DOWNLOAD_TRACE event=lowmem_store field=%s pc=%06x address=%06x "
			"previous=%04x new=%04x mem_mask=%04x "
			"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x\n",
			field, pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
			u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
			u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
			u16(m_maincpu->state_int(M68K_SR)));
		// filesystem-browser-map.md TASK 3 (ES5510/ESP download hypothesis
		// round): one-shot static dump of the retry-loop's own code range
		// (ffc840-ffc8c0, covering the ffc866/ffc872/ffc87e/ffc8a0/ffc8a4
		// PCs already seen writing these same lowmem fields), so the exact
		// compare/branch that decides retry-vs-give-up can be read directly
		// instead of only inferred from field values.
		if (!m_download_retry_loop_dump_logged && read_highview_word(0x00ffc866) != 0)
		{
			m_download_retry_loop_dump_logged = true;
			(void)0;
			}
			// filesystem-browser-map.md 4.26: f973f0/f97580 detection uses
			// these two DATA writes (0x0e7e is f973f0's own first
			// instruction, "move.l A3,$e7e.w"; 0x0e8c is f97580's own
			// instruction, "addq.b #1,$e8c.w") because lowmem_w is the
			// primary backing handler for this address range, not a
			// passthrough tap -- opcode-fetch taps on this core never fire
			// (see the note near FC2068 in machine_start()). The 0x0e8c
			// write fires on EVERY retry increment; log_esp_other_table_
			// first_retry's own one-shot guard restricts it to the first.
			if (byte_address == 0x0e7e && pc == 0x00f973f0)
				(void)0;
			if (byte_address == 0x0e8c && pc == 0x00f97580)
				(void)0;
			// filesystem-browser-map.md 4.26 TASK 6: HALL REVERB table-level
			// retry/give-up markers. Table match only (not record-scoped
			// like log_hall_reverb_event's other call sites) because retry
			// and give-up are attempt boundaries for the WHOLE table
			// transfer (f9740a restarts all record types on a mismatch),
			// not a single record.
			if (m_lowmem_shadow[0x0e8e >> 1] == 0x0001 && m_lowmem_shadow[(0x0e8e >> 1) + 1] == 0x0400)
			{
				if (byte_address == 0x0e8c)
					(void)0;
				if (byte_address == 0x0e8a)
					(void)0;
			}
	}
	// filesystem-browser-map.md 4.25 TASK 3 (observation-only): bounded
	// write-provenance recorder for the low-RAM window surrounding the
	// a3=~0x010722 address seen at the other-table retry-exhaustion path
	// (4.24). No existing recorder covers writes above 0x10000, so this
	// is the minimal extension requested -- a narrow window (0x010600-
	// 0x0108ff, 768 bytes) and a hard cap on event count, not a general
	// loader/chunk recorder.
	if (m_download_trace_enabled && byte_address >= 0x010600 && byte_address <= 0x0108ff &&
		m_esp_010722_window_write_count < 200 && !machine().side_effects_disabled())
	{
		m_esp_010722_window_write_count++;
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		logerror("ASR10_ESP_010722_WINDOW_WRITE seq=%u pc=%06x address=%06x previous=%04x new=%04x "
			"mem_mask=%04x sp=%06x d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a3=%08x\n",
			m_esp_010722_window_write_count, pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
			u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff);
	}
	if (m_disk_sig_trace_enabled &&
		(byte_address == 0x049c || byte_address == 0x04ae || byte_address == 0x0944 ||
			byte_address == 0x0954 || byte_address == 0x04b8) &&
		!machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const char *const field =
			byte_address == 0x049c ? "error_flag_049d" :
			byte_address == 0x04ae ? "error_subcode_04ae" :
			byte_address == 0x0944 ? "sector1_buffer_start" :
			byte_address == 0x0954 ? "signature_compare_word0" : "disk_valid_flag_04b8";
		logerror("ASR10_TASK1_DISK_SIG event=lowmem_store field=%s pc=%06x previous_pc=%06x address=%06x "
			"previous=%04x new=%04x mem_mask=%04x d0=%08x d3=%08x\n",
			field, pc, m_last_distinct_pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D3)));
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
	if constexpr (ASR10_LOG_FDC_04B0_CONTEXT)
	{
		if (m_seen_insert_disk_prompt && byte_address == 0x04b0)
			(void)0;
	}
	if (byte_address == 0x04b0)
		(void)0;
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

	if (m_root_directory_trace_enabled && !machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		log_root_directory_table_write(pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask);
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
	note_step0_fdc_access(false);
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
				if (m_fdc_synth_tc_enabled && !m_fdc_synth_tc_pulsed_this_txn &&
					m_fdc_cmd46_write_count == m_fdc_cmd46_write_bytes.size())
				{
					const u8 n_byte = m_fdc_cmd46_write_bytes[5];
					const u32 expected_sector_size = n_byte <= 7 ? (128U << n_byte) : 0;
					if (expected_sector_size && m_fdc_cmd46_msr_exm_seen_count == expected_sector_size)
					{
						m_fdc_synth_tc_pulsed_this_txn = true;
						m_fdc->tc_w(false);
						m_fdc->tc_w(true);
						logerror("ASR10_FDC_TC source=synthetic_host_completion pc=%06x address=%06x "
							"data=%02x transaction=%u transferred_bytes=%u remaining_bytes=0\n",
							pc, address, device_data, m_fdc_cmd46_transaction, m_fdc_cmd46_msr_exm_seen_count);
					}
				}
			}
			else
				m_fdc_cmd46_last_msr_before_result = m_fdc_last_msr;
		}
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
	(void)0;
	note_step0_fdc_access(true);
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
			m_fdc_synth_tc_pulsed_this_txn = false;
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
	(void)0;

	// The real SCN2681 register file (ACR, CTU/CTL preload and start/stop
	// counter commands, MR/CR/SR, RHR/THR) lives in m_duart now. Panel reply
	// bytes are injected into channel B's RX FIFO, not shadowed here.
	const u8 srb_before_rhrb = (address == 0x00fc4816 && ACCESSING_BITS_0_7) ? u8(m_duart->read(0x09)) : 0;
	const int fifo_before_read = m_duart->m_chanB->rx_fifo_count();
	const u16 conv_03c0_before = lowmem_word(0x03c0);
	const u16 conv_03c4_before = lowmem_word(0x03c4);
	u16 raw_data = ACCESSING_BITS_0_7 ? m_duart->read(word) : 0;
	if (!machine().side_effects_disabled() && m_panel_b_conversation_enabled &&
		!m_panel_b_conversation_done && ACCESSING_BITS_0_7 && address == 0x00fc4816 &&
		(fifo_before_read || pc == 0x00ffb0d4))
	{
		m_panel_b_conversation_seq++;
		m_panel_b_conversation_rhrb++;
		osd_printf_info("ASR10_PANEL_B_CONVERSATION seq=%u dir=RX reg=RHRB time=%s pc=%06x "
			"value=%02x fifo_before=%d fifo_after=%d state03c0_before=%04x state03c0_after=%04x "
			"buf03c4_before=%04x buf03c4_after=%04x\n",
			m_panel_b_conversation_seq, machine().time().to_string(), pc, u8(raw_data),
			fifo_before_read, m_duart->m_chanB->rx_fifo_count(),
			conv_03c0_before, lowmem_word(0x03c0), conv_03c4_before, lowmem_word(0x03c4));
	}
	if (!machine().side_effects_disabled() && m_rx_event_trace_enabled && ACCESSING_BITS_0_7 &&
		(address == 0x00fc4812 || address == 0x00fc4816))
	{
		osd_printf_info("ASR10_RX_EVENT event=duart_read id=%u time=%s pc=%06x "
			"reg=%s address=%06x value=%02x fifo_before=%d fifo_after=%d irq_pending=%u\n",
			m_rx_event_id, machine().time().to_string(), pc,
			address == 0x00fc4812 ? "SRB" : "RHRB", address | 1, u8(raw_data),
			fifo_before_read, m_duart->m_chanB->rx_fifo_count(), m_duart->irq_pending() ? 1 : 0);
	}
	if (!machine().side_effects_disabled() && m_panel_sweep_enabled && m_panel_sweep_waiting_sample &&
		ACCESSING_BITS_0_7 && address == 0x00fc4816 && pc == 0x00ffb0d4 && u8(raw_data) == m_panel_sweep_raw)
	{
		m_panel_sweep_consumed = true;
		m_panel_sweep_consumed_value = u8(raw_data);
		const u16 word03c0 = lowmem_word(0x03c0);
		const u32 target03c0 = BIT(word03c0, 15) ? (0x00ff0000U | word03c0) : word03c0;
		std::string hex;
		for (u32 index = 0; index != 64; index++)
		{
			if (index)
				hex += ' ';
			hex += util::string_format("%02x", m_maincpu->space(AS_PROGRAM).read_byte(target03c0 + index));
		}
		osd_printf_info("ASR10_PANEL_SWEEP_TAIL raw=%02x rhrb_pc=%06x value=%02x "
			"word_03c0=%04x target=%06x target64=\"%s\"\n",
			m_panel_sweep_raw, pc, u8(raw_data), word03c0, target03c0, hex.c_str());
	}
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
	if constexpr (ASR10_DIAG_PANEL_B)
	{
		if (!machine().side_effects_disabled() && address == 0x00fc4816 && ACCESSING_BITS_0_7)
			(void)0;
	}
	if (!machine().side_effects_disabled() &&
		address == 0x00fc4816 && ACCESSING_BITS_0_7 && BIT(srb_before_rhrb, 0))
	{
		logerror("ASR10_PANEL_AUTORESPOND event=rhrb_pop pc=%06x byte=%02x "
			"source=mc68681_channel_b_fifo count_03bc=%02x parser_state_03c0=%04x\n",
			pc, u8(data),
			lowmem_byte(0x03bc), lowmem_word(0x03c0));
		if (m_panel_c_parser_trace_enabled && !m_panel_c_parser_trace_done && pc == 0x00ffb242 &&
			(u8(data) == 0x00 || u8(data) == 0xff))
		{
			m_panel_c_parser_trace_active = true;
			m_panel_c_parser_trace_count = 0;
			m_panel_c_parser_trace_last_pc = 0xffffffffU;
			(void)0;
			m_pc_timer->adjust(attotime::from_ticks(1, m_maincpu->clock()), 0,
				attotime::from_ticks(1, m_maincpu->clock()));
		}
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
			if (m_panel_sweep_enabled)
				osd_printf_info("ASR10_PANEL_SWEEP event=rhrb raw=%02x mapped=%02x pc=%06x\n",
					raw_byte, mapped_byte, pc);
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
	if (!machine().side_effects_disabled() && m_panel_b_conversation_enabled &&
		!m_panel_b_conversation_done && ACCESSING_BITS_0_7 && address == 0x00fc4817)
	{
		m_panel_b_conversation_seq++;
		m_panel_b_conversation_thrb++;
		osd_printf_info("ASR10_PANEL_B_CONVERSATION seq=%u dir=TX reg=THRB time=%s pc=%06x "
			"value=%02x state03c0_before=%04x state03c0_after=%04x "
			"buf03c4_before=%04x buf03c4_after=%04x\n",
			m_panel_b_conversation_seq, machine().time().to_string(), pc, u8(data),
			lowmem_word(0x03c0), lowmem_word(0x03c0), lowmem_word(0x03c4), lowmem_word(0x03c4));
	}
	if (!machine().side_effects_disabled() && m_step0_runtime_trace_enabled && ACCESSING_BITS_0_7)
	{
		if (address == 0x00fc4807)
		{
			m_step0_thra_writes++;
			m_step0_thra_write_pcs[pc]++;
			osd_printf_info("ASR10_STEP0_DUART_WRITE time=%s pc=%06x reg=THRA address=%06x value=%02x count=%u\n",
				machine().time().to_string(), pc, address, u8(data), m_step0_thra_writes);
		}
		else if (address == 0x00fc4817)
		{
			m_step0_thrb_writes++;
		}
	}
	if (!machine().side_effects_disabled() && ACCESSING_BITS_0_7 && address == 0x00fc480b &&
		(m_step0_runtime_trace_enabled || m_rx_event_trace_enabled))
		m_step0_duart_imr = u8(data);
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
					m_panel_diag_direct_text_begin_count++;
					panel_submission_trace("begin", "DirectText", character);
					if (m_root_directory_trace_enabled)
					{
						const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
						auto const disable_side_effects = machine().disable_side_effects();
						char preview[25]{};
						for (u32 i = 0; i != 24; i++)
						{
							const u8 c = m_maincpu->space(AS_PROGRAM).read_byte(a2 + i);
							preview[i] = c >= 0x20 && c <= 0x7e ? char(c) : '.';
							if (!c)
								break;
						}
						m_root_directory_direct_text_count++;
						osd_printf_info("ASR10_ROOT_DIRECTORY_DIRECT_TEXT event=begin count=%u pc=%06x "
							"prefix=%02x source_a2=%06x normalized_source=%06x preview=\"%s\"\n",
							m_root_directory_direct_text_count, pc, character, a2,
							normalize_panel_descriptor_identity(a2), preview);
					}
				}
			}
			else if (pc == 0x00f89cb0 && m_panel_direct_text_active)
			{
				m_panel_receive_role = u8(panel_byte_role::TEXT_PAYLOAD);
				const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
				auto const disable_side_effects = machine().disable_side_effects();
				if (m_maincpu->space(AS_PROGRAM).read_byte(a2) == 0)
				{
					m_panel_diag_direct_text_end_count++;
					panel_submission_trace("end", "DirectText", character);
					if (m_root_directory_trace_enabled)
						osd_printf_info("ASR10_ROOT_DIRECTORY_DIRECT_TEXT event=end pc=%06x final_byte=%02x "
							"next_a2=%06x normalized_next=%06x panel=\"%s\"\n",
							pc, character, a2, normalize_panel_descriptor_identity(a2), m_panel_text);
					m_panel_direct_text_active = false;
				}
			}
		}
		if (!machine().side_effects_disabled() && m_gen_thrb_count < m_gen_thrb_bytes.size())
			m_gen_thrb_bytes[m_gen_thrb_count++] = character;
		if (!machine().side_effects_disabled())
		{
			const char *const thrb_source = (pc == 0x00f89cb0) ? "f89cb0" :
				(pc == 0x00f89aa4) ? "f89aa4" : "other";
			m_panel_autorespond_scheduled_count++;
			logerror("ASR10_PANEL_AUTORESPOND event=schedule_response seq=%u pc=%06x source=%s "
				"byte=%02x count_03bc=%02x idle_03c5=%02x\n",
				m_panel_autorespond_scheduled_count, pc, thrb_source, character,
				lowmem_byte(0x03bc), lowmem_byte(0x03c5));
			m_panel_autorespond_timer->adjust(attotime::from_ticks(4, m_maincpu->clock()), s32(pc));
		}
		if (m_seen_loading_system_prompt && !machine().side_effects_disabled())
		{
			m_post_loading_panel_write_count++;
			logerror("ASR10_POST_LOADING_PANEL_WRITE pc=%06x data=%02x printable=%u char='%c' count=%u\n",
				pc, character, character >= 0x20 && character <= 0x7e ? 1 : 0,
				character >= 0x20 && character <= 0x7e ? char(character) : '.',
				m_post_loading_panel_write_count);
		}
		if constexpr (ASR10_LOG_PANEL_BYTES)
		{
			if (character >= 0x20 && character <= 0x7e)
				logerror("ASR10PANEL char='%c' hex=%02x pc=%06x\n", character, character, pc);
			else
				logerror("ASR10PANEL control=%02x pc=%06x\n", character, pc);
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

	if (m_panel_sweep_enabled && !m_panel_sweep_armed && !m_panel_sweep_injected &&
		strstr(m_panel_text, "FILE 1  TUTORIAL BNK"))
	{
		const std::string display = current_display_text();
		strncpy(m_panel_sweep_before, display.c_str(), PANEL_TEXT_LENGTH - 1);
		m_panel_sweep_before[PANEL_TEXT_LENGTH - 1] = 0;
		for (u32 index = 0; index != 10; index++)
			osd_printf_info("ASR10_DISPLAY_VERIFY sample=%u display=\"%s\" expected=\"FILE 1  TUTORIAL BNK\" match=%u\n",
				index + 1, current_display_text().c_str(),
				current_display_text() == "FILE 1  TUTORIAL BNK" ? 1 : 0);
		if (m_rx_event_trace_enabled && !m_rx_event_pre_inject_dumped)
		{
			m_rx_event_pre_inject_dumped = true;
			osd_printf_info("ASR10_RX_EVENT event=precheck id=%u time=%s raw=%02x display=\"%s\"\n",
				m_rx_event_id, machine().time().to_string(), m_panel_sweep_raw, m_panel_sweep_before);
			rx_event_dump_lowmem();
			rx_event_dump_slots("pre_inject");
		}
		m_panel_sweep_armed = true;
		osd_printf_info("ASR10_PANEL_SWEEP event=precheck raw=%02x display=\"%s\" result=valid\n",
			m_panel_sweep_raw, m_panel_sweep_before);
		m_panel_sweep_timer->adjust(attotime::from_msec(100));
	}
	if (m_rx_event_trace_enabled && m_panel_sweep_injected && !m_rx_event_after_reported &&
		current_display_text() != m_panel_sweep_before)
	{
		m_rx_event_after_reported = true;
		osd_printf_info("ASR10_RX_EVENT event=display_after id=%u time=%s raw=%02x display=\"%s\"\n",
			m_rx_event_id, machine().time().to_string(), m_panel_sweep_raw, current_display_text().c_str());
		rx_event_dump_slots("after_display_change");
	}
	if (!m_panel_receive_live_active && strstr(m_panel_text, "FILE 1  TUTORIAL BNK"))
	{
		m_panel_receive_live_active = true;
		start_pc_profile(m_step0_pc_profiles[1], "B_FILE1");
		osd_printf_info("ASR10_PANEL_RECEIVE_LIVE event=active display=\"%s\"\n", m_panel_text);
	}
	if (m_panel_b_conversation_enabled && !m_panel_b_conversation_done &&
		strstr(m_panel_text, "FILE 1  TUTORIAL BNK"))
	{
		m_panel_b_conversation_done = true;
		osd_printf_info("ASR10_PANEL_B_CONVERSATION_SUMMARY seq=%u tx_thrb=%u rx_rhrb=%u "
			"display=\"%s\" state03c0=%04x buf03c4=%04x time=%s\n",
			m_panel_b_conversation_seq, m_panel_b_conversation_thrb, m_panel_b_conversation_rhrb,
			current_display_text().c_str(), lowmem_word(0x03c0), lowmem_word(0x03c4),
			machine().time().to_string());
		machine().schedule_exit();
	}
	if (m_step0_runtime_trace_enabled && m_panel_receive_live_active && !m_step0_file1_context_logged)
	{
		m_step0_file1_context_logged = true;
		auto const disable_side_effects = machine().disable_side_effects();
		const u8 ivr = u8(m_duart->read(0x0c));
		const u32 autovec78 = read_program_word(0x000078) << 16 | read_program_word(0x00007a);
		const u16 word03c0 = lowmem_word(0x03c0);
		const u32 signext03c0 = BIT(word03c0, 15) ? (0x00ff0000U | word03c0) : word03c0;
		osd_printf_info("ASR10_STEP0_RUNTIME_CONTEXT time=%s ivr=%02x autovec78_long=%08x "
			"word_0003c0=%04x signext_0003c0=%06x\n",
			machine().time().to_string(), ivr, autovec78, word03c0, signext03c0);
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
		if (m_root_directory_trace_enabled && strstr(m_panel_text, "NO INST OR BANK FILES"))
		{
			m_root_directory_no_inst_seen = true;
			osd_printf_info("ASR10_ROOT_DIRECTORY_TEXT text=\"%s\" first_pc=%06x last_pc=%06x "
				"descriptor_16c4_entries=%u\n",
				m_panel_text, m_panel_msg_first_pc, m_panel_msg_last_pc,
				m_root_directory_16c4_entry_count);
		}
		if (strstr(m_panel_text, "ERROR 009 - REBOOT ?"))
		{
			m_seen_error_reboot_prompt = true;
			(void)0;
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


void asr10_boot_state::start_pc_profile(pc_profile_window &window, const char *name)
{
	if (!m_step0_runtime_trace_enabled || window.active || window.done)
		return;

	window.name = name;
	window.active = true;
	window.start = machine().time();
	window.end = window.start + attotime::from_seconds(20);
	osd_printf_info("ASR10_STEP0_PC_PROFILE event=start window=%s time=%s duration_s=20 sample_every_instructions=1024\n",
		name, window.start.to_string());
}


void asr10_boot_state::sample_pc_profile(pc_profile_window &window, u32 pc)
{
	if (!window.active || window.done)
		return;

	const attotime now = machine().time();
	if (now >= window.end)
	{
		window.active = false;
		window.done = true;
		osd_printf_info("ASR10_STEP0_PC_PROFILE event=done window=%s time=%s instructions=%llu samples=%llu\n",
			window.name ? window.name : "unknown", now.to_string(),
			(unsigned long long)window.instructions, (unsigned long long)window.samples);
		return;
	}

	window.instructions++;
	if ((window.instructions & 1023) != 0)
		return;

	window.samples++;
	window.pc_counts[pc]++;
	window.recent_pcs[window.recent_pos++ & (window.recent_pcs.size() - 1)] = pc;
	if (read_program_word(pc) == 0x4e72)
		window.stop_samples++;
}


void asr10_boot_state::pc_profile_summary()
{
	if (!m_step0_runtime_trace_enabled)
		return;

	for (const pc_profile_window &window : m_step0_pc_profiles)
	{
		const attotime end = window.done ? window.end : machine().time();
		const attotime elapsed = (window.start == attotime::never) ? attotime::zero : (end - window.start);
		const double elapsed_s = elapsed.as_double();
		const double sample_hz = elapsed_s > 0.0 ? double(window.samples) / elapsed_s : 0.0;
		std::vector<std::pair<u32, u32>> counts;
		counts.reserve(window.pc_counts.size());
		for (const auto &entry : window.pc_counts)
			counts.emplace_back(entry.first, entry.second);
		std::sort(counts.begin(), counts.end(),
			[](const auto &a, const auto &b)
			{
				if (a.second != b.second)
					return a.second > b.second;
				return a.first < b.first;
			});
		u64 top3 = 0;
		for (size_t index = 0; index < std::min<size_t>(3, counts.size()); index++)
			top3 += counts[index].second;

		osd_printf_info("ASR10_STEP0_PC_PROFILE_SUMMARY window=%s active=%u done=%u start=%s "
			"elapsed_s=%.6f sample_every_instructions=1024 samples=%llu sample_hz=%.3f "
			"instructions=%llu distinct_pc=%zu stop_samples=%u top3_share=%.6f fdc_reads=%u fdc_writes=%u\n",
			window.name ? window.name : "not_started", window.active ? 1 : 0, window.done ? 1 : 0,
			window.start == attotime::never ? "never" : window.start.to_string(), elapsed_s,
			(unsigned long long)window.samples, sample_hz,
			(unsigned long long)window.instructions, counts.size(), window.stop_samples,
			window.samples ? double(top3) / double(window.samples) : 0.0,
			window.fdc_reads, window.fdc_writes);

		for (size_t index = 0; index < std::min<size_t>(20, counts.size()); index++)
		{
			osd_printf_info("ASR10_STEP0_PC_PROFILE_TOP window=%s rank=%zu pc=%06x count=%u share=%.6f\n",
				window.name ? window.name : "not_started", index + 1, counts[index].first,
				counts[index].second,
				window.samples ? double(counts[index].second) / double(window.samples) : 0.0);
		}

		std::string recent;
		const u32 recent_count = std::min<u32>(window.recent_pos, window.recent_pcs.size());
		for (u32 i = 0; i < recent_count; i++)
		{
			const u32 pos = (window.recent_pos - recent_count + i) & (window.recent_pcs.size() - 1);
			if (!recent.empty())
				recent += ',';
			recent += util::string_format("%06x", window.recent_pcs[pos]);
		}
		osd_printf_info("ASR10_STEP0_PC_PROFILE_RECENT window=%s count=%u pcs=%s\n",
			window.name ? window.name : "not_started", recent_count, recent.c_str());
	}
}


bool asr10_boot_state::normalized_rom_handoff_source(u32 pc, u32 &normalized) const
{
	pc &= 0x00ffffff;
	if (pc >= 0x00f80000U && pc <= 0x00fbffffU)
	{
		normalized = pc;
		return true;
	}
	if (pc <= ROM_MASK && m_maincpu->cs0_covers(0))
	{
		normalized = 0x00f80000U | (pc & ROM_MASK);
		return true;
	}
	return false;
}


asr10_boot_state::step0_region asr10_boot_state::classify_step0_region(u32 pc)
{
	pc &= 0x00ffffff;
	if (pc >= 0x00f80000U && pc <= 0x00fbffffU)
		return step0_region::ROM;
	if (pc >= 0x00fc6000U && pc <= 0x00fc67ffU)
		return step0_region::DPRAM;
	if (pc >= 0x00fc0000U && pc <= 0x00fc5fffU)
		return step0_region::PERIPHERAL;
	if (pc <= 0x000fffffU)
		return step0_region::LOW_RAM;
	if (pc >= 0x00ff8000U)
		return step0_region::HIGH_RAM;
	return step0_region::OTHER;
}


const char *asr10_boot_state::step0_region_name(step0_region region)
{
	switch (region)
	{
	case step0_region::ROM: return "ROM";
	case step0_region::DPRAM: return "DPRAM";
	case step0_region::PERIPHERAL: return "PERIPHERAL";
	case step0_region::LOW_RAM: return "LOW_RAM";
	case step0_region::HIGH_RAM: return "HIGH_RAM";
	case step0_region::OTHER: return "OTHER";
	case step0_region::COUNT: break;
	}
	return "UNKNOWN";
}


void asr10_boot_state::record_rom_handoff(u32 from, u32 to)
{
	if (!m_step0_runtime_trace_enabled)
		return;

	const step0_region from_region = classify_step0_region(from);
	const step0_region to_region = classify_step0_region(to);
	const u32 class_index = u32(from_region) * STEP0_REGION_COUNT + u32(to_region);
	const u32 count = m_step0_region_handoff_counts[class_index]++;
	if (count < STEP0_HANDOFF_SAMPLES_PER_CLASS)
	{
		region_handoff_sample &entry = m_step0_region_handoffs[class_index][count];
		entry.from = from;
		entry.to = to;
		entry.first_time = machine().time();
		osd_printf_info("ASR10_STEP0_REGION_HANDOFF event=sample class=%s_to_%s sample=%u time=%s from=%06x to=%06x\n",
			step0_region_name(from_region), step0_region_name(to_region), count,
			entry.first_time.to_string(), from, to);
	}
	else
		m_step0_region_handoff_truncated[class_index]++;
}


void asr10_boot_state::rom_handoff_summary()
{
	if (!m_step0_runtime_trace_enabled)
		return;

	for (u32 from = 0; from != STEP0_REGION_COUNT; from++)
	{
		for (u32 to = 0; to != STEP0_REGION_COUNT; to++)
		{
			const u32 class_index = from * STEP0_REGION_COUNT + to;
			const u32 count = m_step0_region_handoff_counts[class_index];
			if (!count)
				continue;
			osd_printf_info("ASR10_STEP0_REGION_HANDOFF_SUMMARY class=%s_to_%s count=%u stored=%u truncated=%u\n",
				step0_region_name(step0_region(from)), step0_region_name(step0_region(to)),
				count, std::min<u32>(count, STEP0_HANDOFF_SAMPLES_PER_CLASS),
				m_step0_region_handoff_truncated[class_index]);
			for (u32 sample = 0; sample < std::min<u32>(count, STEP0_HANDOFF_SAMPLES_PER_CLASS); sample++)
			{
				const region_handoff_sample &entry = m_step0_region_handoffs[class_index][sample];
				osd_printf_info("ASR10_STEP0_REGION_HANDOFF_SAMPLE class=%s_to_%s sample=%u time=%s from=%06x to=%06x\n",
					step0_region_name(step0_region(from)), step0_region_name(step0_region(to)),
					sample, entry.first_time.to_string(), entry.from, entry.to);
			}
		}
	}
}


void asr10_boot_state::note_step0_fdc_access(bool write)
{
	if (!m_step0_runtime_trace_enabled || machine().side_effects_disabled())
		return;
	start_pc_profile(m_step0_pc_profiles[0], "A_FDC_LOAD");
	for (pc_profile_window &window : m_step0_pc_profiles)
	{
		if (!window.active || window.done)
			continue;
		if (write)
			window.fdc_writes++;
		else
			window.fdc_reads++;
	}
}


void asr10_boot_state::step0_irq6_summary()
{
	if (!m_step0_runtime_trace_enabled)
		return;

	osd_printf_info("ASR10_STEP0_IRQ6_SUMMARY accepts=%u duart_pending=%u non_duart_or_unknown=%u "
		"last_imr=%02x thra_writes=%u thrb_writes=%u\n",
		m_step0_irq6_accept_count, m_step0_irq6_duart_pending_count,
		m_step0_irq6_non_duart_count, m_step0_duart_imr,
		m_step0_thra_writes, m_step0_thrb_writes);
	for (u32 value = 0; value != 256; value++)
	{
		if (m_step0_irq6_isr_hist[value])
			osd_printf_info("ASR10_STEP0_IRQ6_ISR_HIST isr=%02x count=%u share=%.6f\n",
				value, m_step0_irq6_isr_hist[value],
				m_step0_irq6_accept_count ? double(m_step0_irq6_isr_hist[value]) / double(m_step0_irq6_accept_count) : 0.0);
	}
	for (u32 bit = 0; bit != 8; bit++)
	{
		osd_printf_info("ASR10_STEP0_IRQ6_ISR_BIT bit=%u raw_count=%u masked_count=%u\n",
			bit, m_step0_irq6_isr_bit_hist[bit], m_step0_irq6_masked_bit_hist[bit]);
	}

	std::vector<std::pair<u32, u32>> thra_pcs;
	thra_pcs.reserve(m_step0_thra_write_pcs.size());
	for (const auto &entry : m_step0_thra_write_pcs)
		thra_pcs.emplace_back(entry.first, entry.second);
	std::sort(thra_pcs.begin(), thra_pcs.end(),
		[](const auto &a, const auto &b)
		{
			if (a.second != b.second)
				return a.second > b.second;
			return a.first < b.first;
		});
	for (size_t index = 0; index < std::min<size_t>(20, thra_pcs.size()); index++)
	{
		osd_printf_info("ASR10_STEP0_THRA_WRITE_PC rank=%zu pc=%06x count=%u\n",
			index + 1, thra_pcs[index].first, thra_pcs[index].second);
	}
}


void asr10_boot_state::rx_event_dump_lowmem()
{
	if (!m_rx_event_trace_enabled || m_rx_event_lowmem_dumped)
		return;

	m_rx_event_lowmem_dumped = true;
	osd_printf_info("ASR10_RX_EVENT event=lowmem_dump id=%u base=000000 len=0400\n", m_rx_event_id);
	for (u32 address = 0; address < 0x400; address += 0x10)
	{
		std::string hex;
		for (u32 offset = 0; offset < 0x10; offset++)
		{
			if (offset)
				hex += ' ';
			hex += util::string_format("%02x", lowmem_byte(address + offset));
		}
		osd_printf_info("ASR10_RX_EVENT_LOWMEM address=%04x hex=\"%s\"\n", address, hex.c_str());
	}
}


void asr10_boot_state::rx_event_dump_slots(const char *phase)
{
	if (!m_rx_event_trace_enabled)
		return;

	m_rx_event_slot_base = lowmem_word(0x00c6);
	m_rx_event_slot_end = lowmem_word(0x00c8);
	const u32 span = (m_rx_event_slot_end >= m_rx_event_slot_base) ? (m_rx_event_slot_end - m_rx_event_slot_base) : 0;
	m_rx_event_slot_count = span / 0x16;
	osd_printf_info("ASR10_RX_EVENT event=slot_table id=%u phase=%s base=%04x end=%04x "
		"span=%04x stride=0016 stride_source=f87fc2_adda slot_count=%u exact_division=%u\n",
		m_rx_event_id, phase, m_rx_event_slot_base, m_rx_event_slot_end, span,
		m_rx_event_slot_count, (span && (span % 0x16) == 0) ? 1 : 0);

	for (u32 slot = 0; slot < std::min<u32>(m_rx_event_slot_count, 128); slot++)
	{
		const u32 base = m_rx_event_slot_base + slot * 0x16;
		std::string words;
		for (u32 offset = 0; offset < 0x16; offset += 2)
		{
			if (offset)
				words += ' ';
			words += util::string_format("%04x", lowmem_word(base + offset));
		}
		osd_printf_info("ASR10_RX_EVENT_SLOT id=%u phase=%s slot=%u base=%04x "
			"b2=%02x b3=%02x ready=%u words=\"%s\"\n",
			m_rx_event_id, phase, slot, base, lowmem_byte(base + 2), lowmem_byte(base + 3),
			lowmem_byte(base + 2) != lowmem_byte(base + 3) ? 1 : 0, words.c_str());
	}
}


void asr10_boot_state::rx_event_note_slot_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask)
{
	if (!m_rx_event_trace_enabled || !m_rx_event_pre_inject_dumped || !m_rx_event_slot_count)
		return;
	if (byte_address < m_rx_event_slot_base || byte_address >= m_rx_event_slot_end)
		return;

	const u32 slot = (byte_address - m_rx_event_slot_base) / 0x16;
	if (slot >= m_rx_event_slot_activity.size())
		return;

	rx_event_slot_activity &activity = m_rx_event_slot_activity[slot];
	activity.writes++;
	activity.last_previous = previous;
	activity.last_current = current;
	activity.last_mem_mask = mem_mask;
	activity.last_write_pc = pc;
	osd_printf_info("ASR10_RX_EVENT event=slot_write id=%u time=%s pc=%06x slot=%u "
		"address=%04x previous=%04x current=%04x mem_mask=%04x b2=%02x b3=%02x ready=%u writes=%u\n",
		m_rx_event_id, machine().time().to_string(), pc, slot, byte_address, previous, current,
		mem_mask, lowmem_byte(m_rx_event_slot_base + slot * 0x16 + 2),
		lowmem_byte(m_rx_event_slot_base + slot * 0x16 + 3),
		lowmem_byte(m_rx_event_slot_base + slot * 0x16 + 2) != lowmem_byte(m_rx_event_slot_base + slot * 0x16 + 3) ? 1 : 0,
		activity.writes);
}


void asr10_boot_state::rx_event_note_instruction(u32 pc)
{
	if (!m_rx_event_trace_enabled || machine().side_effects_disabled())
		return;

	if (pc == 0x00ff8638)
	{
		osd_printf_info("ASR10_RX_EVENT event=pc_marker id=%u time=%s pc=ff8638\n",
			m_rx_event_id, machine().time().to_string());
	}

	if (pc == 0x00f884d4 || pc == 0x00f884e0 || pc == 0x00f884ec || pc == 0x00f884f4)
	{
		const char *branch =
			pc == 0x00f884d4 ? "RxRDYB_bit5" :
			pc == 0x00f884e0 ? "channel_A_status_bits1_2" :
			pc == 0x00f884ec ? "TxRDYA_bit0" : "counter_ready_bit3";
		const char *form = pc == 0x00f884f4 ? "jmp_abs_short" : "jmp_indirect_lowmem_long";
		const u32 target =
			pc == 0x00f884d4 ? (lowmem_long(0x00de) & 0x00ffffff) :
			pc == 0x00f884e0 ? (lowmem_long(0x00e2) & 0x00ffffff) :
			pc == 0x00f884ec ? (lowmem_long(0x00e6) & 0x00ffffff) :
			0x00ff8638U;
		osd_printf_info("ASR10_RX_EVENT event=dispatch id=%u time=%s pc=%06x branch=%s "
			"form=%s target=%06x ptr_de=%08x ptr_e2=%08x ptr_e6=%08x\n",
			m_rx_event_id, machine().time().to_string(), pc, branch, form, target,
			lowmem_long(0x00de), lowmem_long(0x00e2), lowmem_long(0x00e6));
	}

	if (pc == 0x00f87f9e && m_rx_event_slot_count)
	{
		const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
		if (a2 >= m_rx_event_slot_base && a2 < m_rx_event_slot_end)
		{
			const u32 slot = (a2 - m_rx_event_slot_base) / 0x16;
			if (slot < m_rx_event_slot_activity.size() && lowmem_byte(a2 + 2) != lowmem_byte(a2 + 3))
				m_rx_event_slot_activity[slot].ready_samples++;
		}
	}

	if (pc == 0x00f87fc0)
	{
		const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
		const u32 sp = u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff;
		const u32 slot = (m_rx_event_slot_count && a2 >= m_rx_event_slot_base && a2 < m_rx_event_slot_end)
			? (a2 - m_rx_event_slot_base) / 0x16
			: 0xffffffffU;
		const u16 frame_sr = read_program_word(sp);
		const u32 frame_pc = read_stack_long(sp + 2) & 0x00ffffff;
		osd_printf_info("ASR10_RX_EVENT event=scheduler_rte id=%u time=%s pc=%06x slot=%u "
			"slot_base=%06x frame_sr=%04x frame_pc=%06x frame_region=%s b2=%02x b3=%02x\n",
			m_rx_event_id, machine().time().to_string(), pc, slot, a2, frame_sr, frame_pc,
			address_region_guess(frame_pc), lowmem_byte(a2 + 2), lowmem_byte(a2 + 3));
	}
}


void asr10_boot_state::rx_event_summary()
{
	if (!m_rx_event_trace_enabled)
		return;

	osd_printf_info("ASR10_RX_EVENT_SUMMARY id=%u raw=%02x injected=%u display=\"%s\" "
		"slot_base=%04x slot_end=%04x slot_count=%u\n",
		m_rx_event_id, m_panel_sweep_raw, m_panel_sweep_injected ? 1 : 0, current_display_text().c_str(),
		m_rx_event_slot_base, m_rx_event_slot_end, m_rx_event_slot_count);
	for (u32 slot = 0; slot < std::min<u32>(m_rx_event_slot_count, 128); slot++)
	{
		const rx_event_slot_activity &activity = m_rx_event_slot_activity[slot];
		if (!activity.ready_samples && !activity.writes)
			continue;
		osd_printf_info("ASR10_RX_EVENT_SLOT_ACTIVITY id=%u slot=%u ready_samples=%u writes=%u "
			"last_write_pc=%06x previous=%04x current=%04x mem_mask=%04x\n",
			m_rx_event_id, slot, activity.ready_samples, activity.writes, activity.last_write_pc,
			activity.last_previous, activity.last_current, activity.last_mem_mask);
	}
}


void asr10_boot_state::note_panel_ring_store(u32 pc, u32 ring_address, u8 byte)
{
	if (machine().side_effects_disabled() || ring_address < 0x0378 || ring_address > 0x03b7)
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 return_pc = read_stack_long(sp) & 0x00ffffff;
	panel_byte_role role = panel_byte_role::SERIAL;

	if (is_bounded_panel_ring_control_candidate(pc, return_pc, m_last_distinct_pc))
	{
		role = panel_byte_role::RING_CONTROL;
		m_panel_diag_ring_control_role_count++;
		panel_submission_trace("role", "BoundedRingControl", byte);
	}

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

			if (m_step0_runtime_trace_enabled && !machine().side_effects_disabled())
			{
				if (m_step0_irq6_pending_landing)
				{
					osd_printf_info("ASR10_STEP0_IRQ6_LAND time=%s vector=%02x iack_pc=%06x "
						"target=%06x pc=%06x target_match=%u\n",
						machine().time().to_string(), m_step0_irq6_pending_vector,
						m_step0_irq6_pending_iack_pc, m_step0_irq6_pending_target, pc,
						pc == m_step0_irq6_pending_target ? 1 : 0);
					m_step0_irq6_pending_landing = false;
				}
				u32 normalized_from = m_last_pc;
				u32 normalized_to = pc;
				if (m_last_pc != 0xffffffffU)
				{
					(void)normalized_rom_handoff_source(m_last_pc, normalized_from);
					(void)normalized_rom_handoff_source(pc, normalized_to);
					if (classify_step0_region(normalized_from) != classify_step0_region(normalized_to))
						record_rom_handoff(normalized_from, normalized_to);
				}
				sample_pc_profile(m_step0_pc_profiles[0], pc);
				sample_pc_profile(m_step0_pc_profiles[1], pc);
			}
			if (m_panel_sweep_enabled && m_panel_sweep_waiting_sample && !m_panel_sweep_consumed &&
				!machine().side_effects_disabled())
			{
				if (pc == 0x00f884d4)
					m_panel_sweep_dispatch_target = lowmem_long(0x00de) & 0x00ffffff;
				else if (pc == 0x00f884e0)
					m_panel_sweep_dispatch_target = lowmem_long(0x00e2) & 0x00ffffff;
				else if (pc == 0x00f884ec)
					m_panel_sweep_dispatch_target = lowmem_long(0x00e6) & 0x00ffffff;
				else if (pc == 0x00f884f4)
					m_panel_sweep_dispatch_target = 0x00ff8638U;
			}
			rx_event_note_instruction(pc);

			if (m_root_directory_trace_enabled)
				record_root_directory_instruction(pc);

	if (pc == 0x00f89354)
		note_panel_descriptor_entry(pc);
	else if (pc == 0x00f8937c)
		note_panel_descriptor_return(pc);
}


void asr10_boot_state::note_panel_descriptor_entry(u32 pc)
{
	if (machine().side_effects_disabled())
		return;

	const u32 raw_a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
	const u32 identity = normalize_panel_descriptor_identity(raw_a2);
	const u8 depth_before = m_panel_descriptor_stack_depth;
	m_panel_diag_descriptor_entry_count++;

	if (m_panel_descriptor_stack_overflow_depth)
	{
		m_panel_descriptor_stack_overflow_depth++;
		m_panel_diag_descriptor_stack_overflow_count++;
		panel_descriptor_trace("entry_overflow_nested", pc, raw_a2, identity,
			depth_before, depth_before, "overflow");
		return;
	}

	if (depth_before >= PANEL_DESCRIPTOR_STACK_LIMIT)
	{
		m_panel_descriptor_stack_overflow_depth = 1;
		m_panel_diag_descriptor_stack_overflow_count++;
		panel_descriptor_trace("entry_overflow", pc, raw_a2, identity,
			depth_before, depth_before, "overflow");
		return;
	}

	m_panel_descriptor_stack_raw_a2[depth_before] = raw_a2;
	m_panel_descriptor_stack_identity[depth_before] = identity;
	m_panel_descriptor_stack_entry_pc[depth_before] = pc;
	m_panel_descriptor_stack_depth++;
	m_panel_descriptor_max_depth = std::max(m_panel_descriptor_max_depth, m_panel_descriptor_stack_depth);

	const bool outer = depth_before == 0;
	if (outer)
		m_panel_diag_descriptor_outer_entry_count++;
	else
		m_panel_diag_descriptor_nested_entry_count++;
	if (m_root_directory_trace_enabled)
	{
		if (outer && identity == 0x00f816c4)
		{
			m_root_directory_16c4_entry_count++;
			(void)0;
		}
		osd_printf_info("ASR10_ROOT_DIRECTORY_DESCRIPTOR event=entry pc=%06x raw_a2=%06x identity=%06x "
			"depth_before=%u class=%s panel=\"%s\"\n",
			pc, raw_a2, identity, depth_before, outer ? "outer" : "nested",
			m_panel_text);
	}

	panel_descriptor_trace("entry", pc, raw_a2, identity, depth_before,
		m_panel_descriptor_stack_depth, outer ? "outer" : "nested");
}


void asr10_boot_state::note_panel_descriptor_return(u32 pc)
{
	if (machine().side_effects_disabled())
		return;

	const u8 depth_before = m_panel_descriptor_stack_depth;
	m_panel_diag_descriptor_return_count++;

	if (m_panel_descriptor_stack_overflow_depth)
	{
		m_panel_descriptor_stack_overflow_depth--;
		panel_descriptor_trace("return_overflow", pc, 0, 0, depth_before,
			depth_before, "overflow");
		return;
	}

	if (!depth_before)
	{
		m_panel_diag_descriptor_unmatched_return_count++;
		panel_descriptor_trace("return_unmatched", pc, 0, 0, 0, 0, "unmatched");
		return;
	}

	const u32 raw_a2 = m_panel_descriptor_stack_raw_a2[depth_before - 1];
	const u32 identity = m_panel_descriptor_stack_identity[depth_before - 1];
	m_panel_descriptor_stack_raw_a2[depth_before - 1] = 0;
	m_panel_descriptor_stack_identity[depth_before - 1] = 0;
	m_panel_descriptor_stack_entry_pc[depth_before - 1] = 0;
	m_panel_descriptor_stack_depth--;

	const bool outer = m_panel_descriptor_stack_depth == 0;
	if (outer)
		m_panel_diag_descriptor_outer_return_count++;
	else
		m_panel_diag_descriptor_nested_return_count++;
	if (m_root_directory_trace_enabled)
	{
		osd_printf_info("ASR10_ROOT_DIRECTORY_DESCRIPTOR event=return pc=%06x raw_a2=%06x identity=%06x "
			"depth_before=%u depth_after=%u class=%s panel=\"%s\"\n",
			pc, raw_a2, identity, depth_before, m_panel_descriptor_stack_depth,
			outer ? "outer" : "nested", m_panel_text);
	}

	panel_descriptor_trace("return", pc, raw_a2, identity,
		depth_before, m_panel_descriptor_stack_depth, outer ? "outer" : "nested");
}


u32 asr10_boot_state::normalize_panel_descriptor_identity(u32 address)
{
	const u32 raw = address & 0x00ffffff;
	return raw < 0x8000 ? 0x00f80000 | raw : raw;
}


void asr10_boot_state::panel_descriptor_trace(const char *event, u32 pc, u32 raw_a2, u32 identity, u8 depth_before, u8 depth_after, const char *classification)
{
	if (!m_panel_submission_trace_enabled)
		return;
	if (m_panel_diag_descriptor_trace_count >= ASR10_PANEL_DESCRIPTOR_TRACE_LIMIT)
		return;
	m_panel_diag_descriptor_trace_count++;

	logerror("ASR10_PANEL_DESCRIPTOR event=%s pc=%06x raw_a2=%06x identity=%06x "
		"depth_before=%u depth_after=%u class=%s entries=%u returns=%u outer_entry=%u "
		"outer_return=%u nested_entry=%u nested_return=%u unmatched_return=%u overflow=%u "
		"path_a_begin=%u path_a_end=%u identity_match=%u identity_mismatch=%u\n",
		event, pc, raw_a2, identity, depth_before, depth_after, classification,
		m_panel_diag_descriptor_entry_count, m_panel_diag_descriptor_return_count,
		m_panel_diag_descriptor_outer_entry_count, m_panel_diag_descriptor_outer_return_count,
		m_panel_diag_descriptor_nested_entry_count, m_panel_diag_descriptor_nested_return_count,
		m_panel_diag_descriptor_unmatched_return_count, m_panel_diag_descriptor_stack_overflow_count,
		m_panel_diag_path_a_begin_count, m_panel_diag_path_a_end_count,
		m_panel_diag_path_a_identity_match_count, m_panel_diag_path_a_identity_mismatch_count);
}


void asr10_boot_state::panel_submission_trace(const char *event, const char *kind, u8 data)
{
	if (!m_panel_submission_trace_enabled)
		return;

	logerror("ASR10_PANEL_SUBMISSION event=%s kind=%s data=%02x pc=%06x previous_pc=%06x "
		"ring_control_roles=%u direct_begin=%u direct_end=%u\n",
		event, kind, data, m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff, m_last_distinct_pc,
		m_panel_diag_ring_control_role_count, m_panel_diag_direct_text_begin_count,
		m_panel_diag_direct_text_end_count);
}


void asr10_boot_state::panel_submission_summary()
{
	if (!m_panel_submission_trace_enabled)
		return;

	logerror("ASR10_PANEL_SUBMISSION_SUMMARY exact_descriptor_entries=%u exact_descriptor_returns=%u "
		"outer_entries=%u outer_returns=%u nested_entries=%u nested_returns=%u max_depth=%u "
		"stack_depth=%u overflow_depth=%u unmatched_returns=%u stack_overflows=%u "
		"path_a_begin=%u path_a_end=%u identity_match=%u identity_mismatch=%u "
		"descriptor_detail_logs=%u descriptor_detail_limit=%u ring_control_roles=%u "
		"direct_begin=%u direct_end=%u\n",
		m_panel_diag_descriptor_entry_count, m_panel_diag_descriptor_return_count,
		m_panel_diag_descriptor_outer_entry_count, m_panel_diag_descriptor_outer_return_count,
		m_panel_diag_descriptor_nested_entry_count, m_panel_diag_descriptor_nested_return_count,
		m_panel_descriptor_max_depth, m_panel_descriptor_stack_depth,
		m_panel_descriptor_stack_overflow_depth, m_panel_diag_descriptor_unmatched_return_count,
		m_panel_diag_descriptor_stack_overflow_count, m_panel_diag_path_a_begin_count,
		m_panel_diag_path_a_end_count, m_panel_diag_path_a_identity_match_count,
		m_panel_diag_path_a_identity_mismatch_count, m_panel_diag_descriptor_trace_count,
		ASR10_PANEL_DESCRIPTOR_TRACE_LIMIT, m_panel_diag_ring_control_role_count,
		m_panel_diag_direct_text_begin_count, m_panel_diag_direct_text_end_count);
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


void asr10_boot_state::record_root_directory_instruction(u32 pc)
{
	if (machine().side_effects_disabled())
		return;

	const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
	const u16 opcode = read_program_word(pc);
	const u32 slot = m_root_directory_history_pos % ROOT_DIRECTORY_HISTORY_LIMIT;
	m_root_directory_history_pc[slot] = pc;
	m_root_directory_history_opcode[slot] = opcode;
	m_root_directory_history_a0[slot] = u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff;
	m_root_directory_history_a2[slot] = a2;
	m_root_directory_history_d0[slot] = u32(m_maincpu->state_int(M68K_D0));
	m_root_directory_history_d1[slot] = u32(m_maincpu->state_int(M68K_D1));
	m_root_directory_history_sp[slot] = u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff;
	m_root_directory_history_pos = (m_root_directory_history_pos + 1) % ROOT_DIRECTORY_HISTORY_LIMIT;
	if (m_root_directory_history_count < ROOT_DIRECTORY_HISTORY_LIMIT)
		m_root_directory_history_count++;

	if (a2 != m_root_directory_last_a2)
	{
		const u32 change_slot = m_root_directory_a2_change_pos % ROOT_DIRECTORY_HISTORY_LIMIT;
		m_root_directory_a2_change_pc[change_slot] = pc;
		m_root_directory_a2_change_opcode[change_slot] = opcode;
		m_root_directory_a2_change_previous[change_slot] = m_root_directory_last_a2;
		m_root_directory_a2_change_current[change_slot] = a2;
		m_root_directory_a2_change_d0[change_slot] = u32(m_maincpu->state_int(M68K_D0));
		m_root_directory_a2_change_a0[change_slot] = u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff;
		m_root_directory_a2_change_pos = (m_root_directory_a2_change_pos + 1) % ROOT_DIRECTORY_HISTORY_LIMIT;
		if (m_root_directory_a2_change_count < ROOT_DIRECTORY_HISTORY_LIMIT)
			m_root_directory_a2_change_count++;
		m_root_directory_last_a2 = a2;
	}
}



void asr10_boot_state::log_root_directory_table_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask)
{
	if (!m_root_directory_trace_enabled || machine().side_effects_disabled())
		return;
	if (byte_address < 0x0544 || byte_address >= 0x0544 + 40 * 0x1a)
		return;

	const u32 rel = byte_address - 0x0544;
	if ((rel % 0x1a) != 0)
		return;

	m_root_directory_table_first_word_write_count++;
	if (m_root_directory_table_first_word_write_count > 80 &&
		(m_root_directory_table_first_word_write_count & (m_root_directory_table_first_word_write_count - 1)))
		return;

	const u32 index = rel / 0x1a;
	osd_printf_info("ASR10_ROOT_DIRECTORY_TABLE_WRITE count=%u pc=%06x previous_pc=%06x "
		"index=%u address=%06x previous_first_word=%04x current_first_word=%04x "
		"mem_mask=%04x name=\"%c%c%c%c%c%c%c%c%c%c%c%c%c\"\n",
		m_root_directory_table_first_word_write_count, pc, m_last_distinct_pc,
		index, byte_address, previous, current, mem_mask,
		lowmem_byte(byte_address + 2) >= 0x20 && lowmem_byte(byte_address + 2) <= 0x7e ? char(lowmem_byte(byte_address + 2)) : '.',
		lowmem_byte(byte_address + 3) >= 0x20 && lowmem_byte(byte_address + 3) <= 0x7e ? char(lowmem_byte(byte_address + 3)) : '.',
		lowmem_byte(byte_address + 4) >= 0x20 && lowmem_byte(byte_address + 4) <= 0x7e ? char(lowmem_byte(byte_address + 4)) : '.',
		lowmem_byte(byte_address + 5) >= 0x20 && lowmem_byte(byte_address + 5) <= 0x7e ? char(lowmem_byte(byte_address + 5)) : '.',
		lowmem_byte(byte_address + 6) >= 0x20 && lowmem_byte(byte_address + 6) <= 0x7e ? char(lowmem_byte(byte_address + 6)) : '.',
		lowmem_byte(byte_address + 7) >= 0x20 && lowmem_byte(byte_address + 7) <= 0x7e ? char(lowmem_byte(byte_address + 7)) : '.',
		lowmem_byte(byte_address + 8) >= 0x20 && lowmem_byte(byte_address + 8) <= 0x7e ? char(lowmem_byte(byte_address + 8)) : '.',
		lowmem_byte(byte_address + 9) >= 0x20 && lowmem_byte(byte_address + 9) <= 0x7e ? char(lowmem_byte(byte_address + 9)) : '.',
		lowmem_byte(byte_address + 10) >= 0x20 && lowmem_byte(byte_address + 10) <= 0x7e ? char(lowmem_byte(byte_address + 10)) : '.',
		lowmem_byte(byte_address + 11) >= 0x20 && lowmem_byte(byte_address + 11) <= 0x7e ? char(lowmem_byte(byte_address + 11)) : '.',
		lowmem_byte(byte_address + 12) >= 0x20 && lowmem_byte(byte_address + 12) <= 0x7e ? char(lowmem_byte(byte_address + 12)) : '.',
		lowmem_byte(byte_address + 13) >= 0x20 && lowmem_byte(byte_address + 13) <= 0x7e ? char(lowmem_byte(byte_address + 13)) : '.',
		lowmem_byte(byte_address + 14) >= 0x20 && lowmem_byte(byte_address + 14) <= 0x7e ? char(lowmem_byte(byte_address + 14)) : '.');
}


void asr10_boot_state::root_directory_summary()
{
	if (!m_root_directory_trace_enabled)
		return;
	(void)0;
	for (u32 index = 0; index != 16; index++)
		(void)0;
	osd_printf_info("ASR10_ROOT_DIRECTORY_SUMMARY descriptor_16c4_entries=%u "
		"direct_text_count=%u no_inst_seen=%u table_first_word_writes=%u "
		"nonzero_first_words=%u first_zero_index=%u post_loading_fdc_accesses=%u panel=\"%s\"\n",
		m_root_directory_16c4_entry_count, m_root_directory_direct_text_count,
		m_root_directory_no_inst_seen ? 1 : 0,
		m_root_directory_table_first_word_write_count, m_root_directory_nonzero_first_word_count,
		m_root_directory_first_zero_index, m_post_loading_fdc_access_count, m_panel_text);
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





bool asr10_boot_state::fc3000_verify_table_match() const
{
	// Narrow to the exact table whose outer retry eventually produces
	// ERROR 032 (proven: ffc896 compares $0e8e against this literal).
	// Without this gate the first capture was from an unrelated,
	// harmless mismatch on a different table earlier in boot.
	return m_lowmem_shadow[0x0e8e >> 1] == 0xfff9 && m_lowmem_shadow[(0x0e8e >> 1) + 1] == 0xbca0;
}


// filesystem-browser-map.md 4.26 TASK 6: narrows the HALL REVERB GPR
// transaction trace to exactly table base $0e8e==0x00010400, record type
// D3==1, record index D6==0 -- deliberately not "every effect object",
// per this round's explicit scope.
bool asr10_boot_state::hall_reverb_type1_record0_active() const
{
	return m_lowmem_shadow[0x0e8e >> 1] == 0x0001 && m_lowmem_shadow[(0x0e8e >> 1) + 1] == 0x0400 &&
		(u8(m_maincpu->state_int(M68K_D3)) & 0xff) == 1 &&
		(u32(m_maincpu->state_int(M68K_D6)) & 0xff) == 0;
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




TIMER_CALLBACK_MEMBER(asr10_boot_state::synth_68302_timer_irq)
{
	if constexpr (!ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ && !ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR)
		return;

	m_synth_68302_timer_irq_count++;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u8 sr_mask = (sr >> 8) & 7;
	const u8 irq_level = ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR ?
		ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL : ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ_LEVEL;
	const u16 fc6812 = m_m68302_internal_shadow[0x12 >> 1];
	const u16 fc6814_before = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816_before = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818 = m_m68302_internal_shadow[0x18 >> 1];
	const u16 fc684a = m_m68302_internal_shadow[0x4a >> 1];
	const u16 fc6850 = m_m68302_internal_shadow[0x50 >> 1];
	const u16 fc6852 = m_m68302_internal_shadow[0x52 >> 1];
	bool pulse = sr_mask <= 6;
	const bool dispatcher_context = m_runtime_dispatch_entry_count != 0 || (pc >= 0x00f87f40 && pc <= 0x00f87fd0);
	bool iack_vector_ready = true;
	const char *iack_delay_reason = "none";
	bool iack_fire_ready = true;
	const char *iack_skip_reason = "none";

	if constexpr (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR)
	{
		if (m_seen_loading_system_prompt)
			(void)0;

		if (!m_seen_loading_system_prompt)
		{
			iack_vector_ready = false;
			iack_delay_reason = "loading_system_not_seen";
		}
		else if (!dispatcher_context)
		{
			iack_vector_ready = false;
			iack_delay_reason = "dispatcher_not_active";
		}
		else if (sr_mask != 0)
		{
			iack_vector_ready = false;
			iack_delay_reason = "sr_mask_not_zero";
		}
		else if (fc6850 == 0)
		{
			iack_vector_ready = false;
			iack_delay_reason = "fc6850_zero";
		}

		if (!iack_vector_ready)
		{
			pulse = false;
			m_synth_68302_timer_iack_delay_count++;
			if (m_synth_68302_timer_iack_delay_count <= 32 ||
				!(m_synth_68302_timer_iack_delay_count & (m_synth_68302_timer_iack_delay_count - 1)))
			{
				logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_DELAY count=%u reason=%s "
					"pc=%06x sr=%04x sr_mask=%u dispatcher_count=%u dispatcher_context=%u "
					"fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x fc684a=%04x fc6850=%04x fc6852=%04x "
					"panel=\"%s\"\n",
					m_synth_68302_timer_iack_delay_count, iack_delay_reason, pc, sr, sr_mask,
					m_runtime_dispatch_entry_count, dispatcher_context ? 1 : 0,
					fc6812, fc6814_before, fc6816_before, fc6818, fc684a, fc6850, fc6852, m_panel_text);
			}
		}
		else
		{
			if (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT && m_synth_68302_timer_iack_fire_count != 0)
			{
				iack_fire_ready = false;
				iack_skip_reason = "oneshot_already_fired";
			}
			else if (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR &&
				(fc6816_before & ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK))
			{
				iack_fire_ready = false;
				iack_skip_reason = "source_still_in_service";
			}
			else if (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP != 0 &&
				m_synth_68302_timer_iack_fire_count != 0 &&
				(m_synth_68302_timer_irq_count - m_synth_68302_timer_iack_last_fire_callback) <
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP)
			{
				iack_fire_ready = false;
				iack_skip_reason = "min_callback_gap";
			}

			if (!iack_fire_ready)
			{
				pulse = false;
				m_synth_68302_timer_iack_skip_count++;
				if (m_synth_68302_timer_iack_skip_count <= 32 ||
					!(m_synth_68302_timer_iack_skip_count & (m_synth_68302_timer_iack_skip_count - 1)))
				{
					logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SKIP skip_count=%u callback=%u reason=%s "
						"pc=%06x sr=%04x sr_mask=%u dispatcher_count=%u fire_count=%u last_fire_callback=%u "
						"min_callback_gap=%u fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x "
						"fc684a=%04x fc6850=%04x fc6852=%04x panel=\"%s\"\n",
						m_synth_68302_timer_iack_skip_count, m_synth_68302_timer_irq_count, iack_skip_reason,
						pc, sr, sr_mask, m_runtime_dispatch_entry_count, m_synth_68302_timer_iack_fire_count,
						m_synth_68302_timer_iack_last_fire_callback,
						ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP,
						fc6812, fc6814_before, fc6816_before, fc6818, fc684a, fc6850, fc6852, m_panel_text);
				}
			}
			else
			{
				m_m68302_internal_shadow[0x14 >> 1] = fc6814_before | ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK;
			}

			if (!m_synth_68302_timer_iack_armed_logged)
			{
				logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ARMED pc=%06x sr=%04x sr_mask=%u "
					"dispatcher_count=%u fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x "
					"fc684a=%04x fc6850=%04x fc6852=%04x vector_byte=%02x source_mask=%04x "
					"oneshot=%u wait_for_service_clear=%u min_callback_gap=%u panel=\"%s\"\n",
					pc, sr, sr_mask, m_runtime_dispatch_entry_count, fc6812, fc6814_before,
					fc6816_before, fc6818, fc684a, fc6850, fc6852,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_ONESHOT ? 1 : 0,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_WAIT_FOR_SERVICE_CLEAR ? 1 : 0,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_MIN_CALLBACK_GAP, m_panel_text);
				m_synth_68302_timer_iack_armed_logged = true;
			}
		}
	}
	const u16 fc6814_after = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816_after = m_m68302_internal_shadow[0x16 >> 1];

	logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ callback=%u line=%u irq_level=%u state=%s pc=%06x "
		"sr=%04x sr_mask=%u irq_level_above_mask=%u raw_irq_enabled=%u iack_vector_enabled=%u "
		"iack_vector_ready=%u iack_fire_ready=%u iack_delay_reason=%s iack_skip_reason=%s "
		"iack_vector_byte=%02x iack_source_mask=%04x iack_fire_count=%u iack_last_fire_callback=%u fc6812=%04x "
		"fc6814_before=%04x fc6814_after=%04x fc6816_before=%04x fc6816_after=%04x fc6818=%04x "
		"fc684a=%04x fc6850=%04x fc6852_reference=%04x dispatcher_count=%u panel=\"%s\"\n",
		m_synth_68302_timer_irq_count, irq_level, irq_level, pulse ? "HOLD_LINE" : "masked_no_pulse", pc,
		sr, sr_mask, irq_level > sr_mask ? 1 : 0,
		ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ ? 1 : 0,
		ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR ? 1 : 0,
		iack_vector_ready ? 1 : 0, iack_fire_ready ? 1 : 0, iack_delay_reason, iack_skip_reason,
		ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE,
		ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK,
		m_synth_68302_timer_iack_fire_count, m_synth_68302_timer_iack_last_fire_callback,
		fc6812, fc6814_before, fc6814_after, fc6816_before, fc6816_after, fc6818,
		fc684a, fc6850, fc6852, m_runtime_dispatch_entry_count, m_panel_text);

	if (pulse)
	{
		if constexpr (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR)
		{
			m_synth_68302_timer_iack_fire_count++;
			m_synth_68302_timer_iack_last_fire_callback = m_synth_68302_timer_irq_count;
			if (!m_synth_68302_timer_iack_fired_logged)
			{
				logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_FIRED pc=%06x sr=%04x sr_mask=%u "
					"dispatcher_count=%u irq_level=%u vector_byte=%02x fire_count=%u callback=%u fc6812=%04x fc6814_before=%04x "
					"fc6814_after=%04x fc6816_before=%04x fc6816_after=%04x fc6818=%04x "
					"fc684a=%04x fc6850=%04x fc6852=%04x panel=\"%s\"\n",
					pc, sr, sr_mask, m_runtime_dispatch_entry_count, irq_level,
					ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE,
					m_synth_68302_timer_iack_fire_count, m_synth_68302_timer_irq_count,
					fc6812, fc6814_before, fc6814_after, fc6816_before, fc6816_after,
					fc6818, fc684a, fc6850, fc6852, m_panel_text);
				m_synth_68302_timer_iack_fired_logged = true;
			}
		}
		(void)0;
		m_maincpu->set_input_line(irq_level, HOLD_LINE);
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
			return ASR10_FAKE_SCSI_INSTALLED ? "status_control_candidate_fake_installed" : "status_control_candidate_no_scsi";
		if ((address & 0x1f) == 0x03)
			return ASR10_FAKE_SCSI_INSTALLED ? "data_scratch_candidate_fake_installed" : "data_scratch_candidate_no_scsi";
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
	if (m_panel_c_parser_trace_active && pc != m_panel_c_parser_trace_last_pc)
	{
		m_panel_c_parser_trace_last_pc = pc;
		m_panel_c_parser_trace_count++;
		(void)0;
		if (pc == 0x00f89a9a)
			panel_c_parser_trace_stop("reached_f89a9a", pc);
		else if (pc == 0x00f89aec)
			panel_c_parser_trace_stop("reached_f89aec", pc);
		else if (pc == 0x00ffb3e4)
			panel_c_parser_trace_stop("reached_ffb3e4", pc);
		else if (pc == 0x00ffb424)
			panel_c_parser_trace_stop("reached_ffb424", pc);
		else if (m_panel_c_parser_trace_count >= 100)
			panel_c_parser_trace_stop("budget_100", pc);
		else if (m_panel_c_parser_trace_count > 2 &&
			!((pc >= 0x00ffb200 && pc <= 0x00ffb460) ||
				(pc >= 0x00f89a80 && pc <= 0x00f89ad0)))
			panel_c_parser_trace_stop("left_parser_region", pc);
	}
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
	// Read-only: direct counters for the trap #7/#8 handler entry points
	// themselves (not gated on slot5 specifically), to settle whether either
	// trap fires again at all after the initial six-slot RTE burst,
	// independent of what m_f87f96_queue_rte_count or the slot's own
	// queue_word show (a stable byte2==byte3 result is consistent with both
	// "never called again" and "called repeatedly with a stable outcome").
	// Supporting/scheduler-shape diagnostic only -- see ASR10_EXPERIMENT_
	// TUNING_STALL_TRACE gate; the load-bearing findings for the TUNING KBD
	// investigation come from the pre-existing Channel B/FDC hooks, not these.
	if (m_tuning_stall_trace_enabled && pc == 0x00f88108)
	{
		m_tuning_stall_trap7_count++;
		if (m_tuning_stall_trap7_count <= 20 || !(m_tuning_stall_trap7_count & (m_tuning_stall_trap7_count - 1)))
			logerror("ASR10_TUNING_STALL_TRAP7_ENTRY count=%u pc=%06x a2=%06x d0=%08x rte_count=%u\n",
				m_tuning_stall_trap7_count, pc, u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff,
				u32(m_maincpu->state_int(M68K_D0)), m_f87f96_queue_rte_count);
	}
	else if (m_tuning_stall_trace_enabled && pc == 0x00f8812c)
	{
		m_tuning_stall_trap8_count++;
		if (m_tuning_stall_trap8_count <= 20 || !(m_tuning_stall_trap8_count & (m_tuning_stall_trap8_count - 1)))
			logerror("ASR10_TUNING_STALL_TRAP8_ENTRY count=%u pc=%06x d0=%08x rte_count=%u\n",
				m_tuning_stall_trap8_count, pc, u32(m_maincpu->state_int(M68K_D0)), m_f87f96_queue_rte_count);
	}
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

	// The uPD72069 sees this child connector as drive 0 via the conventional "fdc:0" tag.
	// Mounted HFE media changes Recalibrate/Sense from 68,00 (not ready) to 20,00.
	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);

	// U20, per docs/hardware-identity.md. X1 is derived, not a separate
	// crystal: the board has no 3.6864MHz part (Y1=16MHz, Y2=30.476MHz,
	// Y3=33.8688MHz), and ES5701's own reconstruction (sources/es5701.vhd)
	// divides the 16MHz system clock by two on-chip; docs/asr10/PLAN.md
	// section 3 hypothesizes a further /2 (spare 74HC74/74F74 flip-flops
	// on the board) yields 16/4 = 4.000MHz into X1, matching the 4MHz X1
	// this same SCN2681 runs at on esqkt.cpp/esq5505.cpp. Channel A (MIDI)
	// and channel B (front panel) are not wired to anything yet -- the
	// panel byte transport still runs through the hand-written taps in
	// duart_panel_asr_candidate_r/w until a real esqpanel-style receiver
	// exists. The IRQ output, however, is a real pin on a real device --
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

	es5506_host.read_port_cb().set(FUNC(asr10_boot_state::es5506_host_read_par_diag));

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

	config.set_default_layout(layout_asr10_boot);
}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


// CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", MACHINE_NO_SOUND)
CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", 0)
