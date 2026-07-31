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
	static constexpr u32 ASR10_PANEL_L_MAX_REPLIES = 96;
	static constexpr u32 ASR10_PANEL_L_MAX_ZERO_CROSSINGS = 8;
	static constexpr u64 ASR10_PANEL_L_MAX_CYCLES_AFTER_LATER_START = 20'000'000;

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
	u16 m_es550x_vfx_shadow[0x40]{};
	u16 m_es5510_vfx_shadow[0x100]{};
	u16 m_fdc_vfx_shadow[4]{};
	u16 m_es5506_ts_shadow[0x40]{};
	u16 m_es5510_ts_shadow[0x100]{};
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
	bool m_dispatcher_rte_first_pc_logged = false;
	bool m_dispatcher_rte_candidate_dump_007308_logged = false;
	bool m_dispatcher_rte_candidate_dump_f8d020_logged = false;
	bool m_dispatcher_rte_candidate_dump_f8d05a_logged = false;
	bool m_dispatcher_rte_candidate_dump_f88f06_logged = false;
	bool m_dispatcher_rte_candidate_dump_f88f22_logged = false;
	bool m_dispatcher_rte_candidate_dump_f8d072_logged = false;
	// TUNING KBD stall investigation: one-shot code dumps for the loaded-runtime
	// callback PCs observed in the final RTE burst (slots 1/3/0/4/5) immediately
	// before the dispatcher goes idle forever. These are runtime-loaded (not ROM)
	// so they cannot be read from a static ROM disassembly. Gated as a group
	// behind ASR10_EXPERIMENT_TUNING_STALL_TRACE -- supporting/scheduler-shape
	// diagnostics only; the load-bearing findings for this investigation come
	// from the pre-existing Channel B/FDC hooks, not from these.
	bool m_tuning_stall_trace_enabled = false;
	bool m_tuning_stall_dump_ffc85a_logged = false;
	bool m_tuning_stall_dump_ff9106_logged = false;
	bool m_tuning_stall_dump_00ae14_logged = false;
	bool m_tuning_stall_dump_0068a8_logged = false;
	bool m_tuning_stall_dump_00779c_logged = false;
	bool m_tuning_stall_dump_trap_vectors_logged = false;
	bool m_tuning_stall_dump_7cc4_logged = false;
	bool m_tuning_stall_dump_7164_logged = false;
	bool m_tuning_stall_dump_bf28_logged = false;
	bool m_tuning_stall_dump_bf5a_logged = false;
	// Item 6/7 follow-through (filesystem-browser-map.md PASS 2): resolve
	// slot 0's six jump-vector targets, slot 4's five high-view targets,
	// f894a4's own internal vector calls, and slots 1/3's own resume code,
	// all via read_highview_word()/dump_highview_code_range() instead of
	// the 0xffff placeholder path. One-shot state for these is table-driven
	// -- see FSB_DUMP_TARGETS in the .cpp -- rather than one bool per
	// target. Gated on m_fsb.enabled, not m_tuning_stall_trace_enabled,
	// since this is this task's own instrumentation.
	u32 m_tuning_stall_save_before_count = 0;
	u32 m_tuning_stall_save_after_count = 0;
	u32 m_tuning_stall_trap7_count = 0;
	u32 m_tuning_stall_trap8_count = 0;
	bool m_dispatcher_rte_iack_seen = false;
	u32 m_queue_rte_after_count = 0;
	u32 m_queue_rte_before_pc = 0xffffffff;
	u32 m_queue_rte_last_return_pc = 0xffffffff;
	u32 m_dispatcher_rte_frame_pc = 0xffffffff;
	u32 m_dispatcher_rte_frame_sp = 0xffffffff;
	u32 m_dispatcher_rte_frame_a2 = 0xffffffff;
	u32 m_dispatcher_rte_frame_slot = 0xffffffff;
	u16 m_dispatcher_rte_frame_sr = 0;
	u16 m_dispatcher_rte_current_sr = 0;
	u16 m_dispatcher_rte_fc6814 = 0;
	u16 m_dispatcher_rte_fc6816 = 0;
	u16 m_dispatcher_rte_fc6818 = 0;
	u8 m_dispatcher_rte_iack_vector = 0xff;
	u8 m_dispatcher_rte_iack_level = 0xff;
	u32 m_dispatcher_rte_iack_pc = 0xffffffff;
	u16 m_dispatcher_rte_iack_sr = 0;

	// Filesystem/browser live verification (docs/asr10/filesystem-browser-map.md,
	// section 6). Off by default (ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE).
	// Adds genuine (non-sampled) read/write logging for the candidate
	// DIR/FAT/cache state fields identified by static tracing, plus
	// entry-proxy hooks for the mapped routines. Opcode-fetch read taps are
	// already established (see the four `m_hook_*_tap` installs above) to
	// never fire for this core's instruction-fetch path, so each "function
	// entry" hook below is instead a genuine *data* access tap on that
	// routine's first (or first distinguishing) memory reference, gated on
	// the exact PC of that instruction -- the same technique already used
	// for PANEL_ENQUEUE (pc==0xf89a7a/0xf89a8a inside the lowmem write path).
	// All mutable runtime state for this diagnostic lives in one struct;
	// the name/address/semantic tables it indexes into (FSB_FIELDS,
	// FSB_ENTRIES, FSB_DUMP_TARGETS) are file-scope constexpr in the .cpp.
	enum fsb_field_id : u32
	{
		FSB_FIELD_04FE = 0,
		FSB_FIELD_050C,
		FSB_FIELD_040E,
		FSB_FIELD_0406,
		FSB_FIELD_049A_049B,
		FSB_FIELD_040A,
		FSB_FIELD_04B2_VALIDITY, // validity gate flag read by f894a4 (f89494)
		FSB_FIELD_04B3_MODE,     // mode field written by fb8938/40/48 -- same
		                         // physical word as 04B2_VALIDITY, tracked
		                         // separately (see docs, item 2/item 7)
		FSB_FIELD_04AC,
		FSB_FIELD_04BE_04BF,
	};
	enum fsb_entry_id : u32
	{
		FSB_ENTRY_FB82A4 = 0,
		FSB_ENTRY_FB846A,
		FSB_ENTRY_FB895A,
		FSB_ENTRY_FB8C6E,
		FSB_ENTRY_F894A4_ROUTINE, // true entry proxy: f89494 reads $4b2.w
		FSB_ENTRY_F894A4_TABLE,   // table-reached proxy only: f894b4
		FSB_ENTRY_FDC_CMD,
	};
	struct fsb_field_runtime
	{
		u16 value = 0;
		bool seen = false;
		u32 last_write_pc = 0xffffffff;
		u32 last_read_pc = 0xffffffff;
		u32 write_count = 0;
		u32 read_count = 0;
	};
	struct fsb_state
	{
		static constexpr u32 FIELD_COUNT = 10;
		static constexpr u32 ENTRY_COUNT = 7;
		static constexpr u32 DUMP_COUNT = 19;

		bool enabled = false;
		std::array<fsb_field_runtime, FIELD_COUNT> fields{};
		std::array<u32, ENTRY_COUNT> entry_counts{};
		std::array<bool, DUMP_COUNT> dump_logged{};

		// $0544 is a 40-entry table, not a fixed-width scalar, so its
		// read/write history is tracked with entry-index/byte-offset
		// decoding instead of a plain fsb_field_runtime.
		u32 table_write_count = 0;
		u32 table_read_count = 0;
		u32 table_last_write_pc = 0xffffffff;
		u32 table_last_read_pc = 0xffffffff;
		u32 table_last_write_entry = 0xffffffff;
		u32 table_last_read_entry = 0xffffffff;

		// Milestones A-E: content/PC-triggered, one-shot.
		bool milestone_a_logged = false;
		bool milestone_b_logged = false;
		bool milestone_c_logged = false;
		bool milestone_e_logged = false;
	};
	fsb_state m_fsb;

	// Post-tuning indirect-call / trap #9 verification
	// (docs/asr10/filesystem-browser-map.md section 4.7's smallest edge).
	// Off by default. Every hook here is gated on both m_pti.enabled and
	// m_pti.seen_f880fc (the KEYBOARD TUNED finalizer already tracked by
	// m_fsb.milestone_c_logged, tracked again independently here so this
	// diagnostic does not depend on ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE
	// also being set). `jsr (An)` genuinely pushes a return address (a real
	// memory write, tappable via the established pc-gated technique inside
	// lowmem_w); `jmp (An)` does not push anything and has no memory
	// reference of its own, so it cannot be tapped this way -- of the ~69
	// ROM-wide indirect sites found in section 4.1's enumeration, only the
	// `jsr (An)` ones (PTI_INDIRECT_SITES below) are instrumented, and this
	// is reported as a real coverage gap, not silently ignored.
	static constexpr u32 PTI_INDIRECT_SITE_COUNT = 19;
	// Slot 0's six named vectors reached via `jsr $xxxx.w` from its ae18
	// resume routine and the adjacent ae22 routine (see the authoritative
	// disassembly in filesystem-browser-map.md 4.9). Index order matches
	// the task's own list: 0=87f2, 1=bc8e, 2=a26e, 3=8864, 4=8bb6 (JMP
	// only -- see below), 5=9650. $a26e has two distinct jsr sites
	// (ae5e, ae80); both increment index 2, distinguished in the log by
	// source PC. $8bb6 (index 4) is reached only via `jmp $8bb6.w`
	// (ae8a), which pushes no return address and cannot be tapped the
	// same way -- its counter is incremented only by inference (the
	// preceding `bsr $ae8e` at ae88 returning normally to ae8a).
	static constexpr u32 PTI_VECTOR_COUNT = 6;
	struct pti_state
	{
		bool enabled = false;
		bool seen_f880fc = false;
		std::array<u32, PTI_INDIRECT_SITE_COUNT> indirect_hit_counts{};
		u32 trap9_hit_count = 0;
		bool vector41_logged = false;
		bool target_002b38_dump_logged = false;
		std::array<u32, PTI_VECTOR_COUNT> vector_hit_counts{};
		std::array<u32, PTI_VECTOR_COUNT> vector_return_counts{};
		// 5 branch-guard taps: 0=ae34 (D2 sign, gates the ae38 bmi split),
		// 1=ae56 ($183.w, gates ae5c bne / a26e site 1), 2=ae62 ($31c.w,
		// gates ae66 beq / 8864), 3=ae72 ($183.w again, gates ae78 bne /
		// a26e site 2 first check), 4=ae7a ($306.w, gates ae7e beq / a26e
		// site 2 second check).
		std::array<u32, 5> gate_hit_counts{};
		// 002b14 node-classify verification (filesystem-browser-map.md
		// 4.10): 0=002b1a ($035e.w vs A5), 1=002b26 ($031c.w -> D5).
		std::array<u32, 2> node_gate_hit_counts{};
		u32 trap4_hit_count = 0;
		bool trap4_vector_logged = false;
		// 007000 investigation (filesystem-browser-map.md 4.11): trap #2
		// entry/return, the two node-field writes, trap #12/#14 issuance.
		// Vectors 34/44/46 are dumped once, unconditionally at the
		// f880fc gate (not gated on 007000 actually executing), so their
		// handler addresses are known even if 007000 never runs.
		bool vectors_34_44_46_logged = false;
		u32 trap2_entry_count = 0;
		u32 trap2_return_count = 0;
		u32 node_write_1a_count = 0;
		u32 node_write_a6_count = 0;
		u32 trap12_count = 0;
		u32 trap14_count = 0;
		u32 trap2_a5_before = 0xffffffff;
		// trap #13 caller cross-reference (filesystem-browser-map.md 4.13):
		// 0=f883ac, 1=f88df8, 2=f89b54, all sharing A1 sourced from lowmem
		// $dc.w per static analysis. Return sites: 0=f883ae (write tap),
		// 1=f88dfa (write tap), 2=f89b56 (rts stack-pop read tap, this
		// call site has no write instruction immediately after the trap).
		std::array<u32, 3> trap13_call_counts{};
		std::array<u32, 3> trap13_return_counts{};
		std::array<u32, 3> trap13_a5_before{};
		std::array<u32, 3> trap13_a1_before{};
		std::array<u32, 3> trap13_queue_head_before{};
		std::array<u32, 3> trap13_queue_tail_before{};
		std::array<u32, 3> trap13_queue_count_before{};
		std::array<u32, 3> trap13_queue_gate_before{};
		u32 dispatcher_f87f82_hit_count = 0;
		u32 dc_pointer_write_count = 0;
		u32 dc_pointer_read_count = 0;
		bool full_vector_scan_logged = false;
		// filesystem-browser-map.md 4.14: per-entry tracking for every
		// static path into f87f3e/f87f66/f87f80. "last_entry" is stashed
		// by whichever entry tap fires (trap #1/#5/#6/#7/#15 direct, or
		// one of f87f3e's five direct callers) and *consumed* (cleared)
		// by the next downstream tap that reads it (f87f3e's own body
		// entry, or f87f80/f87f82 for paths that skip f87f3e) -- so a
		// correlation is only reported when a fresh, not-yet-consumed
		// entry event actually precedes it, never by nearest-line timing.
		bool last_entry_valid = false;
		bool last_entry_consumed = false;
		u32 last_entry_id = 0;
		u32 last_entry_pc = 0;
		std::array<u32, 11> entry_hit_counts{}; // indexed by entry id 1-10 (0 unused)
		u32 f87f3e_body_entry_count = 0;
		u32 f87f82_seq = 0;
		u32 trap1_hit_count = 0;
		u32 trap15_hit_count = 0;
		u32 trap5_hit_count = 0;
		u32 trap6_hit_count = 0;
		u32 trap7_hit_count = 0;
		// trap #15 before/after slot +2/+3 (task 4)
		u32 trap15_slot_before2 = 0;
		u32 trap15_slot_before3 = 0;
		// filesystem-browser-map.md 4.15: early-epoch instrumentation, all
		// gated purely on `enabled` (NOT seen_f880fc, which is proven too
		// late -- it is a PC inside trap #6's own body).
		u32 trap8_hit_count = 0;
		u32 trap9_epoch_hit_count = 0;
		u32 ca_list_decrement_count = 0;
		u32 ca_list_callback_count = 0;
		u32 slot0_ready_bit_write_count = 0;
		// filesystem-browser-map.md 4.16: node+2=0x16 and 0xd10a producers,
		// and the $d6.w destination cell (parallel to $dc.w in 4.13).
		std::array<u32, 3> node16_producer_hit_counts{}; // 0=f88e2a 1=f9068a 2=f942f2
		u32 d10a_producer_hit_count = 0;
		u32 d6_pointer_write_count = 0;
		u32 d6_pointer_read_count = 0;
		// filesystem-browser-map.md 4.17
		u32 ca_root_write_count = 0;
		std::array<u32, 2> timer_field_write_counts{}; // 0=14d4 (word), 1=14d6 (long, 2 words)
		std::array<u32, 5> family12_entry_counts{}; // 0=12efc 1=12f24 2=12f66 3=12f76 4=12f7e
		// filesystem-browser-map.md 4.18: ae10-ae20 per-iteration capture.
		u32 ae12_seq = 0;
		u32 ae1a_seq = 0;
		u32 ae20_seq = 0;
	};
	pti_state m_pti;
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
	bool m_panel_c_reply_71_zero_enabled = false;
	bool m_panel_c_reply_71_zero_armed = false;
	bool m_panel_c_reply_71_zero_injected = false;
	bool m_panel_d1_reply_71_ff_enabled = false;
	bool m_panel_d1_reply_71_ff_armed = false;
	bool m_panel_d1_reply_71_ff_injected = false;
	bool m_panel_d2_reply_71_7e_ff_enabled = false;
	bool m_panel_d2_reply_71_armed = false;
	bool m_panel_d2_reply_71_injected = false;
	bool m_panel_d2_reply_7e_armed = false;
	bool m_panel_d2_reply_7e_injected = false;
	bool m_panel_e_ff_drain_known_ring_enabled = false;
	bool m_panel_e_ff_drain_aborted = false;
	bool m_panel_e_ff_drain_done = false;
	bool m_panel_e_ff_drain_ready_valid = false;
	bool m_panel_e_ff_drain_waiting_completion = false;
	bool m_panel_e_f89abe_logged = false;
	bool m_panel_e_f89ac2_logged = false;
	u8 m_panel_e_ff_drain_next_index = 0;
	u8 m_panel_e_ff_drain_ready_index = 0;
	u8 m_panel_e_ff_drain_pending_index = 0;
	u8 m_panel_e_ff_drain_pending_count = 0;
	bool m_panel_l_ff_drain_later_rings_enabled = false;
	bool m_panel_l_aborted = false;
	bool m_panel_l_done = false;
	bool m_panel_l_first_ring_setup_done = false;
	bool m_panel_l_later_started = false;
	bool m_panel_l_waiting_completion = false;
	bool m_panel_l_previous_completion_observed = true;
	bool m_panel_l_zero_pending_f89ac2 = false;
	bool m_panel_l_stop_after_dispatch = false;
	bool m_panel_l_final_idle_probe_queued = false;
	bool m_panel_l_final_idle_probe_waiting = false;
	bool m_panel_l_final_idle_f89a9a_zero_seen = false;
	bool m_panel_l_final_idle_f89ace_seen = false;
	bool m_panel_l_final_idle_03c5_clear_seen = false;
	u8 m_panel_l_first_ring_index = 0;
	u8 m_panel_l_pending_count = 0;
	u8 m_panel_l_pending_byte = 0;
	u8 m_panel_l_previous_byte = 0xff;
	u8 m_panel_l_current_byte = 0xff;
	u16 m_panel_l_zero_slot0_before = 0;
	u16 m_panel_l_zero_slot0_after = 0;
	u32 m_panel_l_reply_count = 0;
	u32 m_panel_l_zero_crossing_count = 0;
	u32 m_panel_l_pending_thrb_pc = 0xffffffffU;
	u32 m_panel_l_current_thrb_pc = 0xffffffffU;
	u64 m_panel_l_later_start_cycle = 0;
	bool m_panel_autorespond_enabled = false;
	u32 m_panel_autorespond_scheduled_count = 0;
	u32 m_panel_autorespond_injected_count = 0;
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
	bool m_duart_counter_timer_enabled = false;
	bool m_divzero_frame_logged = false;
	bool m_es5506_host_enabled = false;
	std::array<u8, 64> m_es5506_host_seen_mask{}; // bit0=read seen, bit1=write seen, per device offset
	u32 m_es5506_host_access_count = 0;
	bool m_es5510_host_enabled = false;
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
	bool m_es5506_diag_par_enabled = false;
	u16 m_es5506_diag_par_value = 0x200;
	u32 m_es5506_diag_par_read_count = 0;
	bool m_primary_slot_snapshot_logged = false;
	bool m_panel_c_parser_trace_enabled = false;
	bool m_panel_c_parser_trace_active = false;
	bool m_panel_c_parser_trace_done = false;
	bool m_panel_c_rx_valid = false;
	bool m_panel_c_irq6_asserted = false;
	u8 m_panel_c_rx_byte = 0;
	u8 m_panel_c_srb = 0;
	u8 m_panel_c_isr = 0;
	u8 m_panel_c_imr = 0;
	u32 m_panel_c_parser_trace_count = 0;
	u32 m_panel_c_parser_trace_last_pc = 0xffffffffU;
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
	u16 es550x_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es550x_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5510_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5510_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 fdc_vfx_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void fdc_vfx_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5506_ts_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5506_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5510_ts_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void es5510_ts_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(pc_poll);
	TIMER_CALLBACK_MEMBER(prompt_select_poll);
	TIMER_CALLBACK_MEMBER(synth_68302_timer_irq);
	TIMER_CALLBACK_MEMBER(panel_autorespond_fire);
	TIMER_CALLBACK_MEMBER(lrclk_toggle);
	u8 maincpu_iack_r(u8 level);

	bool probe_or_alias_region_index(u32 address, u32 &index, u32 &word_index) const;
	u16 probe_or_alias_region_r_at(u32 base, offs_t offset, u16 mem_mask);
	void probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask);
	u16 candidate_r(u32 base, offs_t offset, u16 mem_mask, u16 *shadow, u32 words, trace_region region);
	void candidate_w(u32 base, offs_t offset, u16 data, u16 mem_mask, u16 *shadow, u32 words, trace_region region);
	void trace_access(trace_region region, bool write, u32 address, u16 data, u16 mem_mask, u16 last_write);
	void dump_repeated_accesses();
	void panel_receive_byte(u8 data);
	void flush_panel_text();
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
	void record_root_directory_instruction(u32 pc);
	void dump_root_directory_history(u32 trigger_pc, u32 identity, u8 depth_before);
	void log_root_directory_table_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask);
	void root_directory_summary();
	void mc68302_access_summary();
	void dump_root_directory_entry(const char *tag, u32 index);
	void dump_root_directory_table_summary();
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
	void log_fc3000_verify_handshake(bool write, u32 pc, u32 selected_cpu_byte_address, u32 offset, u16 data, u16 mem_mask);
	void log_esp_first_pass_write(u32 pc, u32 byte_address, u8 data);
	void log_esp_select_commit(u32 pc, u32 byte_address, u8 data);
	void log_esp_select_forward(u32 byte_address, u32 word_address, offs_t map_relative_offset,
		u32 fixed_offset, u8 data, u16 mem_mask);
	bool fc3000_verify_table_match() const;
	// filesystem-browser-map.md 4.25 (observation-only): identifying the
	// retry-exhaustion object at a3=~0x010722, distinct from the
	// already-fixed fff9bca0 table. No fix, no new behavior -- logging
	// only.
	void log_esp_f973f0_entry(u32 pc);
	void log_esp_other_table_first_retry(u32 pc);
	void log_esp_other_table_verify(u32 pc, u32 cpu_byte_address, u16 data, u16 mem_mask);
	void dump_memory_window(const char *tag, u32 base_address, u32 length_bytes);
	// filesystem-browser-map.md 4.26 TASK 6 (observation-only): bounded
	// per-attempt HALL REVERB (table $0e8e==0x00010400) type-1/record-0
	// GPR transaction trace.
	bool hall_reverb_type1_record0_active() const;
	void log_hall_reverb_event(const char *event, u32 pc, u32 byte_address, u16 data, u16 mem_mask);
	void log_fb81b4_path(const char *landmark, u32 pc, u8 tested_value, bool branch_taken,
		u32 branch_target, u16 sr_override = 0xffff, u32 d2_override = 0xffffffff);
	void log_04c6_origin(const char *landmark, u32 pc, u8 value, bool branch_taken, u32 branch_target);
	void log_error009_context(const char *source, u32 pc, u16 value, u16 mem_mask);
	void log_lowmem_service_context(bool write, u32 byte_address, u16 previous, u16 current, u16 data, u16 mem_mask);
	void log_synth_68302_irq_vectors(u8 irq_level, u32 pc, u16 sr);
	void log_runtime_vector_table_for_iack_experiment(u32 pc, u16 sr);
	void dump_loaded_code_range(const char *tag, u32 start, u32 end);
	void dump_highview_code_range(const char *tag, u32 start, u32 end);
	void scan_for_ascii_string(const char *tag, u32 start, u32 end, const char *needle);
	u16 read_highview_word(u32 address) const;
	void log_fsb_field_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask);
	void log_fsb_field_read(u32 pc, u32 byte_address, u16 data, u16 mem_mask);
	void log_fsb_entry(u32 entry_id, u32 pc);
	void log_fsb_snapshot(const char *milestone, u32 pc);
	void log_fsb_milestone_check_panel_text(u32 pc, u8 data, u32 length_after);
	void log_fsb_code_dumps(u32 pc);
	void log_pti_indirect_call(u32 pc, u32 site_index);
	void log_pti_trap9(u32 pc, const char *caller_tag);
	void log_pti_dump_target(u32 address);
	void check_pti_sites(u32 pc);
	void log_pti_stack_probe(const char *milestone, u32 pc);
	void log_pti_vector_call(u32 vector_index, const char *vector_name, u32 pc, u32 target,
		u32 byte_address, u16 data, const char *gate_note);
	void log_pti_vector_return(u32 vector_index, const char *vector_name, u32 pc);
	void log_pti_gate(const char *name, u32 pc, u32 value, u32 gate_index);
	void log_pti_node_gate(const char *name, u32 pc, u32 byte_address, u32 value, u32 index);
	void log_pti_trap4(u32 pc);
	void log_pti_vectors_34_44_46();
	void log_pti_full_vector_scan();
	void log_run_config_header();
	void log_pti_entry_stash(u32 entry_id, const char *name, u32 pc);
	void log_pti_f87f3e_body_entry(u32 pc);
	void log_pti_f87f80_direct(u32 entry_id, const char *name, u32 pc);
	void log_pti_f87f82_correlated(u32 pc);
	void log_pti_trap15(u32 pc);
	void log_pti_f880e0_check(u32 pc);
	void log_pti_trap8(u32 pc);
	void log_pti_trap9_epoch(u32 pc);
	void log_pti_ca_list_decrement(u32 pc);
	void log_pti_ca_list_callback(u32 pc);
	void log_pti_slot0_ready_bit_write(u32 pc, u32 byte_address, u16 value);
	void log_pti_trap2_entry(u32 pc);
	void log_pti_trap2_success_observation(u32 pc);
	void log_pti_node_field_write(const char *name, u32 pc, u32 byte_address, u32 value);
	void log_pti_trap12_or_14(u32 pc, u32 trap_num);
	void log_pti_trap13_call(u32 pc, u32 site_index);
	void log_pti_trap13_return(u32 pc, u32 site_index);
	void log_pti_dispatcher_f87f82(u32 pc);
	void log_pti_dc_pointer_write(u32 pc, u16 value);
	void log_pti_node16_producer(u32 pc, const char *site_name);
	void log_pti_d10a_producer(u32 pc);
	void log_pti_d6_pointer_write(u32 pc, u16 value);
	void log_pti_d6_pointer_read(u32 pc, u16 value);
	void log_pti_ca_root_write(u32 pc, u16 value);
	void log_pti_timer_field_write(const char *name, u32 pc, u32 byte_address, u16 value);
	void log_pti_12_family_entry(const char *name, u32 pc);
	void log_pti_ae12_before(u32 pc);
	void log_pti_ae1a_taken(u32 pc);
	void log_pti_ae20_full(u32 pc);
	void dump_slot_record(const char *tag, u32 slot_addr);
	void log_pti_dc_pointer_read(u32 pc, u16 value);
	void log_dispatcher_rte_first_pc_probe(u32 pc);
	void log_tuning_stall_candidate_dump(u32 pc);
	void log_tuning_stall_save_probe(u32 pc);
	void log_dispatcher_rte_candidate_pc(u32 pc);
	void log_f87f96_queue_read(u32 byte_address, u16 data, u16 mem_mask);
	void log_f87f96_queue_write(u32 byte_address, u16 previous, u16 current, u16 data, u16 mem_mask);
	void log_f87f96_queue_rte(int state);
	void log_lowmem_04ee(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_lowmem_049d(bool write, u16 previous, u16 current, u16 mem_mask);
	void log_pc_summary(const char *reason, u32 pc);
	void log_watched_pc(u32 pc);
	void log_duart_counter_watched_pc(u32 pc);
	void log_divzero_exception_frame(u32 pc);
	void log_primary_slot_snapshot_once(u32 pc);
	void log_timer_secondary_callback(u32 pc);
	std::string dump_cpu_registers() const;
	u16 es5506_host_read_par_diag();
	// ASR10_EXPERIMENT_ES5510_HOST: FC3101/FC3141/FC3181 are each a
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
	void log_panel_b_enqueue(u32 pc, u16 previous_03bc, u16 current_03bc);
	void log_panel_b_thrb(u32 pc, u8 data);
	void log_panel_b_rhrb(u32 pc, u8 data, u8 srb, u8 isr);
	void log_panel_b_complete(u32 pc, u16 previous_03bc, u16 current_03bc);
	void log_panel_b_wake(u32 pc, u32 byte_address, u16 previous, u16 current);
	void log_panel_c_parser_trace(u32 pc, const char *event);
	void panel_c_parser_trace_stop(const char *reason, u32 pc);
	void panel_c_queue_rx(u8 data, const char *reason, u32 pc);
	void panel_c_update_irq6(const char *reason, u32 pc);
	void panel_e_observe_thrb(u32 pc, u8 data);
	void panel_e_note_completion(u32 pc, u16 previous_03bc, u16 current_03bc);
	void panel_e_abort(const char *reason, u32 pc, u8 actual = 0xff);
	bool panel_e_try_inject(u32 pc, const char *reason);
	void panel_l_observe_thrb(u32 pc, u8 data);
	void panel_l_note_completion(u32 pc, u16 previous_03bc, u16 current_03bc);
	void panel_l_abort(const char *reason, u32 pc, u8 actual = 0xff);
	void panel_l_stop(const char *reason, u32 pc);
	bool panel_l_try_inject(u32 pc, const char *reason);
	void panel_l_log_temporal(const char *event, u32 pc, u32 byte_address = 0xffffffffU,
		u16 previous = 0, u16 current = 0);
	bool panel_reply_experiment_enabled() const;
	bool duart_irq_model_enabled() const;
	const char *panel_reply_experiment_name() const;
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
	m_lrclk_timer = timer_alloc(FUNC(asr10_boot_state::lrclk_toggle), this);
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&asr10_boot_state::panel_submission_summary, this));
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
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
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
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
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
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
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
				log_fc3000_verify_handshake(false, pc, selected_cpu_byte_address, offset, data, mem_mask);
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
					log_esp_other_table_verify(pc, selected_cpu_byte_address, data, mem_mask);
				// filesystem-browser-map.md 4.26 TASK 6: HALL REVERB
				// readback (latch bytes re-read after read-select) and the
				// verify compare itself, every attempt (not one-shot),
				// bounded by log_hall_reverb_event's own hard event cap.
				if (hall_reverb_type1_record0_active() &&
					(selected_cpu_byte_address == 0x00fc3001 ||
						selected_cpu_byte_address == 0x00fc3003 ||
						selected_cpu_byte_address == 0x00fc3005))
				{
					const char *const hr_event =
						selected_cpu_byte_address == 0x00fc3001 ? "read_latch_00" :
						selected_cpu_byte_address == 0x00fc3003 ? "read_latch_01" : "read_latch_02";
					log_hall_reverb_event(hr_event, pc, selected_cpu_byte_address, data, mem_mask);
				}
				if (pc == 0x00f97574 && hall_reverb_type1_record0_active())
					log_hall_reverb_event("verify_compare", pc, selected_cpu_byte_address, data, mem_mask);
			}
			if (!m_duart_counter_timer_enabled)
				return;
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
					log_esp_first_pass_write(pc, selected_cpu_byte_address, u8(data));
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
					log_esp_select_commit(pc, selected_cpu_byte_address, u8(data));
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
					// filesystem-browser-map.md 4.27 TASK 1/2 correction:
					// static disassembly (f97450) proves record type D3==1
					// keeps D4's DEFAULT value 0x1c0 (host offset 0xe0,
					// "Write select - GPR+INSTR combined") for the UPLOAD
					// pass's commit at f9743a -- not 0xa0. The FC3101/FC3141
					// pair traced in 4.26 is a SEPARATE re-select idiom
					// inside the VERIFY pass (f97498-f974f6), not the real
					// commit. FC3007-FC3011 (offsets 0x03-0x08, INSTR-latch
					// range) and FC31C1 (offset 0xe0, the real commit) were
					// never traced before this correction.
					const char *const hr_event =
						selected_cpu_byte_address == 0x00fc3001 ? "write_latch_00" :
						selected_cpu_byte_address == 0x00fc3003 ? "write_latch_01" :
						selected_cpu_byte_address == 0x00fc3005 ? "write_latch_02" :
						selected_cpu_byte_address == 0x00fc3007 ? "write_latch_03" :
						selected_cpu_byte_address == 0x00fc3009 ? "write_latch_04" :
						selected_cpu_byte_address == 0x00fc300b ? "write_latch_05" :
						selected_cpu_byte_address == 0x00fc300d ? "write_latch_06" :
						selected_cpu_byte_address == 0x00fc300f ? "write_latch_07" :
						selected_cpu_byte_address == 0x00fc3011 ? "write_latch_08" :
						selected_cpu_byte_address == 0x00fc3101 ? "write_read_select_0x80" :
						selected_cpu_byte_address == 0x00fc3141 ? "write_select_gpr_0xa0" : "write_select_gpr_instr_0xe0";
					log_hall_reverb_event(hr_event, pc, selected_cpu_byte_address, data, mem_mask);
				}
			}
			if (!m_duart_counter_timer_enabled)
				return;
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
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc222e_read pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc222e_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc222e, 0x00fc222f, "hook_fc222e_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc222e_write pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc226e_read_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc226e, 0x00fc226f, "hook_fc226e_read_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc226e_read pc=%06x mem_mask=%04x data=%04x\n",
				pc, mem_mask, data);
		});
	m_hook_fc226e_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc226e, 0x00fc226f, "hook_fc226e_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
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
	// Phase 1 host-port fingerprint experiment (ASR10_EXPERIMENT_ES5506_HOST):
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
				if (!m_es5506_host_enabled || machine().side_effects_disabled())
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
				if (!m_es5506_host_enabled || machine().side_effects_disabled())
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
	// TASK2 investigative dump: static ROM content around the previously
	// flagged-but-unverified "f8db00-f8db4e armed callback" range
	// (subsystems.md), to check for a literal reference near fff8db12.
	dump_loaded_code_range("task2_f8db_armed_callback_range", 0x00f8db00, 0x00f8db60);
	// TASK2 (uPD72069 TC investigation): the CMD46 read-sector loop's known
	// static PCs (fb8cee=fifo command/data writes, fb8db2=fifo/status
	// reads, per ASR10_FDC_CMD46's own fifo_write_pcs/fifo_result_pcs).
	// Disassemble the surrounding ROM to look for a write to some other
	// (currently unmapped-as-device) address once the expected byte
	// count is reached -- the real-hardware "software TC strobe"
	// convention documented for the same FDC family in
	// src/mame/akai/mpc60.cpp (a dedicated I/O write pulses tc_w(0);tc_w(1)).
	dump_loaded_code_range("task2_fdc_read_loop_fb8c80_fb8e00", 0x00fb8c80, 0x00fb8e00);
	dump_loaded_code_range("task2_fdc_read_loop_fb8e00_fb9100", 0x00fb8e00, 0x00fb9100);
	dump_loaded_code_range("task2_fdc_read_loop_fb8a00_fb8c80", 0x00fb8a00, 0x00fb8c80);
	dump_loaded_code_range("task1_fb90b2_decision_area", 0x00fb90b2, 0x00fb9280);
	dump_loaded_code_range("task1_fb92ce_fb9600", 0x00fb92ce, 0x00fb9600);
	dump_loaded_code_range("task1_fb9600_fb9800", 0x00fb9600, 0x00fb9800);
	dump_loaded_code_range("task1_fbb280_fbb600", 0x00fbb280, 0x00fbb600);
	dump_loaded_code_range("task1_fb8090_fb8120", 0x00fb8090, 0x00fb8120);
	dump_loaded_code_range("task1_fb7c00_fb7c40", 0x00fb7c00, 0x00fb7c40);
	dump_loaded_code_range("task1_fb89e0_fb8a00", 0x00fb89e0, 0x00fb8a00);
	dump_loaded_code_range("task2_fb8900_fb8a10", 0x00fb8900, 0x00fb8a10);
	dump_loaded_code_range("task2_fb8100_fb81f0", 0x00fb8100, 0x00fb81f0);
	dump_loaded_code_range("task1_fb8006_words", 0x00f80080, 0x00f800a0);
	dump_loaded_code_range("task1_fb93f4_fb9490", 0x00fb93f4, 0x00fb9490);
	dump_loaded_code_range("task1_fb8830_fb8880", 0x00fb8830, 0x00fb8880);
	dump_loaded_code_range("task4_lineA_handler", 0x00f882a0, 0x00f88320);
	dump_loaded_code_range("task1_fb7f30_fb7fa0", 0x00fb7f30, 0x00fb7fa0);
	dump_loaded_code_range("task4_vector_table_lineA", 0x00000000, 0x00000040);
	dump_loaded_code_range("task2_fb8c40_fb8ce0", 0x00fb8c40, 0x00fb8ce0);
	scan_for_ascii_string("rom_please_insert_disk", 0x00f80000, 0x00fbffff, "PLEASE INSERT DISK");
	scan_for_ascii_string("lowmem_please_insert_disk", 0x00000000, 0x000fffff, "PLEASE INSERT DISK");
	// TASK1 investigative scan: exhaustive search of the ENTIRE static ROM
	// for literal `jsr $fffc60b0` (4eb9 fffc 60b0) occurrences, since the
	// f8db00-f8db60 dump above turned up at least one such literal --
	// contradicting an earlier session's "zero literal $fc60xx references"
	// claim. Logs every match's address, not just a first-hit.
	{
		u32 matches = 0;
		for (u32 cursor = 0x00f80000; cursor <= 0x00fbfffa; cursor += 2)
		{
			if (read_loaded_word(cursor) == 0x4eb9 &&
				read_loaded_word(cursor + 2) == 0xfffc &&
				read_loaded_word(cursor + 4) == 0x60b0)
			{
				logerror("ASR10_TASK1_STATIC_JSR_SCAN match=%u caller_pc=%06x target=fc60b0\n",
					matches, cursor);
				matches++;
			}
		}
		logerror("ASR10_TASK1_STATIC_JSR_SCAN_DONE total_matches=%u range=f80000_fbffff\n", matches);
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
	save_item(NAME(m_es550x_vfx_shadow));
	save_item(NAME(m_es5510_vfx_shadow));
	save_item(NAME(m_fdc_vfx_shadow));
	save_item(NAME(m_es5506_ts_shadow));
	save_item(NAME(m_es5510_ts_shadow));
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
	save_item(NAME(m_panel_c_reply_71_zero_enabled));
	save_item(NAME(m_panel_c_reply_71_zero_armed));
	save_item(NAME(m_panel_c_reply_71_zero_injected));
	save_item(NAME(m_panel_d1_reply_71_ff_enabled));
	save_item(NAME(m_panel_d1_reply_71_ff_armed));
	save_item(NAME(m_panel_d1_reply_71_ff_injected));
	save_item(NAME(m_panel_d2_reply_71_7e_ff_enabled));
	save_item(NAME(m_panel_d2_reply_71_armed));
	save_item(NAME(m_panel_d2_reply_71_injected));
	save_item(NAME(m_panel_d2_reply_7e_armed));
	save_item(NAME(m_panel_d2_reply_7e_injected));
	save_item(NAME(m_panel_e_ff_drain_known_ring_enabled));
	save_item(NAME(m_panel_e_ff_drain_aborted));
	save_item(NAME(m_panel_e_ff_drain_done));
	save_item(NAME(m_panel_e_ff_drain_ready_valid));
	save_item(NAME(m_panel_e_ff_drain_waiting_completion));
	save_item(NAME(m_panel_e_f89abe_logged));
	save_item(NAME(m_panel_e_f89ac2_logged));
	save_item(NAME(m_panel_e_ff_drain_next_index));
	save_item(NAME(m_panel_e_ff_drain_ready_index));
	save_item(NAME(m_panel_e_ff_drain_pending_index));
	save_item(NAME(m_panel_e_ff_drain_pending_count));
	save_item(NAME(m_panel_l_ff_drain_later_rings_enabled));
	save_item(NAME(m_panel_l_aborted));
	save_item(NAME(m_panel_l_done));
	save_item(NAME(m_panel_l_first_ring_setup_done));
	save_item(NAME(m_panel_l_later_started));
	save_item(NAME(m_panel_l_waiting_completion));
	save_item(NAME(m_panel_l_previous_completion_observed));
	save_item(NAME(m_panel_l_zero_pending_f89ac2));
	save_item(NAME(m_panel_l_stop_after_dispatch));
	save_item(NAME(m_panel_l_final_idle_probe_queued));
	save_item(NAME(m_panel_l_final_idle_probe_waiting));
	save_item(NAME(m_panel_l_final_idle_f89a9a_zero_seen));
	save_item(NAME(m_panel_l_final_idle_f89ace_seen));
	save_item(NAME(m_panel_l_final_idle_03c5_clear_seen));
	save_item(NAME(m_panel_l_first_ring_index));
	save_item(NAME(m_panel_l_pending_count));
	save_item(NAME(m_panel_l_pending_byte));
	save_item(NAME(m_panel_l_previous_byte));
	save_item(NAME(m_panel_l_current_byte));
	save_item(NAME(m_panel_l_zero_slot0_before));
	save_item(NAME(m_panel_l_zero_slot0_after));
	save_item(NAME(m_panel_l_reply_count));
	save_item(NAME(m_panel_l_zero_crossing_count));
	save_item(NAME(m_panel_l_pending_thrb_pc));
	save_item(NAME(m_panel_l_current_thrb_pc));
	save_item(NAME(m_panel_l_later_start_cycle));
	save_item(NAME(m_panel_autorespond_enabled));
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
	save_item(NAME(m_duart_counter_timer_enabled));
	save_item(NAME(m_divzero_frame_logged));
	m_es5506_host_enabled = m_es5506_host.found();
	save_item(NAME(m_es5506_host_enabled));
	save_item(NAME(m_es5506_host_seen_mask));
	save_item(NAME(m_es5506_host_access_count));
	m_es5510_host_enabled = m_es5510_host.found();
	save_item(NAME(m_es5510_host_enabled));
	save_item(NAME(m_esp_select_commit_log_count));
	save_item(NAME(m_es5506_diag_par_enabled));
	save_item(NAME(m_es5506_diag_par_value));
	save_item(NAME(m_es5506_diag_par_read_count));
	save_item(NAME(m_primary_slot_snapshot_logged));
	save_item(NAME(m_fc60b0_verified));
	save_item(NAME(m_fc2d40_cluster_count));
	save_item(NAME(m_fc3000_cluster_count));
	save_item(NAME(m_panel_c_parser_trace_enabled));
	save_item(NAME(m_panel_c_parser_trace_active));
	save_item(NAME(m_panel_c_parser_trace_done));
	save_item(NAME(m_panel_c_rx_valid));
	save_item(NAME(m_panel_c_irq6_asserted));
	save_item(NAME(m_panel_c_rx_byte));
	save_item(NAME(m_panel_c_srb));
	save_item(NAME(m_panel_c_isr));
	save_item(NAME(m_panel_c_imr));
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
	{
		const char *const fs_browser_trace = std::getenv("ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE");
		const bool fs_browser_trace_enabled =
			fs_browser_trace && fs_browser_trace[0] && fs_browser_trace[0] != '0';
		m_fsb = fsb_state{};
		m_fsb.enabled = fs_browser_trace_enabled;
	}
	{
		const char *const post_tuning_trace = std::getenv("ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE");
		const bool post_tuning_trace_enabled =
			post_tuning_trace && post_tuning_trace[0] && post_tuning_trace[0] != '0';
		m_pti = pti_state{};
		m_pti.enabled = post_tuning_trace_enabled;
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
	const char *const panel_autorespond = std::getenv("ASR10_DIAG_PANEL_AUTORESPOND");
	m_panel_autorespond_enabled = panel_autorespond && panel_autorespond[0] && panel_autorespond[0] != '0';
	const char *const panel_reply_71_zero = std::getenv("ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO");
	m_panel_c_reply_71_zero_enabled = !m_panel_autorespond_enabled &&
		panel_reply_71_zero && panel_reply_71_zero[0] && panel_reply_71_zero[0] != '0';
	const char *const panel_reply_71_ff = std::getenv("ASR10_EXPERIMENT_PANEL_REPLY_71_FF");
	m_panel_d1_reply_71_ff_enabled = !m_panel_autorespond_enabled && !m_panel_c_reply_71_zero_enabled &&
		panel_reply_71_ff && panel_reply_71_ff[0] && panel_reply_71_ff[0] != '0';
	const char *const panel_reply_71_7e_ff = std::getenv("ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF");
	m_panel_d2_reply_71_7e_ff_enabled = !m_panel_autorespond_enabled && !m_panel_c_reply_71_zero_enabled &&
		!m_panel_d1_reply_71_ff_enabled &&
		panel_reply_71_7e_ff && panel_reply_71_7e_ff[0] && panel_reply_71_7e_ff[0] != '0';
	const char *const panel_ff_drain_known_ring = std::getenv("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING");
	m_panel_e_ff_drain_known_ring_enabled = !m_panel_autorespond_enabled && !m_panel_c_reply_71_zero_enabled &&
		!m_panel_d1_reply_71_ff_enabled &&
		!m_panel_d2_reply_71_7e_ff_enabled && panel_ff_drain_known_ring &&
		panel_ff_drain_known_ring[0] && panel_ff_drain_known_ring[0] != '0';
	const char *const panel_ff_drain_later_rings = std::getenv("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS");
	m_panel_l_ff_drain_later_rings_enabled = !m_panel_autorespond_enabled && !m_panel_c_reply_71_zero_enabled &&
		!m_panel_d1_reply_71_ff_enabled &&
		!m_panel_d2_reply_71_7e_ff_enabled && !m_panel_e_ff_drain_known_ring_enabled &&
		panel_ff_drain_later_rings && panel_ff_drain_later_rings[0] && panel_ff_drain_later_rings[0] != '0';
	m_panel_autorespond_scheduled_count = 0;
	m_panel_autorespond_injected_count = 0;
	m_panel_autorespond_timer->adjust(attotime::never);
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
	const char *const duart_counter_timer = std::getenv("ASR10_EXPERIMENT_DUART_COUNTER_TIMER");
	m_duart_counter_timer_enabled = duart_counter_timer && duart_counter_timer[0] && duart_counter_timer[0] != '0';
	m_divzero_frame_logged = false;
	m_es5506_host_seen_mask.fill(0);
	m_es5506_host_access_count = 0;
	{
		// Task-continuation diagnostic PAR test: requires BOTH the explicit
		// experiment flag AND a value, narrower than the old Phase 1B gate
		// (ASR10_DIAG_PAR_VALUE alone). NOT an analog model, NOT a claim
		// that 0x300 (or whatever value is passed) is a real resting value.
		const char *const par_diagnostic_env = std::getenv("ASR10_EXPERIMENT_PAR_DIAGNOSTIC");
		const bool par_diagnostic_enabled =
			par_diagnostic_env && par_diagnostic_env[0] && par_diagnostic_env[0] != '0';
		const char *const par_value_env = std::getenv("ASR10_DIAG_PAR_VALUE");
		m_es5506_diag_par_enabled = par_diagnostic_enabled && par_value_env && par_value_env[0];
		m_es5506_diag_par_value = m_es5506_diag_par_enabled
			? u16(std::strtoul(par_value_env, nullptr, 0)) : 0x200;
	}
	m_es5506_diag_par_read_count = 0;
	m_primary_slot_snapshot_logged = false;
	m_fc60b0_verified = false;
	m_fc2d40_cluster_count = 0;
	m_fc3000_cluster_count = 0;
	const char *const panel_c_parser_trace = std::getenv("ASR10_DIAG_PANEL_C_PARSER_TRACE");
	m_panel_c_parser_trace_enabled = m_panel_d1_reply_71_ff_enabled || m_panel_d2_reply_71_7e_ff_enabled ||
		m_panel_e_ff_drain_known_ring_enabled || m_panel_l_ff_drain_later_rings_enabled ||
		(panel_c_parser_trace && panel_c_parser_trace[0] && panel_c_parser_trace[0] != '0');
	m_panel_c_reply_71_zero_armed = false;
	m_panel_c_reply_71_zero_injected = false;
	m_panel_d1_reply_71_ff_armed = false;
	m_panel_d1_reply_71_ff_injected = false;
	m_panel_d2_reply_71_armed = false;
	m_panel_d2_reply_71_injected = false;
	m_panel_d2_reply_7e_armed = false;
	m_panel_d2_reply_7e_injected = false;
	m_panel_e_ff_drain_aborted = false;
	m_panel_e_ff_drain_done = false;
	m_panel_e_ff_drain_ready_valid = false;
	m_panel_e_ff_drain_waiting_completion = false;
	m_panel_e_f89abe_logged = false;
	m_panel_e_f89ac2_logged = false;
	m_panel_e_ff_drain_next_index = 0;
	m_panel_e_ff_drain_ready_index = 0;
	m_panel_e_ff_drain_pending_index = 0;
	m_panel_e_ff_drain_pending_count = 0;
	m_panel_l_aborted = false;
	m_panel_l_done = false;
	m_panel_l_first_ring_setup_done = false;
	m_panel_l_later_started = false;
	m_panel_l_waiting_completion = false;
	m_panel_l_previous_completion_observed = true;
	m_panel_l_zero_pending_f89ac2 = false;
	m_panel_l_stop_after_dispatch = false;
	m_panel_l_final_idle_probe_queued = false;
	m_panel_l_final_idle_probe_waiting = false;
	m_panel_l_final_idle_f89a9a_zero_seen = false;
	m_panel_l_final_idle_f89ace_seen = false;
	m_panel_l_final_idle_03c5_clear_seen = false;
	m_panel_l_first_ring_index = 0;
	m_panel_l_pending_count = 0;
	m_panel_l_pending_byte = 0;
	m_panel_l_previous_byte = 0xff;
	m_panel_l_current_byte = 0xff;
	m_panel_l_zero_slot0_before = 0;
	m_panel_l_zero_slot0_after = 0;
	m_panel_l_reply_count = 0;
	m_panel_l_zero_crossing_count = 0;
	m_panel_l_pending_thrb_pc = 0xffffffffU;
	m_panel_l_current_thrb_pc = 0xffffffffU;
	m_panel_l_later_start_cycle = 0;
	m_panel_c_parser_trace_active = false;
	m_panel_c_parser_trace_done = false;
	m_panel_c_rx_valid = false;
	m_panel_c_irq6_asserted = false;
	m_panel_c_rx_byte = 0;
	m_panel_c_srb = 0;
	m_panel_c_isr = 0;
	m_panel_c_imr = 0;
	m_panel_c_parser_trace_count = 0;
	m_panel_c_parser_trace_last_pc = 0xffffffffU;
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
	std::fill(std::begin(m_es550x_vfx_shadow), std::end(m_es550x_vfx_shadow), 0);
	std::fill(std::begin(m_es5510_vfx_shadow), std::end(m_es5510_vfx_shadow), 0);
	std::fill(std::begin(m_fdc_vfx_shadow), std::end(m_fdc_vfx_shadow), 0);
	std::fill(std::begin(m_es5506_ts_shadow), std::end(m_es5506_ts_shadow), 0);
	std::fill(std::begin(m_es5510_ts_shadow), std::end(m_es5510_ts_shadow), 0);
	m_trace_slots = {};

	// Coarse landmark polling only. This does not replace instruction tracing.
	m_pc_timer->adjust(attotime::zero, 0, attotime::from_ticks(64, m_maincpu->clock()));

	logerror("ASR10BOOT reset: expected SP=$00000300 PC=$0000000c from ROM vectors\n");
	logerror("ASR10_MAINCPU_INPUT_LINE_DRIVER source=harness default_set_input_line_calls=0 "
		"iack_map=installed_returns_autovectors_by_default pc_timer=diagnostic_poll "
		"prompt_select_timer=diagnostic_poll fc6850_fc6852_timer_binding=none\n");
	log_run_config_header();
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
	map(0x2c0000, 0x2c0007).rw(FUNC(asr10_boot_state::fdc_vfx_candidate_r), FUNC(asr10_boot_state::fdc_vfx_candidate_w));
	map(0x300000, 0x30007f).rw(FUNC(asr10_boot_state::es5506_ts_candidate_r), FUNC(asr10_boot_state::es5506_ts_candidate_w));
	map(0x380000, 0x3801ff).rw(FUNC(asr10_boot_state::es5510_ts_candidate_r), FUNC(asr10_boot_state::es5510_ts_candidate_w));

	map(0xf00000, 0xf7ffff).ram();
	map(0xf80000, 0xfbffff).rw(FUNC(asr10_boot_state::high_alias_r), FUNC(asr10_boot_state::high_alias_w));
	{
		// Phase 1 host-port fingerprint experiment
		// (ASR10_EXPERIMENT_ES5506_HOST): flagged premise, NOT board-proven
		// -- see docs/asr10/es5506-chain-verification.md. Narrow adapter
		// owns only FC2000-FC207F; FC2080+ (and FC2Dxx/FC30xx elsewhere)
		// are untouched .ram(), matching the exact 0x80-byte,
		// .umask16(0x00ff) convention already proven in esqkt.cpp/
		// macrossp.cpp/ssv.cpp for this same device.
		const char *const es5506_host_env = std::getenv("ASR10_EXPERIMENT_ES5506_HOST");
		const bool es5506_host_enabled =
			es5506_host_env && es5506_host_env[0] && es5506_host_env[0] != '0';
		if (es5506_host_enabled)
		{
			map(0xfc0000, 0xfc1fff).ram();
			map(0xfc2000, 0xfc207f).rw(m_es5506_host, FUNC(es5506_device::read), FUNC(es5506_device::write)).umask16(0x00ff);
			map(0xfc2080, 0xfc2fff).ram();
		}
		else
		{
			map(0xfc0000, 0xfc2fff).ram();
		}

		// ASR10_EXPERIMENT_ES5510_HOST (filesystem-browser-map.md 4.24):
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
		// proven ASR10_EXPERIMENT_ES5506_HOST adapter above; MAME's normal
		// word/byte bus-width shim (not any driver-side special case)
		// makes this transparent to both ordinary move.b and MOVEP's
		// spaced byte accesses. Real precedent for this exact mapping
		// (same device, same .umask16(0x00ff) idiom, whole host_r/host_w
		// window) is esq5505.cpp's map(0x260000, 0x2601ff).rw(m_esp,
		// FUNC(es5510_device::host_r), FUNC(es5510_device::host_w))
		// .umask16(0x00ff); this round deliberately narrows that to only
		// the evidenced offsets, per this investigation's acceptance
		// criteria, and can be widened later if evidence demands it.
		const char *const es5510_host_env = std::getenv("ASR10_EXPERIMENT_ES5510_HOST");
		const bool es5510_host_enabled =
			es5510_host_env && es5510_host_env[0] && es5510_host_env[0] != '0';
		if (es5510_host_enabled)
		{
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
		}
		else
		{
			map(0xfc3000, 0xfc31ff).ram();
		}

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
	if (duart_irq_model_enabled() && level == 6 && m_panel_c_irq6_asserted)
	{
		vector = 0x56;
		custom_vector = true;
		logerror("%s event=iack level=6 vector=%02x "
			"pc=%06x sr=%04x isr=%02x imr=%02x active=%02x rx_active=%02x counter_active=%02x "
			"rx_valid=%u rx_byte=%02x\n",
			panel_reply_experiment_name(), vector, pc, sr, m_panel_c_isr, m_panel_c_imr, m_panel_c_isr & m_panel_c_imr,
			m_panel_c_isr & m_panel_c_imr & 0x20, m_panel_c_isr & m_panel_c_imr & 0x08,
			m_panel_c_rx_valid ? 1 : 0, m_panel_c_rx_byte);
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
		if (!machine().side_effects_disabled())
			log_lowmem_service_context(false, byte_address, m_lowmem_shadow[offset], m_lowmem_shadow[offset], data, mem_mask);
		if (!machine().side_effects_disabled())
			log_f87f96_queue_read(byte_address, data, mem_mask);
		if (m_fsb.enabled && !machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			log_fsb_field_read(pc, byte_address, m_lowmem_shadow[offset], mem_mask);
			// fb895a (generic bounds-checked range reader): `movea.w #$4fe,A2`
			// is register-only, so the entry proxy is its very next
			// instruction, the unconditional read of $416.w into D2.
			if (byte_address == 0x0416 && pc == 0x00fb895e)
				log_fsb_entry(FSB_ENTRY_FB895A, pc);
			// fb8c6e (SEEK wrapper): `bsr fb7c7a` is control-only; the entry
			// proxy is the read of $49e.w (seek target) immediately after
			// that ready-check subroutine returns.
			if (byte_address == 0x049e && pc == 0x00fb8c78)
				log_fsb_entry(FSB_ENTRY_FB8C6E, pc);
			// f894a4's canonical entry is f8948e; this reads its gate flag
			// $4b2.w at f89494, on every execution through that canonical
			// entry path regardless of which of the three paths (A/B/C, see
			// filesystem-browser-map.md 6.3) is then taken. This proves
			// execution (or its absence) through the *canonical*
			// f8948e/f89494 path specifically -- it does not prove the
			// routine's interior (f894a4 onward) is unreachable by some
			// other control-flow edge (see 6.2's enumeration of candidate
			// entries: no static caller was found for f8948e itself either,
			// but ~69 indirect jsr/jmp-through-register sites exist ROM-wide
			// that were not exhaustively checked against runtime pointer
			// contents).
			if (byte_address == 0x04b2 && pc == 0x00f89494)
			{
				log_fsb_entry(FSB_ENTRY_F894A4_ROUTINE, pc);
				log_fsb_code_dumps(pc);
			}
			// The table read itself (`move.w (0,A0),D1` at f894b4) proves
			// only that the table-consumption path was reached, not that
			// the canonical entry path ran -- see FSB_ENTRY_F894A4_ROUTINE
			// above for that.
			if (pc == 0x00f894b4 && byte_address >= 0x0544 && byte_address < 0x0544 + 40 * 0x1a)
			{
				const u32 entry_index = (byte_address - 0x0544) / 0x1a;
				log_fsb_entry(FSB_ENTRY_F894A4_TABLE, pc);
				logerror("ASR10_FSB_TABLE_ACCESS name=f894a4_table_read pc=%06x entry_index=%u "
					"address=%06x value=%04x\n",
					pc, entry_index, byte_address, m_lowmem_shadow[offset]);
			}
		}
		if (m_pti.enabled && m_pti.seen_f880fc && !machine().side_effects_disabled())
		{
			// Slot 0's own resume code (ae18) is `and.b #$80,D0; beq $ae20`
			// -- a register/flag-only decision with no memory reference of
			// its own, so it can't be tapped directly (same "Path D" style
			// limitation as f894a4's bounds check). `rts` at ae20 (the
			// early-return target if the branch is taken) DOES read memory
			// (pops the return address from the stack), giving a genuine
			// proxy: if this fires, the branch was taken and `jsr $87f2.w`
			// (the only vector directly reachable from *this* loop -- see
			// filesystem-browser-map.md 4.9's authoritative disassembly;
			// the other five belong to a separate routine at ae22 whose
			// call relationship to this one is not yet established) did
			// not execute this dispatch. D0 is unchanged since ae18 (only
			// `and`/`beq` intervene), so its value here is also the value
			// tested.
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc == 0x0000ae20)
			{
				const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
				logerror("ASR10_PTI_SLOT0_EARLY_RETURN pc=%06x d0=%08x active_slot=%u sp=%06x "
					"read_address=%06x read_data=%04x region=%s panel=\"%s\"\n",
					pc, u32(m_maincpu->state_int(M68K_D0)), m_dispatcher_rte_frame_slot, sp,
					byte_address, data, address_region_guess(sp), m_panel_text);
			}
			// Gate/return reads for the ae22 routine's five vectors (all
			// via genuine memory reads immediately before/after each
			// conditional jsr -- see filesystem-browser-map.md 4.9).
			if (byte_address == 0x0cdc && pc == 0x0000ae2e)
				log_pti_vector_return(1, "bc8e", pc);
			if (byte_address == 0x0182 && pc == 0x0000ae56)
				log_pti_gate("ae56_183_first_check", pc, data, 1);
			if (byte_address == 0x031c && pc == 0x0000ae62)
			{
				log_pti_gate("ae62_31c_gates_8864", pc, data, 2);
				log_pti_vector_return(2, "a26e_site1", pc);
			}
			if (pc == 0x0000ae6c)
				log_pti_vector_return(3, "8864", pc);
			if (byte_address == 0x0182 && pc == 0x0000ae72)
				log_pti_gate("ae72_183_second_check", pc, data, 3);
			if (byte_address == 0x0306 && pc == 0x0000ae7a)
				log_pti_gate("ae7a_306_gates_a26e_site2", pc, data, 4);
			// 002b14's node-classify sequence (filesystem-browser-map.md
			// 4.10): 2b1a is `cmpa.w $35e.w,A5` (genuine read); 2b26 is
			// `move.w $31c.w,D5` (genuine read, also the D5 producer).
			if (byte_address == 0x035e && pc == 0x00002b1a)
				log_pti_node_gate("2b1a_35e_vs_a5", pc, byte_address, data, 0);
			if (byte_address == 0x031c && pc == 0x00002b26)
				log_pti_node_gate("2b26_31c_to_d5", pc, byte_address, data, 1);
			// filesystem-browser-map.md 4.13: trap #13 call site 2
			// (f89b54)'s only following instruction is `rts` at f89b56,
			// a stack-pop read with no write to tap -- same technique
			// as the ae20 slot-0 early-return proxy above.
			if (pc == 0x00f89b56)
				log_pti_trap13_return(pc, 2);
			// filesystem-browser-map.md 4.13 TASK 4: broad net, read side.
			if (byte_address == 0x00dc)
				log_pti_dc_pointer_read(pc, data);
			// filesystem-browser-map.md 4.14: f880d6 (trap #6)'s first
			// genuine access is a read, unlike the other entry proxies.
			if (pc == 0x00f880e0)
				log_pti_f880e0_check(pc);
		}
		// filesystem-browser-map.md 4.15: early-epoch instrumentation,
		// gated purely on `enabled` (from reset) -- see the matching
		// block in lowmem_w for the gate-correction rationale.
		if (m_pti.enabled && !machine().side_effects_disabled())
		{
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc == 0x00f8813c)
				log_pti_trap9_epoch(pc);
			// filesystem-browser-map.md 4.16 TASK 3/6: broad net, read side.
			if (byte_address == 0x00d6)
				log_pti_d6_pointer_read(pc, data);
			// filesystem-browser-map.md 4.17 TASK 1: the outer dispatcher
			// entry `tst.b $101a.w` at 12efc is a genuine read.
			if (pc == 0x00012efc)
				log_pti_12_family_entry("12efc", pc);
			// filesystem-browser-map.md 4.18: ae20's `rts` stack-pop read.
			if (pc == 0x0000ae20)
				log_pti_ae20_full(pc);
		}
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


void asr10_boot_state::log_panel_b_enqueue(u32 pc, u16 previous_03bc, u16 current_03bc)
{
	if constexpr (!ASR10_DIAG_PANEL_B)
		return;
	if (machine().side_effects_disabled())
		return;

	const u8 count_before = u8(previous_03bc >> 8);
	const u8 count_after = u8(current_03bc >> 8);
	const u8 byte = m_panel_b_last_ring_write_valid ? m_panel_b_last_ring_write_byte : 0xff;
	const u16 slot0_state = lowmem_word(0x23d6);
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	logerror("ASR10_DIAG_PANEL_B event=PANEL_ENQUEUE seq=%llu pc=%06x previous_pc=%06x "
		"caller_stack0=%08x byte=%02x ascii='%c' count_03bc_before=%02x count_03bc_after=%02x "
		"write_ptr_03b8_before=%04x write_ptr_03b8_after=%04x read_ptr_03ba=%04x idle_03c5=%02x "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x ring_write_pc=%06x\n",
		(unsigned long long)++m_panel_b_seq, pc, m_last_distinct_pc, read_stack_long(sp),
		byte, byte >= 0x20 && byte <= 0x7e ? char(byte) : '.',
		count_before, count_after, m_panel_b_last_ring_write_valid ? u16(m_panel_b_last_ring_write_address) : 0xffff,
		lowmem_word(0x03b8), lowmem_word(0x03ba), lowmem_byte(0x03c5),
		slot0_state, lowmem_word(0x23e4), lowmem_word(0x23e6), m_panel_b_last_ring_write_pc);
}


void asr10_boot_state::log_panel_b_thrb(u32 pc, u8 data)
{
	if constexpr (!ASR10_DIAG_PANEL_B)
		return;
	if (machine().side_effects_disabled())
		return;

	const char *source = (pc == 0x00f89cb0) ? "f89cb0" :
		(pc == 0x00f89aa4) ? "f89aa4" : "other";
	logerror("ASR10_DIAG_PANEL_B event=PANEL_THRB seq=%llu pc=%06x previous_pc=%06x source=%s "
		"byte=%02x ascii='%c' count_03bc=%02x write_ptr_03b8=%04x read_ptr_03ba=%04x "
		"idle_03c5=%02x parser_state_03c0=%04x\n",
		(unsigned long long)++m_panel_b_seq, pc, m_last_distinct_pc, source,
		data, data >= 0x20 && data <= 0x7e ? char(data) : '.',
		lowmem_byte(0x03bc), lowmem_word(0x03b8), lowmem_word(0x03ba),
		lowmem_byte(0x03c5), lowmem_word(0x03c0));
}


void asr10_boot_state::log_panel_b_rhrb(u32 pc, u8 data, u8 srb, u8 isr)
{
	if constexpr (!ASR10_DIAG_PANEL_B)
		return;
	if (machine().side_effects_disabled())
		return;

	const char *source = (pc >= 0x00ffb22a && pc <= 0x00ffb240) ? "irq6_rx_parser_ffb22a" : "unknown";
	const char *parser = m_panel_b_last_parser_pc == 0x00ffb286 ? "ffb286_f89aec" :
		m_panel_b_last_parser_pc == 0x00ffb32e ? "ffb32e_f89aec" :
		m_panel_b_last_parser_pc == 0x00ffb3e4 ? "ffb3e4_f89a9a" :
		m_panel_b_last_parser_pc == 0x00ffb424 ? "ffb424_f89a9a" : "unknown";
	m_panel_b_last_rhrb_seq = ++m_panel_b_seq;
	logerror("ASR10_DIAG_PANEL_B event=PANEL_RHRB seq=%llu pc=%06x previous_pc=%06x source=%s "
		"byte=%02x ascii='%c' parser_state_03c0=%04x srb=%02x isr=%02x count_03bc=%02x "
		"read_ptr_03ba=%04x parser_branch_or_destination=%s last_parser_pc=%06x\n",
		(unsigned long long)m_panel_b_last_rhrb_seq, pc, m_last_distinct_pc, source,
		data, data >= 0x20 && data <= 0x7e ? char(data) : '.',
		lowmem_word(0x03c0), srb, isr, lowmem_byte(0x03bc), lowmem_word(0x03ba),
		parser, m_panel_b_last_parser_pc);
}


void asr10_boot_state::log_panel_b_complete(u32 pc, u16 previous_03bc, u16 current_03bc)
{
	if constexpr (!ASR10_DIAG_PANEL_B)
		return;
	if (machine().side_effects_disabled())
		return;

	const char *source = m_panel_b_last_parser_pc == 0x00ffb3e4 ? "ffb3e4_to_f89a9a_f89ab8" :
		m_panel_b_last_parser_pc == 0x00ffb424 ? "ffb424_to_f89a9a_f89ab8" :
		m_panel_b_last_parser_pc == 0x00ffb3cc ? "ffb3cc_ff_to_f89a9a_f89ab8" :
		m_panel_b_last_parser_pc == 0x00f89aec ? "f89aec_related" :
		(pc == 0x00f89ab8) ? "f89ab8_count_decrement" : "unknown";
	logerror("ASR10_DIAG_PANEL_B event=PANEL_COMPLETE seq=%llu pc=%06x previous_pc=%06x source_path=%s "
		"triggering_rhrb_seq=%llu count_03bc_before=%02x count_03bc_after=%02x "
		"next_thrb_byte=unknown parser_state_03c0=%04x byte_03bd=%02x last_parser_pc=%06x\n",
		(unsigned long long)++m_panel_b_seq, pc, m_last_distinct_pc, source,
		(unsigned long long)m_panel_b_last_rhrb_seq, u8(previous_03bc >> 8), u8(current_03bc >> 8),
		lowmem_word(0x03c0), lowmem_byte(0x03bd), m_panel_b_last_parser_pc);
}


void asr10_boot_state::log_panel_b_wake(u32 pc, u32 byte_address, u16 previous, u16 current)
{
	if constexpr (!ASR10_DIAG_PANEL_B)
		return;
	if (machine().side_effects_disabled())
		return;

	logerror("ASR10_DIAG_PANEL_B event=PANEL_WAKE seq=%llu pc=%06x previous_pc=%06x "
		"count_03bc_before=00 count_03bc_after=%02x slot_write_address=%06x "
		"slot0_state_before=%04x slot0_state_after=%04x slot0_queue_head=%04x "
		"slot0_queue_tail=%04x node_14f4_type=%04x\n",
		(unsigned long long)++m_panel_b_seq, pc, m_last_distinct_pc, lowmem_byte(0x03bc),
		byte_address, previous, current, lowmem_word(0x23e4), lowmem_word(0x23e6),
		lowmem_word(0x14f6));
}


void asr10_boot_state::log_panel_c_parser_trace(u32 pc, const char *event)
{
	if (!m_panel_c_parser_trace_enabled || machine().side_effects_disabled())
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	logerror("ASR10_DIAG_PANEL_C_PARSER_TRACE event=%s seq=%u pc=%06x previous_pc=%06x "
		"op0=%04x op1=%04x op2=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x "
		"a0=%08x a1=%08x a2=%08x sr=%04x sp=%06x stack0=%08x "
		"b03c0=%02x b03c1=%02x b03c2=%02x b03c3=%02x b03c4=%02x b03c5=%02x b03c6=%02x "
		"b03bc=%02x w03c0=%04x\n",
		event, m_panel_c_parser_trace_count, pc, m_last_distinct_pc,
		read_program_word(pc), read_program_word(pc + 2), read_program_word(pc + 4),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u16(m_maincpu->state_int(M68K_SR)),
		sp, read_stack_long(sp),
		lowmem_byte(0x03c0), lowmem_byte(0x03c1), lowmem_byte(0x03c2),
		lowmem_byte(0x03c3), lowmem_byte(0x03c4), lowmem_byte(0x03c5),
		lowmem_byte(0x03c6), lowmem_byte(0x03bc), lowmem_word(0x03c0));
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


bool asr10_boot_state::panel_reply_experiment_enabled() const
{
	return m_panel_autorespond_enabled ||
		m_panel_c_reply_71_zero_enabled || m_panel_d1_reply_71_ff_enabled ||
		m_panel_d2_reply_71_7e_ff_enabled || m_panel_e_ff_drain_known_ring_enabled ||
		m_panel_l_ff_drain_later_rings_enabled;
}


const char *asr10_boot_state::panel_reply_experiment_name() const
{
	return m_panel_autorespond_enabled ? "ASR10_DIAG_PANEL_AUTORESPOND" :
		m_panel_l_ff_drain_later_rings_enabled ? "ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS" :
		m_panel_e_ff_drain_known_ring_enabled ? "ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING" :
		m_panel_d2_reply_71_7e_ff_enabled ? "ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF" :
		m_panel_d1_reply_71_ff_enabled ? "ASR10_EXPERIMENT_PANEL_REPLY_71_FF" :
		m_duart_counter_timer_enabled ? "ASR10_EXPERIMENT_DUART_COUNTER_TIMER" :
		"ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO";
}


bool asr10_boot_state::duart_irq_model_enabled() const
{
	// Counter/timer is not a panel-reply experiment; it only needs the same
	// proven ISR/IMR-driven IRQ6 + IACK routing. Kept narrow and used only
	// in IRQ routing/IACK paths so existing panel RX behavior is unchanged
	// bit-for-bit when only ASR10_EXPERIMENT_DUART_COUNTER_TIMER is set.
	return panel_reply_experiment_enabled() || m_duart_counter_timer_enabled;
}


void asr10_boot_state::panel_c_update_irq6(const char *reason, u32 pc)
{
	if (!duart_irq_model_enabled() || machine().side_effects_disabled())
		return;

	// 0x20 = RxRDYB (proven panel-reply path); 0x08 = counter/timer ready
	// (ASR10_EXPERIMENT_DUART_COUNTER_TIMER). Bit 0x08 is only ever set by
	// that experiment, so this widened mask is a no-op when it is disabled.
	const u8 rx_active = m_panel_c_isr & m_panel_c_imr & 0x20;
	const u8 counter_active = m_panel_c_isr & m_panel_c_imr & 0x08;
	const bool active = (rx_active | counter_active) != 0;
	if (active == m_panel_c_irq6_asserted)
	{
		logerror("%s event=irq6_route_no_change reason=%s pc=%06x "
			"isr=%02x imr=%02x active=%02x rx_active=%02x counter_active=%02x irq6=%u rx_valid=%u rx_byte=%02x\n",
			panel_reply_experiment_name(), reason, pc, m_panel_c_isr, m_panel_c_imr, m_panel_c_isr & m_panel_c_imr,
			rx_active, counter_active,
			m_panel_c_irq6_asserted ? 1 : 0, m_panel_c_rx_valid ? 1 : 0, m_panel_c_rx_byte);
		return;
	}

	m_panel_c_irq6_asserted = active;
	m_maincpu->set_input_line(6, active ? ASSERT_LINE : CLEAR_LINE);
	logerror("%s event=irq6_route reason=%s pc=%06x "
		"isr=%02x imr=%02x active=%02x rx_active=%02x counter_active=%02x irq6=%u rx_valid=%u rx_byte=%02x sr=%04x\n",
		panel_reply_experiment_name(), reason, pc, m_panel_c_isr, m_panel_c_imr, m_panel_c_isr & m_panel_c_imr,
		rx_active, counter_active,
		m_panel_c_irq6_asserted ? 1 : 0, m_panel_c_rx_valid ? 1 : 0, m_panel_c_rx_byte,
		u16(m_maincpu->state_int(M68K_SR)));
}


void asr10_boot_state::panel_c_queue_rx(u8 data, const char *reason, u32 pc)
{
	if (!panel_reply_experiment_enabled() || machine().side_effects_disabled())
		return;
	if (m_panel_c_rx_valid)
	{
		logerror("%s event=rx_queue_blocked reason=%s pc=%06x "
			"existing_rx=%02x requested_rx=%02x srb=%02x isr=%02x imr=%02x\n",
			panel_reply_experiment_name(), reason, pc, m_panel_c_rx_byte, data, m_panel_c_srb, m_panel_c_isr, m_panel_c_imr);
		return;
	}

	m_panel_c_rx_valid = true;
	m_panel_c_rx_byte = data;
	m_panel_c_srb |= 0x01;
	m_panel_c_isr |= 0x20;
	logerror("%s event=rx_queued reason=%s pc=%06x "
		"rx=%02x srb=%02x isr=%02x imr=%02x active=%02x slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x count_03bc=%02x\n",
		panel_reply_experiment_name(), reason, pc, data, m_panel_c_srb, m_panel_c_isr, m_panel_c_imr, m_panel_c_isr & m_panel_c_imr,
		lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6),
		lowmem_byte(0x03bc));
	panel_c_update_irq6("rx_queued", pc);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::lrclk_toggle)
{
	m_lrclk_level = !m_lrclk_level;
	m_maincpu->set_pb_input(3, m_lrclk_level);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::panel_autorespond_fire)
{
	if (!m_panel_autorespond_enabled)
		return;

	const u32 write_pc = u32(param);
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_panel_autorespond_injected_count++;
	logerror("ASR10_DIAG_PANEL_AUTORESPOND event=inject_response seq=%u write_pc=%06x pc=%06x byte=ff "
		"count_03bc=%02x idle_03c5=%02x rx_valid_before=%u slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		m_panel_autorespond_injected_count, write_pc, pc, lowmem_byte(0x03bc), lowmem_byte(0x03c5),
		m_panel_c_rx_valid ? 1 : 0, lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));
	panel_c_queue_rx(0xff, "autorespond_fc4817_write", write_pc);
}


void asr10_boot_state::log_duart_counter_watched_pc(u32 pc)
{
	if (!m_duart_counter_timer_enabled)
		return;

	const char *role = nullptr;
	switch (pc)
	{
	case 0x00f87ed2: role = "scheduler_core_f87ed2"; break;
	case 0x00f87eda: role = "scheduler_core_f87eda"; break;
	case 0x00f88300: role = "bit3_handler_entry_f88300"; break;
	case 0x00f88302: role = "bit3_handler_body_f88302"; break;
	case 0x00f8e1ee: role = "f8e2xx_user_f8e1ee"; break;
	case 0x00f8e1f6: role = "f8e2xx_user_f8e1f6"; break;
	case 0x00f97bd6: role = "f97bxx_user_f97bd6"; break;
	case 0x00f97bde: role = "f97bxx_user_f97bde"; break;
	default:
		return;
	}
	logerror("ASR10_DUART_COUNTER event=watched_pc pc=%06x role=%s\n", pc, role);
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
	// Diagnostic-only fixed-value PAR test (ASR10_EXPERIMENT_PAR_DIAGNOSTIC).
	// NOT an analog model, NOT a claim that this value is a real resting
	// position for any physical control -- its sole purpose is to observe
	// the DIVU's downstream propagation with a controlled, in-range input.
	m_es5506_diag_par_read_count++;
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	logerror("ASR10_ES5506_HOST event=par_diag_read source=diagnostic_constant value=%03x "
		"read_count=%u caller_pc=%06x\n",
		m_es5506_diag_par_value, m_es5506_diag_par_read_count, pc);
	return m_es5506_diag_par_value;
}


// ASR10_EXPERIMENT_ES5510_HOST select/commit wrappers (filesystem-browser-
// map.md 4.24 TASK 2/3). FC3100-FC3101, FC3140-FC3141 and FC3180-FC3181 are
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
		log_esp_select_forward(0x00fc3101, 0x00fc3100, offset, 0x80, data, 0x00ff);
	m_es5510_host->host_w(0x80, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xa0);
}

void asr10_boot_state::es5510_host_write_select_gpr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		log_esp_select_forward(0x00fc3141, 0x00fc3140, offset, 0xa0, data, 0x00ff);
	m_es5510_host->host_w(0xa0, data);
}

u8 asr10_boot_state::es5510_host_write_select_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xc0);
}

void asr10_boot_state::es5510_host_write_select_instr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		log_esp_select_forward(0x00fc3181, 0x00fc3180, offset, 0xc0, data, 0x00ff);
	m_es5510_host->host_w(0xc0, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xe0);
}

void asr10_boot_state::es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data)
{
	if (m_fc3000_verify_trace_enabled && fc3000_verify_table_match() && (data == 0 || data == 58))
		log_esp_select_forward(0x00fc31c1, 0x00fc31c0, offset, 0xe0, data, 0x00ff);
	m_es5510_host->host_w(0xe0, data);
}


void asr10_boot_state::log_divzero_exception_frame(u32 pc)
{
	// One-shot: 68000 ERROR 130 (divide-by-zero) handler entry, per
	// architecture.md's trap table (f882b6: moveq #$82). Observation only.
	if (!m_duart_counter_timer_enabled || m_divzero_frame_logged || pc != 0x00f882b6)
		return;
	m_divzero_frame_logged = true;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u16 stacked_sr = read_program_word(sp);
	const u32 stacked_pc = read_stack_long(sp + 2);
	logerror("ASR10_DIVZERO_FRAME event=exception_frame pc=%06x sp=%06x stacked_sr=%04x stacked_pc=%06x "
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
		"a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x "
		"tick_0b82=%02x flag_03c5=%02x word_03bc=%04x\n",
		pc, sp, stacked_sr, stacked_pc,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u32(m_maincpu->state_int(M68K_A4)), u32(m_maincpu->state_int(M68K_A5)),
		u32(m_maincpu->state_int(M68K_A6)),
		lowmem_byte(0x0b82), lowmem_byte(0x03c5), lowmem_word(0x03bc));

	// Temporary: stacked_pc lands in loaded low-RAM OS content (populated by
	// the floppy-image loader at runtime), not static ROM, so it cannot be
	// disassembled from the ROM file. Dump the live shadow bytes around it
	// (32 before, 8 after) for offline unidasm analysis.
	if (stacked_pc >= 32 && stacked_pc + 8 < LOWMEM_WORDS * 2)
	{
		std::string hex;
		for (u32 offset = stacked_pc - 32; offset < stacked_pc + 8; offset++)
			hex += util::string_format("%02x", lowmem_byte(offset));
		logerror("ASR10_DIVZERO_FRAME event=stacked_pc_bytes stacked_pc=%06x dump_start=%06x "
			"dump_len=40 hex=%s\n",
			stacked_pc, stacked_pc - 32, hex.c_str());
	}

	// Extended stack-frame dump: SP+0 (stacked SR), SP+2..SP+5 (stacked PC),
	// then every word SP+6..SP+0x24. The supervisor stack lives in low RAM
	// (per the captured A7/SP), so the shadow-backed read_program_word is
	// exact. SP+6 (a long) is the return address into 0x6800's direct
	// caller, since 0x6800 pushes nothing of its own before the DIVU.
	{
		std::string words;
		for (u32 off = 0; off <= 0x24; off += 2)
			words += util::string_format("%04x@%02x ", read_program_word(sp + off), off);
		const u32 caller_return_address = read_stack_long(sp + 6);
		logerror("ASR10_DIVZERO_FRAME event=stack_frame_dump sp=%06x words=\"%s\" "
			"caller_return_address=%06x\n",
			sp, words.c_str(), caller_return_address);

		// Live bytes around the caller's return address, for offline unidasm
		// (same rationale as the stacked_pc dump above: loaded low-RAM OS
		// content, not static ROM).
		if (caller_return_address >= 48 && caller_return_address + 8 < LOWMEM_WORDS * 2)
		{
			std::string hex;
			for (u32 offset = caller_return_address - 48; offset < caller_return_address + 8; offset++)
				hex += util::string_format("%02x", lowmem_byte(offset));
			logerror("ASR10_DIVZERO_FRAME event=caller_return_bytes caller_return_address=%06x "
				"dump_start=%06x dump_len=56 hex=%s\n",
				caller_return_address, caller_return_address - 48, hex.c_str());
		}
	}
}


void asr10_boot_state::log_primary_slot_snapshot_once(u32 pc)
{
	// One-shot, first entry into f88300 only. Walks the primary scheduler
	// slot table [$00c6.w, $00c8.w), stride 0x16. Pure shadow reads via
	// lowmem_word -- no guest RAM is mutated.
	if (!m_duart_counter_timer_enabled || m_primary_slot_snapshot_logged || pc != 0x00f88300)
		return;
	m_primary_slot_snapshot_logged = true;

	const u16 base = lowmem_word(0x00c6);
	const u16 end = lowmem_word(0x00c8);
	constexpr u32 MAX_SLOTS = 16;
	u32 index = 0;
	for (u32 addr = base; addr < u32(end) && index < MAX_SLOTS; addr += 0x16, index++)
	{
		logerror("ASR10_SLOT_TIMEOUT_SNAPSHOT event=primary_slot pc=%06x index=%u slot_base=%04x "
			"countdown_plus00=%04x state_plus02=%04x threshold_plus14=%04x\n",
			pc, index, addr, lowmem_word(addr), lowmem_word(addr + 2), lowmem_word(addr + 0x14));
	}
	logerror("ASR10_SLOT_TIMEOUT_SNAPSHOT event=primary_table_bounds pc=%06x base=%04x end=%04x count=%u\n",
		pc, base, end, index);
}


void asr10_boot_state::log_timer_secondary_callback(u32 pc)
{
	// f88352 is the jsr (A1) itself, immediately after f8834a's
	// movea.l ($16,A0),A1 loads the callback pointer -- verified by
	// disassembly, not assumed. Logs every invocation (volume is low:
	// gated by the every-10th-tick $0b82 walk plus each entry's own
	// +0x14 countdown reaching zero), so the one immediately preceding
	// a fault is always captured regardless of any dedup scheme.
	if (!m_duart_counter_timer_enabled || pc != 0x00f88352)
		return;

	const u32 entry_base = m_maincpu->state_int(M68K_A0) & 0x00ffffff;
	const u32 callback_ptr = m_maincpu->state_int(M68K_A1) & 0x00ffffff;
	logerror("ASR10_TIMER_SECONDARY_CALLBACK event=callback_invoke pc=%06x entry_base=%06x "
		"entry_plus14_countdown=%04x callback_ptr=%06x tick_0b82=%02x caller_pc=%06x\n",
		pc, entry_base, lowmem_word(entry_base + 0x14), callback_ptr,
		lowmem_byte(0x0b82), m_last_distinct_pc);
}


void asr10_boot_state::panel_e_abort(const char *reason, u32 pc, u8 actual)
{
	if (!m_panel_e_ff_drain_known_ring_enabled || m_panel_e_ff_drain_aborted)
		return;

	m_panel_e_ff_drain_aborted = true;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=abort reason=%s pc=%06x "
		"next_index=%u ready_valid=%u ready_index=%u waiting_completion=%u pending_index=%u "
		"actual=%02x count_03bc=%02x parser_state_03c0=%04x rx_valid=%u slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		reason, pc, m_panel_e_ff_drain_next_index, m_panel_e_ff_drain_ready_valid ? 1 : 0,
		m_panel_e_ff_drain_ready_index, m_panel_e_ff_drain_waiting_completion ? 1 : 0,
		m_panel_e_ff_drain_pending_index, actual, lowmem_byte(0x03bc), lowmem_word(0x03c0),
		m_panel_c_rx_valid ? 1 : 0, lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));
}


bool asr10_boot_state::panel_e_try_inject(u32 pc, const char *reason)
{
	static constexpr u8 known_ring[] = {
		0x71, 0x7e, 0xfc, 0x74, 0x07, 0x74, 0x06, 0x74,
		0x05, 0x74, 0x04, 0x74, 0x03, 0x74, 0x02
	};

	if (!m_panel_e_ff_drain_known_ring_enabled || m_panel_e_ff_drain_aborted ||
		m_panel_e_ff_drain_done || !m_panel_e_ff_drain_ready_valid)
		return false;
	if (m_panel_e_ff_drain_waiting_completion)
		return false;
	if (m_panel_c_rx_valid)
	{
		panel_e_abort("rx_byte_still_pending_before_reply", pc);
		return false;
	}
	if (lowmem_word(0x03c0) != 0xb3ba)
	{
		panel_e_abort("parser_state_not_b3ba_before_reply", pc);
		return false;
	}
	if (m_panel_e_ff_drain_ready_index == 0 && (lowmem_byte(0x03bc) != 0x0e || lowmem_word(0x03ba) != 0x0379))
		return false;
	if (m_panel_e_ff_drain_ready_index >= std::size(known_ring))
	{
		panel_e_abort("ready_index_out_of_range", pc);
		return false;
	}

	m_panel_e_ff_drain_pending_index = m_panel_e_ff_drain_ready_index;
	m_panel_e_ff_drain_pending_count = lowmem_byte(0x03bc);
	m_panel_e_ff_drain_ready_valid = false;
	m_panel_e_ff_drain_waiting_completion = true;
	m_panel_c_parser_trace_done = false;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=diagnostic_completion_probe "
		"cycle=%u reason=%s pc=%06x expected_thrb=%02x parser_state_03c0=%04x "
		"rx_queued=ff count_03bc_before=%02x read_ptr_03ba=%04x write_ptr_03b8=%04x "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		m_panel_e_ff_drain_pending_index, reason, pc, known_ring[m_panel_e_ff_drain_pending_index],
		lowmem_word(0x03c0), m_panel_e_ff_drain_pending_count, lowmem_word(0x03ba),
		lowmem_word(0x03b8), lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));
	panel_c_queue_rx(0xff, "known_ring_ff_completion_probe", pc);
	return true;
}


void asr10_boot_state::panel_e_observe_thrb(u32 pc, u8 data)
{
	static constexpr u8 known_ring[] = {
		0x71, 0x7e, 0xfc, 0x74, 0x07, 0x74, 0x06, 0x74,
		0x05, 0x74, 0x04, 0x74, 0x03, 0x74, 0x02
	};

	if (!m_panel_e_ff_drain_known_ring_enabled || m_panel_e_ff_drain_aborted ||
		m_panel_e_ff_drain_done)
		return;
	if (m_panel_e_ff_drain_next_index >= std::size(known_ring))
	{
		logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=post_known_ring_thrb "
			"pc=%06x byte=%02x count_03bc=%02x parser_state_03c0=%04x slot0_state=%04x "
			"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
			pc, data, lowmem_byte(0x03bc), lowmem_word(0x03c0), lowmem_word(0x23d6),
			lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
		return;
	}

	const u8 index = m_panel_e_ff_drain_next_index;
	const u8 expected = known_ring[index];
	if (data != expected)
	{
		panel_e_abort("natural_thrb_mismatch", pc, data);
		return;
	}
	if (m_panel_e_ff_drain_ready_valid)
	{
		panel_e_abort("previous_thrb_not_replied_before_next_thrb", pc, data);
		return;
	}

	m_panel_e_ff_drain_ready_valid = true;
	m_panel_e_ff_drain_ready_index = index;
	m_panel_e_ff_drain_next_index++;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=observe_thrb cycle=%u "
		"pc=%06x expected_thrb=%02x actual_thrb=%02x parser_state_03c0=%04x "
		"count_03bc=%02x read_ptr_03ba=%04x write_ptr_03b8=%04x waiting_completion=%u "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		index, pc, expected, data, lowmem_word(0x03c0), lowmem_byte(0x03bc),
		lowmem_word(0x03ba), lowmem_word(0x03b8),
		m_panel_e_ff_drain_waiting_completion ? 1 : 0, lowmem_word(0x23d6),
		lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
	panel_e_try_inject(pc, "thrb_observed");
}


void asr10_boot_state::panel_e_note_completion(u32 pc, u16 previous_03bc, u16 current_03bc)
{
	static constexpr u8 known_ring[] = {
		0x71, 0x7e, 0xfc, 0x74, 0x07, 0x74, 0x06, 0x74,
		0x05, 0x74, 0x04, 0x74, 0x03, 0x74, 0x02
	};

	if (!m_panel_e_ff_drain_known_ring_enabled || m_panel_e_ff_drain_aborted ||
		!m_panel_e_ff_drain_waiting_completion)
		return;

	const u8 count_before = u8(previous_03bc >> 8);
	const u8 count_after = u8(current_03bc >> 8);
	if (count_before != m_panel_e_ff_drain_pending_count || count_after != u8(count_before - 1))
	{
		panel_e_abort("03bc_failed_to_decrement_by_one", pc);
		return;
	}

	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=completion_verified cycle=%u "
		"pc=%06x expected_thrb=%02x rhrb=ff count_03bc_before=%02x count_03bc_after=%02x "
		"parser_state_03c0=%04x read_ptr_03ba=%04x write_ptr_03b8=%04x slot0_state=%04x "
		"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		m_panel_e_ff_drain_pending_index, pc, known_ring[m_panel_e_ff_drain_pending_index],
		count_before, count_after, lowmem_word(0x03c0), lowmem_word(0x03ba),
		lowmem_word(0x03b8), lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));

	m_panel_e_ff_drain_waiting_completion = false;
	if (count_after == 0)
	{
		m_panel_e_ff_drain_done = true;
		logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=zero_crossing "
			"pc=%06x cycle=%u count_03bc_before=%02x count_03bc_after=00 "
			"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
			"node_14f4_00=%04x node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x\n",
			pc, m_panel_e_ff_drain_pending_index, count_before, lowmem_word(0x23d6),
			lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f4),
			lowmem_word(0x14f6), lowmem_word(0x14f8), lowmem_word(0x14fa));
		return;
	}

	panel_e_try_inject(pc, "previous_completion_settled");
}


void asr10_boot_state::panel_l_log_temporal(const char *event, u32 pc, u32 byte_address, u16 previous, u16 current)
{
	if (!m_panel_l_ff_drain_later_rings_enabled || machine().side_effects_disabled())
		return;

	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=%s pc=%06x previous_pc=%06x "
		"addr=%06x previous=%04x current=%04x setup_done=%u later_started=%u replies=%u "
		"zero_crossings=%u count_03bc=%02x read_ptr_03ba=%04x write_ptr_03b8=%04x "
		"idle_03c5=%02x prev_ring_byte=%02x current_ring_byte=%02x thrb_pc=%06x "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
		"node_14f4_exists=%u node_14f4_00=%04x node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x\n",
		event, pc, m_last_distinct_pc, byte_address, previous, current,
		m_panel_l_first_ring_setup_done ? 1 : 0, m_panel_l_later_started ? 1 : 0,
		m_panel_l_reply_count, m_panel_l_zero_crossing_count, lowmem_byte(0x03bc),
		lowmem_word(0x03ba), lowmem_word(0x03b8), lowmem_byte(0x03c5), m_panel_l_previous_byte,
		m_panel_l_current_byte, m_panel_l_current_thrb_pc, lowmem_word(0x23d6),
		lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6) == 0x89a2 ? 1 : 0,
		lowmem_word(0x14f4), lowmem_word(0x14f6), lowmem_word(0x14f8), lowmem_word(0x14fa));
}


void asr10_boot_state::panel_l_abort(const char *reason, u32 pc, u8 actual)
{
	if (!m_panel_l_ff_drain_later_rings_enabled || m_panel_l_aborted || m_panel_l_done)
		return;

	m_panel_l_aborted = true;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=abort reason=%s pc=%06x "
		"actual=%02x setup_done=%u later_started=%u waiting_completion=%u previous_completion=%u "
		"replies=%u zero_crossings=%u count_03bc=%02x parser_state_03c0=%04x rx_valid=%u "
		"prev_ring_byte=%02x current_ring_byte=%02x pending_byte=%02x pending_count=%02x "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		reason, pc, actual, m_panel_l_first_ring_setup_done ? 1 : 0,
		m_panel_l_later_started ? 1 : 0, m_panel_l_waiting_completion ? 1 : 0,
		m_panel_l_previous_completion_observed ? 1 : 0, m_panel_l_reply_count,
		m_panel_l_zero_crossing_count, lowmem_byte(0x03bc), lowmem_word(0x03c0),
		m_panel_c_rx_valid ? 1 : 0, m_panel_l_previous_byte, m_panel_l_current_byte,
		m_panel_l_pending_byte, m_panel_l_pending_count, lowmem_word(0x23d6),
		lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
}


void asr10_boot_state::panel_l_stop(const char *reason, u32 pc)
{
	if (!m_panel_l_ff_drain_later_rings_enabled || m_panel_l_done)
		return;

	m_panel_l_done = true;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=stop reason=%s pc=%06x "
		"setup_done=%u later_started=%u replies=%u zero_crossings=%u count_03bc=%02x "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
		"node_14f4_00=%04x node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x\n",
		reason, pc, m_panel_l_first_ring_setup_done ? 1 : 0, m_panel_l_later_started ? 1 : 0,
		m_panel_l_reply_count, m_panel_l_zero_crossing_count, lowmem_byte(0x03bc),
		lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6),
		lowmem_word(0x14f4), lowmem_word(0x14f6), lowmem_word(0x14f8), lowmem_word(0x14fa));
}


bool asr10_boot_state::panel_l_try_inject(u32 pc, const char *reason)
{
	if (!m_panel_l_ff_drain_later_rings_enabled || m_panel_l_aborted || m_panel_l_done)
		return false;
	if (m_panel_l_waiting_completion)
		return false;
	if (!m_panel_l_previous_completion_observed)
	{
		panel_l_abort("previous_completion_not_observed", pc);
		return false;
	}
	if (m_panel_c_rx_valid)
	{
		panel_l_abort("rx_byte_still_pending_before_reply", pc);
		return false;
	}
	if (lowmem_word(0x03c0) != 0xb3ba)
	{
		panel_l_abort("parser_state_not_b3ba_before_reply", pc);
		return false;
	}
	if (!lowmem_byte(0x03bc))
	{
		panel_l_abort("03bc_zero_before_reply", pc);
		return false;
	}
	if (m_panel_l_first_ring_setup_done)
		return false;
	if (!m_panel_l_first_ring_setup_done && m_panel_l_first_ring_index == 1 &&
		m_panel_l_current_byte == 0x71 && (lowmem_byte(0x03bc) != 0x0e || lowmem_word(0x03ba) != 0x0379))
		return false;
	if (m_panel_l_current_thrb_pc != 0x00f89aa4)
	{
		panel_l_abort("unexpected_thrb_source", pc);
		return false;
	}
	if (m_panel_l_reply_count >= ASR10_PANEL_L_MAX_REPLIES)
	{
		panel_l_abort("reply_safety_limit", pc);
		return false;
	}
	if (m_panel_l_later_started &&
		(u64(m_maincpu->total_cycles()) - m_panel_l_later_start_cycle) > ASR10_PANEL_L_MAX_CYCLES_AFTER_LATER_START)
	{
		panel_l_abort("cycle_safety_limit_after_later_start", pc);
		return false;
	}

	m_panel_l_pending_count = lowmem_byte(0x03bc);
	m_panel_l_pending_byte = m_panel_l_current_byte;
	m_panel_l_pending_thrb_pc = m_panel_l_current_thrb_pc;
	m_panel_l_waiting_completion = true;
	m_panel_l_previous_completion_observed = false;
	m_panel_l_reply_count++;
	m_panel_c_parser_trace_done = false;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=diagnostic_completion_probe "
		"reason=%s pc=%06x phase=%s reply=%u rx_queued=ff pending_byte=%02x previous_byte=%02x "
		"thrb_pc=%06x count_03bc_before=%02x parser_state_03c0=%04x read_ptr_03ba=%04x "
		"write_ptr_03b8=%04x slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
		"node_14f4_type=%04x\n",
		reason, pc, m_panel_l_first_ring_setup_done ? "later_ring" : "first_ring_setup",
		m_panel_l_reply_count, m_panel_l_pending_byte, m_panel_l_previous_byte,
		m_panel_l_pending_thrb_pc, m_panel_l_pending_count, lowmem_word(0x03c0),
		lowmem_word(0x03ba), lowmem_word(0x03b8), lowmem_word(0x23d6),
		lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
	panel_c_queue_rx(0xff, "later_rings_ff_completion_probe", pc);
	return true;
}


void asr10_boot_state::panel_l_observe_thrb(u32 pc, u8 data)
{
	static constexpr u8 known_ring[] = {
		0x71, 0x7e, 0xfc, 0x74, 0x07, 0x74, 0x06, 0x74,
		0x05, 0x74, 0x04, 0x74, 0x03, 0x74, 0x02
	};

	if (!m_panel_l_ff_drain_later_rings_enabled || m_panel_l_aborted || m_panel_l_done)
		return;
	if (pc != 0x00f89aa4)
	{
		panel_l_abort("unexpected_thrb_source", pc, data);
		return;
	}
	if (!m_panel_l_first_ring_setup_done)
	{
		if (m_panel_l_first_ring_index >= std::size(known_ring))
		{
			panel_l_abort("first_ring_index_out_of_range", pc, data);
			return;
		}
		const u8 expected = known_ring[m_panel_l_first_ring_index];
		if (data != expected)
		{
			panel_l_abort("first_ring_setup_thrb_mismatch", pc, data);
			return;
		}
		m_panel_l_first_ring_index++;
	}
	else if (!m_panel_l_later_started)
	{
		m_panel_l_later_started = true;
		m_panel_l_later_start_cycle = u64(m_maincpu->total_cycles());
		panel_l_log_temporal("later_ring_start", pc);
	}

	m_panel_l_previous_byte = m_panel_l_current_byte;
	m_panel_l_current_byte = data;
	m_panel_l_current_thrb_pc = pc;
	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=observe_thrb "
		"pc=%06x phase=%s byte=%02x previous_byte=%02x count_03bc=%02x parser_state_03c0=%04x "
		"read_ptr_03ba=%04x write_ptr_03b8=%04x waiting_completion=%u previous_completion=%u "
		"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x\n",
		pc, m_panel_l_first_ring_setup_done ? "later_ring" : "first_ring_setup",
		data, m_panel_l_previous_byte, lowmem_byte(0x03bc), lowmem_word(0x03c0),
		lowmem_word(0x03ba), lowmem_word(0x03b8), m_panel_l_waiting_completion ? 1 : 0,
		m_panel_l_previous_completion_observed ? 1 : 0, lowmem_word(0x23d6),
		lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6));
	if (m_panel_l_first_ring_setup_done && m_panel_l_final_idle_03c5_clear_seen && data == 0x74)
	{
		panel_l_log_temporal("natural_second_ring_thrb_after_final_idle_completion", pc);
		panel_l_stop("natural_second_ring_thrb_after_final_idle_completion", pc);
	}
	panel_l_try_inject(pc, "thrb_observed");
}


void asr10_boot_state::panel_l_note_completion(u32 pc, u16 previous_03bc, u16 current_03bc)
{
	static constexpr u8 known_ring[] = {
		0x71, 0x7e, 0xfc, 0x74, 0x07, 0x74, 0x06, 0x74,
		0x05, 0x74, 0x04, 0x74, 0x03, 0x74, 0x02
	};

	if (!m_panel_l_ff_drain_later_rings_enabled || m_panel_l_aborted || m_panel_l_done)
		return;
	if (!m_panel_l_waiting_completion)
	{
		if (!m_panel_l_first_ring_setup_done)
			return;
		panel_l_abort("unexpected_completion_without_pending_reply", pc);
		return;
	}

	const u8 count_before = u8(previous_03bc >> 8);
	const u8 count_after = u8(current_03bc >> 8);
	if (count_before != m_panel_l_pending_count || count_after != u8(count_before - 1))
	{
		panel_l_abort("03bc_failed_to_decrement_by_one", pc);
		return;
	}

	logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=completion_verified "
		"pc=%06x phase=%s reply=%u rhrb=ff pending_byte=%02x previous_byte=%02x "
		"count_03bc_before=%02x count_03bc_after=%02x parser_state_03c0=%04x "
		"read_ptr_03ba=%04x write_ptr_03b8=%04x slot0_state=%04x slot0_queue_head=%04x "
		"slot0_queue_tail=%04x node_14f4_type=%04x\n",
		pc, m_panel_l_first_ring_setup_done ? "later_ring" : "first_ring_setup",
		m_panel_l_reply_count, m_panel_l_pending_byte, m_panel_l_previous_byte,
		count_before, count_after, lowmem_word(0x03c0), lowmem_word(0x03ba),
		lowmem_word(0x03b8), lowmem_word(0x23d6), lowmem_word(0x23e4),
		lowmem_word(0x23e6), lowmem_word(0x14f6));

	m_panel_l_waiting_completion = false;
	m_panel_l_previous_completion_observed = true;
	if (count_after == 0)
	{
		const bool setup_crossing = !m_panel_l_first_ring_setup_done && m_panel_l_first_ring_index >= std::size(known_ring);
		if (!m_panel_l_first_ring_setup_done && !setup_crossing)
		{
			panel_l_abort("first_ring_zero_before_sequence_complete", pc);
			return;
		}
		m_panel_l_zero_slot0_before = lowmem_word(0x23d6);
		m_panel_l_zero_slot0_after = m_panel_l_zero_slot0_before;
		m_panel_l_zero_pending_f89ac2 = true;
		if (!setup_crossing)
			m_panel_l_zero_crossing_count++;
		logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=zero_crossing "
			"index=%u phase=%s pc=%06x count_03bc_before=%02x count_03bc_after=00 "
			"previous_ring_byte=%02x current_ring_byte=%02x thrb_source_pc=%06x "
			"slot0_state_before_f89ac2=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
			"node_14f4_exists=%u node_14f4_00=%04x node_14f4_02=%04x node_14f4_04=%04x "
			"node_14f4_06=%04x node_14f4_payload=%04x\n",
			m_panel_l_zero_crossing_count, setup_crossing ? "first_ring_setup" : "later_ring",
			pc, count_before, m_panel_l_previous_byte, m_panel_l_pending_byte,
			m_panel_l_pending_thrb_pc, m_panel_l_zero_slot0_before, lowmem_word(0x23e4),
			lowmem_word(0x23e6), lowmem_word(0x14f6) == 0x89a2 ? 1 : 0,
			lowmem_word(0x14f4), lowmem_word(0x14f6), lowmem_word(0x14f8),
			lowmem_word(0x14fa), lowmem_word(0x14f6));
		if (setup_crossing)
		{
			m_panel_l_first_ring_setup_done = true;
			panel_l_log_temporal("first_ring_setup_zero_complete", pc);
			if (m_panel_l_current_byte != 0x02)
			{
				panel_l_abort("final_setup_byte_not_02", pc, m_panel_l_current_byte);
				return;
			}
			if (m_panel_c_rx_valid)
			{
				panel_l_abort("rx_byte_pending_before_final_idle_probe", pc);
				return;
			}
			if (lowmem_word(0x03c0) != 0xb3ba)
			{
				panel_l_abort("parser_state_not_b3ba_before_final_idle_probe", pc);
				return;
			}
			m_panel_l_final_idle_probe_queued = true;
			m_panel_l_final_idle_probe_waiting = true;
			m_panel_l_reply_count++;
			m_panel_c_parser_trace_done = false;
			logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=final_idle_completion_probe "
				"pc=%06x reply=%u rx_queued=ff final_thrb_byte=%02x count_03bc=%02x "
				"idle_03c5=%02x parser_state_03c0=%04x read_ptr_03ba=%04x write_ptr_03b8=%04x\n",
				pc, m_panel_l_reply_count, m_panel_l_current_byte, lowmem_byte(0x03bc),
				lowmem_byte(0x03c5), lowmem_word(0x03c0), lowmem_word(0x03ba),
				lowmem_word(0x03b8));
			panel_c_queue_rx(0xff, "final_idle_completion_after_02", pc);
		}
		else if (m_panel_l_zero_crossing_count >= ASR10_PANEL_L_MAX_ZERO_CROSSINGS)
		{
			panel_l_stop("zero_crossing_safety_limit", pc);
		}
	}
	else
	{
		panel_l_try_inject(pc, "previous_completion_settled");
	}
}


static const char *pti_entry_name(u32 entry_id);


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
			dump_highview_code_range("download_retry_loop_ffc840_ffc8c0", 0x00ffc840, 0x00ffc8c0);
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
				log_esp_f973f0_entry(pc);
			if (byte_address == 0x0e8c && pc == 0x00f97580)
				log_esp_other_table_first_retry(pc);
			// filesystem-browser-map.md 4.26 TASK 6: HALL REVERB table-level
			// retry/give-up markers. Table match only (not record-scoped
			// like log_hall_reverb_event's other call sites) because retry
			// and give-up are attempt boundaries for the WHOLE table
			// transfer (f9740a restarts all record types on a mismatch),
			// not a single record.
			if (m_lowmem_shadow[0x0e8e >> 1] == 0x0001 && m_lowmem_shadow[(0x0e8e >> 1) + 1] == 0x0400)
			{
				if (byte_address == 0x0e8c)
					log_hall_reverb_event("retry_increment", pc, byte_address, data, mem_mask);
				if (byte_address == 0x0e8a)
					log_hall_reverb_event("give_up_flag_set", pc, byte_address, data, mem_mask);
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
					log_panel_b_enqueue(pc, previous, m_lowmem_shadow[offset]);
					if (panel_reply_experiment_enabled() && u8(m_lowmem_shadow[offset] >> 8) == 0x0e &&
						lowmem_word(0x03ba) == 0x0379 &&
						((m_panel_c_reply_71_zero_enabled && m_panel_c_reply_71_zero_armed &&
							!m_panel_c_reply_71_zero_injected) ||
							(m_panel_d1_reply_71_ff_enabled && m_panel_d1_reply_71_ff_armed &&
							!m_panel_d1_reply_71_ff_injected) ||
							(m_panel_d2_reply_71_7e_ff_enabled && m_panel_d2_reply_71_armed &&
							!m_panel_d2_reply_71_injected)))
					{
						const u8 injected = (m_panel_d1_reply_71_ff_enabled || m_panel_d2_reply_71_7e_ff_enabled) ? 0xff : 0x00;
						if (m_panel_d2_reply_71_7e_ff_enabled)
							m_panel_d2_reply_71_injected = true;
						else if (m_panel_d1_reply_71_ff_enabled)
							m_panel_d1_reply_71_ff_injected = true;
						else
							m_panel_c_reply_71_zero_injected = true;
						logerror("%s event=inject_after_natural_queue "
							"pc=%06x count_03bc=%02x write_ptr_03b8=%04x read_ptr_03ba=%04x "
							"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
							"node_14f4_type=%04x injected_rx=%02x parser_state_03c0=%04x\n",
							panel_reply_experiment_name(), pc, lowmem_byte(0x03bc),
							lowmem_word(0x03b8), lowmem_word(0x03ba),
							lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6),
							lowmem_word(0x14f6), injected, lowmem_word(0x03c0));
						panel_c_queue_rx(injected, (m_panel_d1_reply_71_ff_enabled || m_panel_d2_reply_71_7e_ff_enabled) ?
							"thrb_71_deferred_until_count_0e_ff" :
							"thrb_71_deferred_until_count_0e", pc);
					}
					if (m_panel_e_ff_drain_known_ring_enabled)
						panel_e_try_inject(pc, "natural_queue_stable_after_71");
					if (m_panel_l_ff_drain_later_rings_enabled && !m_panel_l_first_ring_setup_done &&
						m_panel_l_first_ring_index)
						panel_l_try_inject(pc, "natural_queue_stable_after_71");
					if (m_panel_l_ff_drain_later_rings_enabled && m_panel_l_first_ring_setup_done &&
						!m_panel_l_later_started)
						panel_l_log_temporal("second_or_later_ring_enqueue_start", pc, byte_address, previous,
							m_lowmem_shadow[offset]);
				}
				else if (pc == 0x00f89ab8)
				{
					log_panel_b_complete(pc, previous, m_lowmem_shadow[offset]);
					panel_e_note_completion(pc, previous, m_lowmem_shadow[offset]);
					panel_l_note_completion(pc, previous, m_lowmem_shadow[offset]);
					if (m_panel_d2_reply_71_7e_ff_enabled && m_panel_d2_reply_71_injected &&
						m_panel_d2_reply_7e_armed && !m_panel_d2_reply_7e_injected &&
						u8(m_lowmem_shadow[offset] >> 8) == 0x0d && lowmem_word(0x03ba) == 0x037a)
					{
						m_panel_d2_reply_7e_injected = true;
						m_panel_c_parser_trace_done = false;
						logerror("%s event=inject_after_7e_completion pc=%06x count_03bc=%02x "
							"write_ptr_03b8=%04x read_ptr_03ba=%04x slot0_state=%04x "
							"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_type=%04x "
							"injected_rx=ff parser_state_03c0=%04x\n",
							panel_reply_experiment_name(), pc, lowmem_byte(0x03bc),
							lowmem_word(0x03b8), lowmem_word(0x03ba), lowmem_word(0x23d6),
							lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6),
							lowmem_word(0x03c0));
						panel_c_queue_rx(0xff, "thrb_7e_after_count_0d_ff", pc);
					}
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
			if (m_panel_l_ff_drain_later_rings_enabled && byte_address == 0x03c4 &&
				ACCESSING_BITS_0_7 && pc == 0x00f89ace && m_panel_l_final_idle_probe_waiting)
			{
				const u8 previous_03c5 = u8(previous);
				const u8 current_03c5 = lowmem_byte(0x03c5);
				m_panel_l_final_idle_f89ace_seen = true;
				logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=final_idle_03c5_clear "
					"pc=%06x previous_03c5=%02x current_03c5=%02x count_03bc=%02x "
					"parser_state_03c0=%04x f89a9a_zero_seen=%u f89ace_seen=%u\n",
					pc, previous_03c5, current_03c5, lowmem_byte(0x03bc), lowmem_word(0x03c0),
					m_panel_l_final_idle_f89a9a_zero_seen ? 1 : 0,
					m_panel_l_final_idle_f89ace_seen ? 1 : 0);
				if (previous_03c5 != 0xff || current_03c5 != 0x00)
				{
					panel_l_abort("final_idle_03c5_not_ff_to_00", pc, current_03c5);
					return;
				}
				if (!m_panel_l_final_idle_f89a9a_zero_seen)
				{
					panel_l_abort("final_idle_clear_without_expected_pc_markers", pc);
					return;
				}
				m_panel_l_final_idle_probe_waiting = false;
				m_panel_l_final_idle_03c5_clear_seen = true;
			}
			if (pc == 0x00f89ac2 && byte_address == 0x23d6)
			{
				log_panel_b_wake(pc, byte_address, previous, m_lowmem_shadow[offset]);
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
				if (m_panel_l_ff_drain_later_rings_enabled && m_panel_l_zero_pending_f89ac2)
				{
					m_panel_l_zero_pending_f89ac2 = false;
					m_panel_l_zero_slot0_after = m_lowmem_shadow[offset];
					logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=f89ac2_result "
						"pc=%06x zero_index=%u slot0_state_before=%04x slot0_state_after=%04x "
						"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_00=%04x "
						"node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x\n",
						pc, m_panel_l_zero_crossing_count, previous, m_lowmem_shadow[offset],
						lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f4),
						lowmem_word(0x14f6), lowmem_word(0x14f8), lowmem_word(0x14fa));
					if (previous == 0x0202 && m_lowmem_shadow[offset] == 0x0002)
						m_panel_l_stop_after_dispatch = true;
				}
			}
			if (m_panel_l_ff_drain_later_rings_enabled)
			{
				if (byte_address >= 0x14f4 && byte_address <= 0x14fa)
				{
					const char *event = (byte_address == 0x14f6 && m_lowmem_shadow[offset] == 0x89a2) ?
						"node_14f4_payload_89a2_install" : "node_14f4_write";
					panel_l_log_temporal(event, pc, byte_address, previous, m_lowmem_shadow[offset]);
				}
				if (byte_address == 0x23d6 && m_lowmem_shadow[offset] == 0x0202)
					panel_l_log_temporal("slot0_transition_to_0202", pc, byte_address, previous, m_lowmem_shadow[offset]);
				if (byte_address == 0x23e4 || byte_address == 0x23e6)
					panel_l_log_temporal("slot0_queue_head_tail_write", pc, byte_address, previous,
						m_lowmem_shadow[offset]);
				if (pc == 0x00f8816e && byte_address == 0x23d6)
					panel_l_log_temporal("trap9_enqueue_bit7_clear", pc, byte_address, previous,
						m_lowmem_shadow[offset]);
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
	log_lowmem_service_context(true, byte_address, previous, m_lowmem_shadow[offset], data, mem_mask);
	log_f87f96_queue_write(byte_address, previous, m_lowmem_shadow[offset], data, mem_mask);
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

	if (m_fsb.enabled && !machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		log_fsb_field_write(pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask);
		// fb82a4 (boot/format sector loader): its own first instruction is
		// `move.b #$20,$49e.w` -- tag entry via that write, gated on this
		// exact PC (opcode-fetch taps are established not to fire for this
		// core; see the note near FC2068 in machine_start()).
		if (byte_address == 0x049e && pc == 0x00fb82a4)
			log_fsb_entry(FSB_ENTRY_FB82A4, pc);
		// fb846a (generic "load one FDC unit into $40e.w"): its first
		// instruction writes $4a9.w=5 (retry count). Called from fb82c0,
		// fb83d6, and fb834e's loop; the caller field in ASR10_FSB_ENTRY
		// (read from the stack at entry) distinguishes which -- fb834e
		// itself has no data write before the bsr, so it is identified only
		// by its callees' return address (0xfb8352), not a direct tap.
		if (byte_address == 0x04a9 && pc == 0x00fb846a)
			log_fsb_entry(FSB_ENTRY_FB846A, pc);
	}
	if (m_root_directory_trace_enabled && !machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		log_root_directory_table_write(pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask);
	}

	if (m_pti.enabled && m_pti.seen_f880fc && !machine().side_effects_disabled())
	{
		// `jsr (An)` and `trap #9` both push a genuine return-address/frame
		// word onto the stack; that write is what reaches this handler and
		// makes the pc-gated checks inside check_pti_sites() possible (the
		// instructions themselves have no other memory reference to tap).
		// Identified by PC alone, not byte_address, since the push lands
		// wherever SP currently is, not at a fixed field address.
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		check_pti_sites(pc);
		// Positive control: slot 5's resume code unconditionally executes
		// `clr.w $cdb0.w` (007aa, a genuine, always-reached lowmem write --
		// proof this specific dispatch is live and its SP is what it is
		// right here) immediately followed by `jsr $00007cc4.l` (0077ae, a
		// direct absolute-long call, unconditional, no branch in between).
		// If the stack really is in lowmem, 0077ae's own return-address
		// push (to 0077b4, the next instruction) must also reach this
		// handler; if it does not despite 0077aa firing, the stack is not
		// where this technique assumes.
		if (pc == 0x000077aa)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			logerror("ASR10_PTI_CONTROL_BEFORE_JSR pc=%06x sp=%06x region=%s active_slot=%u panel=\"%s\"\n",
				pc, sp, address_region_guess(sp), m_dispatcher_rte_frame_slot, m_panel_text);
			log_pti_stack_probe("before_known_jsr_0077ae", pc);
		}
		if (pc == 0x000077ae)
		{
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			logerror("ASR10_PTI_CONTROL_JSR_PUSH pc=%06x sp=%06x pushed_word_address=%06x "
				"pushed_word_value=%04x mem_mask=%04x expected_return_address_00077b4 region=%s panel=\"%s\"\n",
				pc, sp, byte_address, data, mem_mask, address_region_guess(sp), m_panel_text);
		}

		// Slot 0's six named vectors (filesystem-browser-map.md 4.9's
		// authoritative disassembly of ae0c-ae91). All six `jsr $xxxx.w`
		// sites are 4-byte instructions; each fires here via its own
		// genuine return-address push, pc-gated exactly like the
		// positive control above (byte_address/data below are that
		// actual write, not a re-derived value). Gate taps
		// (ae34/ae56/ae62/ae72/ae7a) capture the branch-condition inputs
		// immediately before each conditional call, using the same
		// technique.
		if (pc == 0x0000ae1a)
			log_pti_vector_call(0, "87f2", pc, 0x00ff87f2, byte_address, m_lowmem_shadow[offset],
				"and.b#$80,D0 (ae14) != 0 -- unconditional once looped");
		if (pc == 0x0000ae2a)
			log_pti_vector_call(1, "bc8e", pc, 0x00ffbc8e, byte_address, m_lowmem_shadow[offset],
				"unconditional once ae22 routine entered");
		if (byte_address == 0x0cdc && pc == 0x0000ae34)
			log_pti_gate("ae34_d2_to_cdc", pc, m_lowmem_shadow[offset], 0);
		if (pc == 0x0000ae5e)
			log_pti_vector_call(2, "a26e_site1", pc, 0x00ffa26e, byte_address, m_lowmem_shadow[offset],
				"ae54 beq(D0!=0) + ae5c bne($183.w==3)");
		if (byte_address == 0x0330 && pc == 0x0000ae4c)
			log_pti_vector_return(5, "9650_via_ae4a_bsr", pc);
		if (pc == 0x0000ae68)
			log_pti_vector_call(3, "8864", pc, 0x00ff8864, byte_address, m_lowmem_shadow[offset],
				"ae66 beq($31c.w!=0)");
		if (pc == 0x0000ae80)
			log_pti_vector_call(2, "a26e_site2", pc, 0x00ffa26e, byte_address, m_lowmem_shadow[offset],
				"ae38 bmi(D2<0) + ae78 bne($183.w==3) + ae7e beq($306.w!=0)");
		if (byte_address == 0x0cda && pc == 0x0000ae84)
			log_pti_vector_return(2, "a26e_site2", pc);
		if (pc == 0x0000ae8e)
			log_pti_vector_call(5, "9650", pc, 0x00ff9650, byte_address, m_lowmem_shadow[offset],
				"bsr from ae4a or ae88 -- see caller field in the log line's a-registers/stack, not distinguished here");
		// trap #4 at 002b2a: a genuine exception-frame push, tappable by
		// PC alone (lands wherever the supervisor stack is, not a fixed
		// field address) -- same technique as trap #9 in 4.8.
		if (pc == 0x00002b2a)
			log_pti_trap4(pc);
		// 007000 investigation (filesystem-browser-map.md 4.11): trap #2
		// (7000) is a genuine exception push, tappable by PC alone. 7002
		// (`bcs $702e`) is register/flag-only with no memory reference
		// (same limitation as ae18/f894a8) -- 7004's write (only reached
		// if carry was clear) is used as the "trap #2 succeeded" proxy
		// instead. 700a/7010 are the node+2/node+4 writes themselves,
		// PC-gated since the target address depends on A5, not fixed.
		// 701e/702a are trap #12/#14, again genuine exception pushes.
		if (pc == 0x00007000)
			log_pti_trap2_entry(pc);
		if (byte_address == 0x22bc && pc == 0x00007004)
			log_pti_trap2_success_observation(pc);
		if (pc == 0x0000700a)
			log_pti_node_field_write("node+2=0x1a", pc, byte_address, m_lowmem_shadow[offset]);
		if (pc == 0x00007010)
			log_pti_node_field_write("node+4=A6", pc, byte_address, m_lowmem_shadow[offset]);
		if (pc == 0x0000701e)
			log_pti_trap12_or_14(pc, 12);
		if (pc == 0x0000702a)
			log_pti_trap12_or_14(pc, 14);
		// filesystem-browser-map.md 4.13: the three static trap #13
		// callers outside its own vector-46 nested call. Entry taps on
		// the trap instruction's own exception-frame push (same
		// technique as trap #4 above); return taps on the genuine
		// lowmem write immediately after the trap, where one exists
		// (site 2, f89b54, has none -- see low_rom_or_lowmem_r instead).
		if (pc == 0x00f883ac)
			log_pti_trap13_call(pc, 0);
		if (pc == 0x00f883ae)
			log_pti_trap13_return(pc, 0);
		if (pc == 0x00f88df8)
			log_pti_trap13_call(pc, 1);
		if (pc == 0x00f88dfa)
			log_pti_trap13_return(pc, 1);
		if (pc == 0x00f89b54)
			log_pti_trap13_call(pc, 2);
		if (pc == 0x00f87f82)
			log_pti_f87f82_correlated(pc);
		// filesystem-browser-map.md 4.13 TASK 4: broad net on the queue
		// pointer cell itself, independent of which PC touches it -- did
		// $dc.w change post-gate, or get read by some caller other than
		// the three known trap #13 sites (no static writer was found for
		// it, consistent with the already-established bulk-copy pattern).
		if (byte_address == 0x00dc)
			log_pti_dc_pointer_write(pc, m_lowmem_shadow[offset]);
		// filesystem-browser-map.md 4.14: entry taps for every distinct
		// static path into f87f3e/f87f80. Register-only entry
		// instructions are tapped at their first genuine memory access
		// instead (same technique used throughout this driver).
		if (pc == 0x00f87f7a)
			log_pti_f87f80_direct(1, pti_entry_name(1), pc);   // trap #1 direct
		if (pc == 0x00f880ba)
			log_pti_f87f80_direct(2, pti_entry_name(2), pc);   // trap #5 (falls into #6's body)
		if (pc == 0x00f8810c)
			log_pti_f87f80_direct(3, pti_entry_name(3), pc);   // trap #7
		if (pc == 0x00f8805a)
			log_pti_trap15(pc);                                 // trap #15
		if (pc == 0x00f881d0)
			log_pti_entry_stash(5, pti_entry_name(5), pc);      // f87f3e via trap #12 bsr
		if (pc == 0x00f881f0)
			log_pti_entry_stash(6, pti_entry_name(6), pc);      // f87f3e via trap #14 jsr
		if (pc == 0x00f89a28)
			log_pti_entry_stash(7, pti_entry_name(7), pc);      // f87f3e via f89a28
		if (pc == 0x00f89a4e)
			log_pti_entry_stash(8, pti_entry_name(8), pc);      // f87f3e via f89a4e
		if (pc == 0x00f89a68)
			log_pti_entry_stash(9, pti_entry_name(9), pc);      // f87f3e via f89a68
		if (pc == 0x00f87f40)
			log_pti_f87f3e_body_entry(pc);
	}

	// filesystem-browser-map.md 4.15: early-epoch instrumentation, gated
	// purely on `enabled` (from reset) -- f880fc/seen_f880fc is proven too
	// late for the calibration-transition search (it is a PC inside trap
	// #6's own body, not a universal post-tuning landmark).
	if (m_pti.enabled && !machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		if (pc == 0x00f88134)
			log_pti_trap8(pc);
		if (pc == 0x00f88344)
			log_pti_ca_list_decrement(pc);
		if (pc == 0x00f88352)
			log_pti_ca_list_callback(pc);
		const u32 slot0_ptr = u32(lowmem_word(0x000c6)) & 0xffff;
		if (byte_address == (slot0_ptr + 2) || byte_address == (slot0_ptr + 3))
			log_pti_slot0_ready_bit_write(pc, byte_address, m_lowmem_shadow[offset]);
		// filesystem-browser-map.md 4.16: node+2=0x16 / 0xd10a producers,
		// and the $d6.w destination cell.
		if (pc == 0x00f88e2a)
			log_pti_node16_producer(pc, "f88e2a");
		if (pc == 0x00f9068a)
			log_pti_node16_producer(pc, "f9068a");
		if (pc == 0x00f942f2)
			log_pti_node16_producer(pc, "f942f2");
		if (pc == 0x00012f88)
			log_pti_d10a_producer(pc);
		if (byte_address == 0x00d6)
			log_pti_d6_pointer_write(pc, m_lowmem_shadow[offset]);
		// filesystem-browser-map.md 4.17: $00ca root, timer fields, and
		// the corrected 012efc-012f94 dispatcher family entries.
		if (byte_address == 0x00ca)
			log_pti_ca_root_write(pc, m_lowmem_shadow[offset]);
		if (byte_address == 0x14d4)
			log_pti_timer_field_write("14d4", pc, byte_address, m_lowmem_shadow[offset]);
		if (byte_address == 0x14d6 || byte_address == 0x14d8)
			log_pti_timer_field_write("14d6", pc, byte_address, m_lowmem_shadow[offset]);
		if (pc == 0x00012f24)
			log_pti_12_family_entry("12f24", pc);
		if (pc == 0x00012f66)
			log_pti_12_family_entry("12f66", pc);
		if (pc == 0x00012f76)
			log_pti_12_family_entry("12f76", pc);
		if (pc == 0x00012f7e)
			log_pti_12_family_entry("12f7e", pc);
		// filesystem-browser-map.md 4.18: ae10-ae20 loop, writes side
		// (trap #5's own exception push at ae12; jsr $87f2.w's return-
		// address push at ae1a).
		if (pc == 0x0000ae12)
			log_pti_ae12_before(pc);
		if (pc == 0x0000ae1a)
			log_pti_ae1a_taken(pc);
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
		if (m_fsb.enabled && !machine().side_effects_disabled())
		{
			// "The low-level FDC command issue routine" for this driver is
			// this write path itself (fc4001, the aux/command register) --
			// every ROM-side command primitive (fb8cfc/fb8cda/fb7c5a/fb7bc8/
			// fb7c3c/fb7ca6/fb8dd8) ultimately reaches here. Logged directly,
			// not via a PC-proxy, since this IS the real issue point.
			log_fsb_entry(FSB_ENTRY_FDC_CMD, pc);
		}
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
	log_cpu_context(pc);

	// The real SCN2681 register file (ACR, CTU/CTL preload and start/stop
	// counter commands, MR/CR/SR, RHR/THR) lives in m_duart now; only the
	// panel-B byte-transport taps and the unmodeled external-input stub
	// below stay hand-written here.
	u16 raw_data = ACCESSING_BITS_0_7 ? m_duart->read(word) : 0;
	// ISR visibility is shared IRQ-routing state (RxRDYB and/or counter-ready),
	// so it uses the broader duart_irq_model_enabled() gate. SRB/RHRB below
	// stay on panel_reply_experiment_enabled() only: RX byte delivery is
	// unrelated to the counter/timer experiment and must not change when it
	// alone is enabled.
	if (duart_irq_model_enabled() && ACCESSING_BITS_0_7 && address == 0x00fc480a &&
		(m_panel_c_rx_valid || m_panel_c_isr))
		raw_data = m_panel_c_isr;
	if (panel_reply_experiment_enabled() && ACCESSING_BITS_0_7)
	{
		if (address == 0x00fc4812 && m_panel_c_rx_valid)
			raw_data = m_panel_c_srb;
		else if (address == 0x00fc4816)
			raw_data = m_panel_c_rx_valid ? m_panel_c_rx_byte : 0;
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
			log_panel_b_rhrb(pc, u8(data),
				panel_reply_experiment_enabled() ? m_panel_c_srb : u8(m_duart->read(0x09)),
				panel_reply_experiment_enabled() ? m_panel_c_isr : u8(m_duart->read(0x05)));
	}
	if (panel_reply_experiment_enabled() && !machine().side_effects_disabled() &&
		address == 0x00fc4816 && ACCESSING_BITS_0_7 && m_panel_c_rx_valid)
	{
		logerror("%s event=rhrb_pop pc=%06x byte=%02x "
			"srb_before=%02x isr_before=%02x imr=%02x count_03bc=%02x parser_state_03c0=%04x\n",
			panel_reply_experiment_name(), pc, m_panel_c_rx_byte, m_panel_c_srb, m_panel_c_isr, m_panel_c_imr,
			lowmem_byte(0x03bc), lowmem_word(0x03c0));
		if (m_panel_c_parser_trace_enabled && !m_panel_c_parser_trace_done && pc == 0x00ffb242 &&
			(m_panel_c_rx_byte == 0x00 || m_panel_c_rx_byte == 0xff))
		{
			m_panel_c_parser_trace_active = true;
			m_panel_c_parser_trace_count = 0;
			m_panel_c_parser_trace_last_pc = 0xffffffffU;
			log_panel_c_parser_trace(pc, m_panel_c_rx_byte == 0xff ? "start_after_rhrb_ff" : "start_after_rhrb_00");
			m_pc_timer->adjust(attotime::from_ticks(1, m_maincpu->clock()), 0,
				attotime::from_ticks(1, m_maincpu->clock()));
		}
		m_panel_c_rx_valid = false;
		m_panel_c_srb &= ~0x01;
		m_panel_c_isr &= ~0x20;
		panel_c_update_irq6("rhrb_pop", pc);
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
	log_cpu_context(pc);
	if (duart_irq_model_enabled() && address == 0x00fc480b && ACCESSING_BITS_0_7 &&
		!machine().side_effects_disabled())
	{
		const u8 previous_imr = m_panel_c_imr;
		m_panel_c_imr = u8(data);
		logerror("%s event=imr_write pc=%06x previous_imr=%02x "
			"new_imr=%02x isr=%02x active=%02x rx_active=%02x counter_active=%02x rx_valid=%u\n",
			panel_reply_experiment_name(), pc, previous_imr, m_panel_c_imr, m_panel_c_isr, m_panel_c_isr & m_panel_c_imr,
			m_panel_c_isr & m_panel_c_imr & 0x20, m_panel_c_isr & m_panel_c_imr & 0x08,
			m_panel_c_rx_valid ? 1 : 0);
		panel_c_update_irq6("imr_write", pc);
	}
	if (ACCESSING_BITS_0_7)
		m_duart->write(word, u8(data));
	if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
	{
		const u8 character = u8(data);
		log_panel_b_thrb(pc, character);
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
		if (m_panel_autorespond_enabled && !machine().side_effects_disabled())
		{
			const char *const thrb_source = (pc == 0x00f89cb0) ? "f89cb0" :
				(pc == 0x00f89aa4) ? "f89aa4" : "other";
			m_panel_autorespond_scheduled_count++;
			logerror("ASR10_DIAG_PANEL_AUTORESPOND event=schedule_response seq=%u pc=%06x source=%s "
				"byte=%02x count_03bc=%02x idle_03c5=%02x\n",
				m_panel_autorespond_scheduled_count, pc, thrb_source, character,
				lowmem_byte(0x03bc), lowmem_byte(0x03c5));
			m_panel_autorespond_timer->adjust(attotime::from_ticks(4, m_maincpu->clock()), s32(pc));
		}
		if (m_panel_e_ff_drain_known_ring_enabled && !machine().side_effects_disabled() &&
			pc == 0x00f89aa4)
			panel_e_observe_thrb(pc, character);
		if (m_panel_l_ff_drain_later_rings_enabled && !machine().side_effects_disabled())
		{
			if (pc == 0x00f89aa4)
				panel_l_observe_thrb(pc, character);
			else if (pc == 0x00f89cb0 || pc == 0x00f89c48)
				panel_l_log_temporal("ignored_direct_or_polled_thrb", pc);
			else
				panel_l_abort("unexpected_thrb_source", pc, character);
		}
		if (panel_reply_experiment_enabled() && !machine().side_effects_disabled() &&
			pc == 0x00f89aa4 && character == 0x71 &&
			((m_panel_c_reply_71_zero_enabled && !m_panel_c_reply_71_zero_armed) ||
				(m_panel_d1_reply_71_ff_enabled && !m_panel_d1_reply_71_ff_armed) ||
				(m_panel_d2_reply_71_7e_ff_enabled && !m_panel_d2_reply_71_armed)))
		{
			if (m_panel_d2_reply_71_7e_ff_enabled)
				m_panel_d2_reply_71_armed = true;
			else if (m_panel_d1_reply_71_ff_enabled)
				m_panel_d1_reply_71_ff_armed = true;
			else
				m_panel_c_reply_71_zero_armed = true;
			logerror("%s event=trigger_thrb_71_armed pc=%06x "
				"byte=71 count_03bc=%02x slot0_state=%04x slot0_queue_head=%04x "
				"slot0_queue_tail=%04x node_14f4_type=%04x\n",
				panel_reply_experiment_name(), pc, lowmem_byte(0x03bc), lowmem_word(0x23d6), lowmem_word(0x23e4),
				lowmem_word(0x23e6), lowmem_word(0x14f6));
		}
		if (m_panel_d2_reply_71_7e_ff_enabled && !machine().side_effects_disabled() &&
			pc == 0x00f89aa4 && character == 0x7e && m_panel_d2_reply_71_injected &&
			!m_panel_d2_reply_7e_armed)
		{
			m_panel_d2_reply_7e_armed = true;
			logerror("%s event=trigger_thrb_7e_armed pc=%06x byte=7e count_03bc=%02x "
				"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
				"node_14f4_type=%04x parser_state_03c0=%04x\n",
				panel_reply_experiment_name(), pc, lowmem_byte(0x03bc), lowmem_word(0x23d6),
				lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f6),
				lowmem_word(0x03c0));
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

	log_fsb_milestone_check_panel_text(pc, data, m_panel_text_length);

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
		// filesystem-browser-map.md 4.15 TASK 1: unconditional (not gated
		// on the too-late f880fc landmark) display-timeline entry, active
		// whenever the PTI flag is on, from reset.
		if (m_pti.enabled)
		{
			logerror("ASR10_PTI_PANEL_TIMELINE time=%s text=\"%s\" first_char_pc=%06x "
				"last_char_pc=%06x active_slot=%u\n",
				machine().time().to_string(), m_panel_text, m_panel_msg_first_pc,
				m_panel_msg_last_pc, m_dispatcher_rte_frame_slot);
		}
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
			log_error009_context("panel_error009_text",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
				m_lowmem_shadow[0x00c0 >> 1], 0xffff);
		}
		if (strstr(m_panel_text, "ERROR 139 - REBOOT ?"))
		{
			log_error009_context("panel_error139_text",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
				m_lowmem_shadow[0x00c0 >> 1], 0xffff);
		}
		if (strstr(m_panel_text, "EFFECT DOWNLOAD FAILED"))
		{
			log_error009_context("panel_effect_download_failed_text",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
				m_lowmem_shadow[0x00c0 >> 1], 0xffff);
		}
		if (strstr(m_panel_text, "ERROR 032 - REBOOT ?"))
		{
			log_error009_context("panel_error032_text",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff,
				m_lowmem_shadow[0x00c0 >> 1], 0xffff);
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
			dump_loaded_code_range("task1_rom_f840a0_f84120", 0x00f840a0, 0x00f84120);
			dump_loaded_code_range("task1_lowmem_000380_000400", 0x00000380, 0x00000400);
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
			dump_root_directory_history(pc, identity, depth_before);
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


void asr10_boot_state::dump_root_directory_history(u32 trigger_pc, u32 identity, u8 depth_before)
{
	dump_root_directory_table_summary();

	const u32 sp = u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff;
	osd_printf_info("ASR10_ROOT_DIRECTORY_SELECTOR trigger_pc=%06x identity=%06x "
		"depth_before=%u previous_pc=%06x caller=%06x sr=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x "
		"a0=%06x a1=%06x a2=%06x a3=%06x a4=%06x a5=%06x a6=%06x sp=%06x "
		"stack=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
		"low04b0=%02x low04b1=%02x low04b2=%02x low04b3=%02x low04be=%04x low04bf=%02x "
		"table_nonzero=%u first_zero=%u\n",
		trigger_pc, identity, depth_before, m_last_distinct_pc, read_stack_long(sp) & 0x00ffffff,
		u16(m_maincpu->state_int(M68K_SR)),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff,
		sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8),
		read_stack_long(sp + 12), read_stack_long(sp + 16), read_stack_long(sp + 20),
		read_stack_long(sp + 24), read_stack_long(sp + 28),
		lowmem_byte(0x04b0), lowmem_byte(0x04b1), lowmem_byte(0x04b2),
		lowmem_byte(0x04b3), lowmem_word(0x04be), lowmem_byte(0x04bf),
		m_root_directory_nonzero_first_word_count, m_root_directory_first_zero_index);

	for (u32 i = 0; i < m_root_directory_history_count; i++)
	{
		const u32 slot = (m_root_directory_history_pos + ROOT_DIRECTORY_HISTORY_LIMIT -
			m_root_directory_history_count + i) % ROOT_DIRECTORY_HISTORY_LIMIT;
		osd_printf_info("ASR10_ROOT_DIRECTORY_HISTORY index=%u pc=%06x opcode=%04x "
			"d0=%08x d1=%08x a0=%06x a2=%06x sp=%06x\n",
			i, m_root_directory_history_pc[slot], m_root_directory_history_opcode[slot],
			m_root_directory_history_d0[slot], m_root_directory_history_d1[slot],
			m_root_directory_history_a0[slot], m_root_directory_history_a2[slot],
			m_root_directory_history_sp[slot]);
	}

	for (u32 i = 0; i < m_root_directory_a2_change_count; i++)
	{
		const u32 slot = (m_root_directory_a2_change_pos + ROOT_DIRECTORY_HISTORY_LIMIT -
			m_root_directory_a2_change_count + i) % ROOT_DIRECTORY_HISTORY_LIMIT;
		osd_printf_info("ASR10_ROOT_DIRECTORY_A2_CHANGE index=%u pc=%06x opcode=%04x "
			"previous=%06x current=%06x d0=%08x a0=%06x\n",
			i, m_root_directory_a2_change_pc[slot], m_root_directory_a2_change_opcode[slot],
			m_root_directory_a2_change_previous[slot], m_root_directory_a2_change_current[slot],
			m_root_directory_a2_change_d0[slot], m_root_directory_a2_change_a0[slot]);
	}

	for (u32 pc = 0x00ff8840; pc <= 0x00ff8870; pc += 2)
	{
		osd_printf_info("ASR10_ROOT_DIRECTORY_CODE pc=%06x word=%04x\n",
			pc, read_program_word(pc));
	}
	for (u32 pc = 0x00ffa640; pc <= 0x00ffa660; pc += 2)
	{
		osd_printf_info("ASR10_ROOT_DIRECTORY_CODE pc=%06x word=%04x\n",
			pc, read_program_word(pc));
	}
	for (u32 address = 0x00ffcb10; address <= 0x00ffcb40; address += 2)
	{
		osd_printf_info("ASR10_ROOT_DIRECTORY_SELECTOR_TABLE address=%06x word=%04x%s\n",
			address, read_program_word(address), address == 0x00ffcb2c ? " selected=1" : "");
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
	dump_root_directory_table_summary();
	for (u32 index = 0; index != 16; index++)
		dump_root_directory_entry(index < 14 ? "final_nonzero_window" : "final_zero_window", index);
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


void asr10_boot_state::dump_root_directory_entry(const char *tag, u32 index)
{
	if (index >= 40)
		return;
	const u32 base = 0x0544 + index * 0x1a;
	std::string bytes;
	for (u32 i = 0; i < 0x1a; i++)
	{
		if (i)
			bytes += ' ';
		bytes += util::string_format("%02x", lowmem_byte(base + i));
	}
	std::string name;
	for (u32 i = 2; i < 15; i++)
	{
		const u8 ch = lowmem_byte(base + i);
		name += (ch >= 0x20 && ch <= 0x7e) ? char(ch) : '.';
	}
	osd_printf_info("ASR10_ROOT_DIRECTORY_ENTRY tag=%s index=%u address=%06x first_word=%04x "
		"type_byte=%02x name=\"%s\" bytes=\"%s\"\n",
		tag, index, base, lowmem_word(base), lowmem_byte(base + 1),
		name.c_str(), bytes.c_str());
}


void asr10_boot_state::dump_root_directory_table_summary()
{
	u16 first_zero = 0xffff;
	u16 nonzero = 0;
	u16 last_nonzero = 0xffff;
	for (u32 index = 0; index < 40; index++)
	{
		const u16 first_word = lowmem_word(0x0544 + index * 0x1a);
		if (first_word)
		{
			nonzero++;
			last_nonzero = index;
		}
		else if (first_zero == 0xffff)
			first_zero = index;
	}
	m_root_directory_nonzero_first_word_count = nonzero;
	m_root_directory_first_zero_index = first_zero;
	osd_printf_info("ASR10_ROOT_DIRECTORY_TABLE_SUMMARY nonzero_first_words=%u first_zero_index=%u "
		"last_nonzero_index=%u\n",
		nonzero, first_zero, last_nonzero);
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
void asr10_boot_state::log_esp_first_pass_write(u32 pc, u32 byte_address, u8 data)
{
	m_esp_first_pass_write_seq++;
	const u32 a3 = u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff;
	logerror("ASR10_ESP_FIRST_PASS_WRITE seq=%u pc=%06x dest_address=%06x written=%02x "
		"source_rom_address=%06x table_base_0e8e=%04x%04x record_type_d3=%02x "
		"record_count_d5=%02x record_param_d1=%02x record_index_d6=%02x "
		"a0=%06x a1=%06x a2=%06x a3=%06x a4=%06x a5=%06x a6=%06x "
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x\n",
		m_esp_first_pass_write_seq, pc, byte_address, data, a3 - 1,
		m_lowmem_shadow[0x0e8e >> 1], m_lowmem_shadow[(0x0e8e >> 1) + 1],
		u32(m_maincpu->state_int(M68K_D3)) & 0xff, u32(m_maincpu->state_int(M68K_D5)) & 0xff,
		u32(m_maincpu->state_int(M68K_D1)) & 0xff, u32(m_maincpu->state_int(M68K_D6)) & 0xff,
		u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff, a3,
		u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)));
}


// filesystem-browser-map.md 4.24 TASK 7: proves the adapter forwards the
// select/commit writes (f97776's "move.b D1,(A0,D4.w)", D4=0x100/0x140/0x180
// selecting FC3101/FC3141/FC3181) with the correct record index into the
// real es5510_device, for the two indices (0 and 58) whose collision this
// round's integration is meant to resolve. host_offset is derived with the
// same formula used throughout this section:
// (cpu_byte_address - 0xFC3001) >> 1.
void asr10_boot_state::log_esp_select_commit(u32 pc, u32 byte_address, u8 data)
{
	const u32 host_offset = (byte_address - 0x00fc3001) >> 1;
	const char *const name =
		host_offset == 0x80 ? "read_select_gpr_instr" :
		host_offset == 0xa0 ? "write_select_gpr" :
		host_offset == 0xc0 ? "write_select_instr" : "unknown";
	m_esp_select_commit_log_count++;
	logerror("ASR10_ESP_SELECT_COMMIT seq=%u pc=%06x byte_address=%06x host_offset=%02x "
		"register=%s record_index=%u\n",
		m_esp_select_commit_log_count, pc, byte_address, host_offset, name, data);
}


// filesystem-browser-map.md 4.24 TASK 4: proves, from inside the wrapper
// itself (not just an independent recomputation from address), that the
// single-word FC3101/FC3141/FC3181 ranges' map-relative offset (always 0)
// is being discarded and the fixed absolute ES5510 host offset (0x80/0xa0/
// 0xc0) is what actually reaches host_w. Bounded to record indices 0 and
// 58 by the caller, so this adds no per-retry volume across a 180s run.
void asr10_boot_state::log_esp_select_forward(u32 byte_address, u32 word_address, offs_t map_relative_offset,
	u32 fixed_offset, u8 data, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_esp_select_commit_log_count++;
	logerror("ASR10_ESP_SELECT_FORWARD seq=%u pc=%06x cpu_byte_address=%06x word_address=%06x "
		"map_relative_offset=%02x fixed_host_offset=%02x record_index=%u mem_mask=%04x\n",
		m_esp_select_commit_log_count, pc, byte_address, word_address,
		map_relative_offset, fixed_offset, data, mem_mask);
}


// filesystem-browser-map.md 4.25 (observation-only): renders a bounded
// live-memory window as both hex bytes and printable ASCII, via the
// side-effect-free general bus reader (read_program_word) so it works
// regardless of which region (ROM, lowmem, FC-range, etc.) base_address
// falls in. Read word-at-a-time; length_bytes is rounded up to the next
// even number if odd.
void asr10_boot_state::dump_memory_window(const char *tag, u32 base_address, u32 length_bytes)
{
	std::string hex;
	std::string ascii;
	for (u32 i = 0; i < length_bytes; i += 2)
	{
		const u16 word = read_program_word((base_address + i) & 0x00ffffff);
		const u8 hi = u8(word >> 8);
		const u8 lo = u8(word & 0xff);
		hex += util::string_format("%02x%02x", hi, lo);
		ascii += (hi >= 0x20 && hi < 0x7f) ? char(hi) : '.';
		ascii += (lo >= 0x20 && lo < 0x7f) ? char(lo) : '.';
	}
	logerror("ASR10_ESP_OTHER_TABLE_MEMDUMP tag=%s base=%06x length=%u hex=\"%s\" ascii=\"%s\"\n",
		tag, base_address, length_bytes, hex.c_str(), ascii.c_str());
}


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
void asr10_boot_state::log_esp_f973f0_entry(u32 pc)
{
	if (m_esp_f973f0_entry_log_count >= 20)
		return;
	m_esp_f973f0_entry_log_count++;
	const bool known_table = fc3000_verify_table_match();
	const u32 sp = u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff;
	const u32 caller_return_address = read_program_word(sp) << 16 | read_program_word(sp + 2);
	const u32 a3 = u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff;
	const u32 table_ptr_0e8e = (u32(m_lowmem_shadow[0x0e8e >> 1]) << 16) | m_lowmem_shadow[(0x0e8e >> 1) + 1];
	logerror("ASR10_ESP_F973F0_ENTRY seq=%u pc=%06x known_fixed_table=%u caller_return_address=%06x "
		"entry_a3=%06x lowmem_0e82=%04x lowmem_0e8c=%04x lowmem_0e8e=%08x "
		"a0=%08x a1=%08x a2=%08x a4=%08x a5=%08x a6=%08x "
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x sr=%04x "
		"note=entry_a3_is_the_actual_register_value_lowmem_0e7e_is_not_reconstructed_here_"
		"since_only_the_high_word_of_move_l_a3_e7e_w_may_have_been_written_at_this_point\n",
		m_esp_f973f0_entry_log_count, pc, known_table ? 1u : 0u, caller_return_address,
		a3, m_lowmem_shadow[0x0e82 >> 1], m_lowmem_shadow[0x0e8c >> 1], table_ptr_0e8e,
		u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u16(m_maincpu->state_int(M68K_SR)));
}


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
void asr10_boot_state::log_esp_other_table_first_retry(u32 pc)
{
	if (m_esp_other_table_first_retry_captured || fc3000_verify_table_match())
		return;
	m_esp_other_table_first_retry_captured = true;

	const u32 a3 = u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff;
	const u32 table_ptr_0e7e = (u32(m_lowmem_shadow[0x0e7e >> 1]) << 16) | m_lowmem_shadow[(0x0e7e >> 1) + 1];
	const u32 table_ptr_0e8e = (u32(m_lowmem_shadow[0x0e8e >> 1]) << 16) | m_lowmem_shadow[(0x0e8e >> 1) + 1];
	const u8 retry_number = u8(m_lowmem_shadow[0x0e8c >> 1] >> 8);

	logerror("ASR10_ESP_OTHER_TABLE_FIRST_RETRY pc=%06x "
		"a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x "
		"d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x sr=%04x "
		"lowmem_0e7e=%08x lowmem_0e82=%04x lowmem_0e8c=%04x lowmem_0e8e=%08x "
		"a3_minus_lowmem_0e7e=%d a3_minus_lowmem_0e8e=%d retry_number=%u\n",
		pc,
		u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff, a3,
		u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u16(m_maincpu->state_int(M68K_SR)),
		table_ptr_0e7e, m_lowmem_shadow[0x0e82 >> 1], m_lowmem_shadow[0x0e8c >> 1], table_ptr_0e8e,
		int32_t(a3) - int32_t(table_ptr_0e7e), int32_t(a3) - int32_t(table_ptr_0e8e), retry_number);

	// TASK2 (prior round): bounded live-memory windows at every plausible
	// base-pointer candidate. "64 bytes before A3" and "64 bytes at A3"
	// are rendered as one contiguous 128-byte window starting 64 bytes
	// before A3, so the boundary itself is visible in one dump.
	dump_memory_window("a3_minus64_to_a3_plus64", a3 >= 0x40 ? a3 - 0x40 : 0, 128);
	dump_memory_window("lowmem_0e8e_pointer_target", table_ptr_0e8e, 128);
	dump_memory_window("lowmem_0e7e_pointer_target", table_ptr_0e7e, 128);
	// "64 bytes at the start of the current record": no register or
	// lowmem field distinct from A3 has been established as a
	// per-record (as opposed to per-table) base pointer -- A3 is the
	// running per-byte cursor per the established f97432 call graph
	// ("move.b (A3)+,(A6)"). Not dumped separately from the a3 window
	// above; see the FINAL REPORT for this limitation stated explicitly.
}


// filesystem-browser-map.md 4.26 TASK 1: one-shot (first REAL mismatch
// only) capture of the f97574 compare ("cmp.b (A6),D2") when the CURRENT
// table is NOT the already-fixed fff9bca0 table. Fed the actual bus-read
// value/mem_mask from the caller (the FC3000-range read tap), not
// rereard via read_program_word -- a prior round's version fired on the
// first COMPARE regardless of outcome, consuming its one-shot even on a
// match. This version checks observed against D2 (the expected value)
// BEFORE touching the one-shot flag, and returns without logging or
// consuming it when the compare actually matches.
void asr10_boot_state::log_esp_other_table_verify(u32 pc, u32 cpu_byte_address, u16 data, u16 mem_mask)
{
	if (m_esp_other_table_verify_captured || fc3000_verify_table_match())
		return;

	const bool low_lane = (mem_mask & 0x00ff) != 0;
	const u8 observed_byte = low_lane ? u8(data & 0xff) : u8(data >> 8);
	const u8 expected_byte = u8(m_maincpu->state_int(M68K_D2) & 0xff);
	if (observed_byte == expected_byte)
		return; // not a mismatch -- do not consume the one-shot

	m_esp_other_table_verify_captured = true;

	const u32 table_ptr_0e8e = (u32(m_lowmem_shadow[0x0e8e >> 1]) << 16) | m_lowmem_shadow[(0x0e8e >> 1) + 1];
	const bool in_es5510_window = cpu_byte_address >= 0x00fc3000 && cpu_byte_address <= 0x00fc31ff;
	const u32 es5510_host_offset = in_es5510_window ? (cpu_byte_address - 0x00fc3001) >> 1 : 0xffffffff;
	const u8 retry_number = u8(m_lowmem_shadow[0x0e8c >> 1] >> 8);

	logerror("ASR10_ESP_OTHER_TABLE_VERIFY pc=%06x cpu_byte_address=%06x bus_data=%04x mem_mask=%04x "
		"observed=%02x expected=%02x byte_lane=%s in_es5510_window=%u es5510_host_offset=%08x "
		"record_type_d3=%02x record_param_d1=%08x record_index_d6=%08x table_base_0e8e=%08x retry_number=%u "
		"a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x sr=%04x "
		"note=select_commit_history_not_captured_existing_ASR10_ESP_SELECT_FORWARD_"
		"tap_is_scoped_to_record_index_0_and_58_only\n",
		pc, cpu_byte_address, data, mem_mask,
		observed_byte, expected_byte, low_lane ? "odd_low" : "even_high",
		in_es5510_window ? 1u : 0u, es5510_host_offset,
		u32(m_maincpu->state_int(M68K_D3)) & 0xff, u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D6)), table_ptr_0e8e, retry_number,
		u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		u16(m_maincpu->state_int(M68K_SR)));
}


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
void asr10_boot_state::log_hall_reverb_event(const char *event, u32 pc, u32 byte_address, u16 data, u16 mem_mask)
{
	if (m_hall_reverb_trace_count >= 300)
		return;
	m_hall_reverb_trace_count++;
	const u8 retry_number = u8(m_lowmem_shadow[0x0e8c >> 1] >> 8);
	const u8 give_up_flag = u8(m_lowmem_shadow[0x0e8a >> 1] & 0xff);
	logerror("ASR10_HALL_REVERB_TRACE seq=%u event=%s pc=%06x byte_address=%06x data=%04x mem_mask=%04x "
		"retry_number=%u give_up_flag_0e8a=%02x "
		"d1=%08x d2=%08x d3=%08x d6=%08x a3=%08x a4=%08x a6=%08x sr=%04x\n",
		m_hall_reverb_trace_count, event, pc, byte_address, data, mem_mask,
		retry_number, give_up_flag,
		u32(m_maincpu->state_int(M68K_D1)), u32(m_maincpu->state_int(M68K_D2)),
		u32(m_maincpu->state_int(M68K_D3)), u32(m_maincpu->state_int(M68K_D6)),
		u32(m_maincpu->state_int(M68K_A3)) & 0x00ffffff, u32(m_maincpu->state_int(M68K_A4)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff, u16(m_maincpu->state_int(M68K_SR)));
}


void asr10_boot_state::log_fc3000_verify_handshake(bool write, u32 pc, u32 selected_cpu_byte_address, u32 offset, u16 data, u16 mem_mask)
{
	// Only the read at f97574 ("cmp.b (A6),D2", the write-then-verify
	// compare identified in the prior session's ERROR 032 trace) is of
	// interest here -- everything else just feeds the ring buffer above.
	if (write || pc != 0x00f97574 || m_fc3000_verify_captured)
		return;
	if (!fc3000_verify_table_match())
		return;

	const u16 d2 = u16(m_maincpu->state_int(M68K_D2));
	const u8 expected = u8(d2);
	const u8 observed = u8(data);
	if (expected == observed)
		return; // this particular compare matched; keep waiting for a real mismatch

	m_fc3000_verify_captured = true;

	const u32 a0 = u32(m_maincpu->state_int(M68K_A0));
	const u32 a3 = u32(m_maincpu->state_int(M68K_A3));
	const u32 a4 = u32(m_maincpu->state_int(M68K_A4));
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5));
	const u32 a6 = u32(m_maincpu->state_int(M68K_A6));
	const u16 table_base = m_lowmem_shadow[0x0e8e >> 1];
	const u16 table_base_lo = m_lowmem_shadow[(0x0e8e >> 1) + 1];
	const u8 outer_retry_remaining = u8(m_lowmem_shadow[0x0e9c >> 1]);
	const u8 internal_retry_count = u8(m_lowmem_shadow[0x0e8c >> 1] >> 8);
	const u8 record_type = u8(m_maincpu->state_int(M68K_D3));
	const u8 record_count = u8(m_maincpu->state_int(M68K_D5));
	const u8 record_param = u8(m_maincpu->state_int(M68K_D1));
	const u8 record_index = u8(m_maincpu->state_int(M68K_D6));
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));

	std::string ring;
	const u32 count = std::min<u32>(m_fc3000_verify_ring_pos, u32(m_fc3000_verify_ring.size()));
	for (u32 i = 0; i < count; i++)
	{
		const auto &e = m_fc3000_verify_ring[(m_fc3000_verify_ring_pos - count + i) % m_fc3000_verify_ring.size()];
		if (i)
			ring += ',';
		ring += util::string_format("[pc=%06x addr=%06x rw=%c data=%04x mask=%04x]",
			e.pc, e.address, e.write ? 'W' : 'R', e.data, e.mem_mask);
	}

	logerror("ASR10_FC3000_VERIFY_HANDSHAKE pc=%06x address=%06x offset=%04x mem_mask=%04x "
		"observed=%02x expected_d2=%02x sr=%04x "
		"outer_retry_remaining=%u internal_retry_count=%u "
		"table_base_0e8e=%04x%04x record_type_d3=%02x record_count_d5=%02x "
		"record_param_d1=%02x record_index_d6=%02x "
		"a0=%06x a3=%06x a4=%06x a5=%06x a6=%06x "
		"last_accesses=\"%s\"\n",
		pc, selected_cpu_byte_address, offset, mem_mask, observed, expected, sr,
		outer_retry_remaining, internal_retry_count,
		table_base, table_base_lo, record_type, record_count,
		record_param, record_index,
		a0, a3, a4, a5, a6, ring.c_str());
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
	const bool panel_error_text = !strcmp(source, "panel_error009_text") || !strcmp(source, "panel_error139_text") ||
		!strcmp(source, "panel_effect_download_failed_text") || !strcmp(source, "panel_error032_text");
	const u8 error_number = u8(value);
	const bool is_032_write = !panel_error_text && error_number == 0x20;
	if (m_error009_origin_logged && !panel_error_text && !is_032_write)
		return;
	if (is_032_write && m_error032_origin_logged)
		return;

	if (error_number != 0x09 && error_number != 0x8b && !panel_error_text && !is_032_write)
		return;

	if (!panel_error_text && !is_032_write)
		m_error009_origin_logged = true;
	if (is_032_write)
	{
		m_error032_origin_logged = true;
		const u32 caller_pc = m_last_distinct_pc & 0x00ffffff;
		// caller_pc lands in the plain .ram() window (0xfc6900-0xffffff,
		// mem_map()) rather than ROM or the 0x000000-0x0fffff lowmem
		// shadow, so read it via the generic bus (read_program_word)
		// instead of dump_loaded_code_range/read_loaded_word (which would
		// silently return 0xffff for this range).
		{
			const u32 start = caller_pc >= 0x40 ? caller_pc - 0x40 : 0;
			const u32 end = caller_pc + 0x20;
			std::string words;
			for (u32 cursor = start; cursor <= end; cursor += 2)
			{
				if (cursor != start)
					words += ',';
				words += util::string_format("%06x:%04x", cursor, read_program_word(cursor));
			}
			logerror("ASR10_CODE_DUMP tag=task1_error032_caller range=%06x_%06x words=\"%s\"\n",
				start, end, words.c_str());
		}
		dump_loaded_code_range("task1_common_error_routine_tail", 0x00f882a0, 0x00f88320);
		dump_loaded_code_range("task2_f973f0_download_routine", 0x00f973b0, 0x00f97460);
		dump_loaded_code_range("task3_f97340_f97800", 0x00f97340, 0x00f97800);
		dump_loaded_code_range("task3_f97800_f97b00", 0x00f97800, 0x00f97b00);
		{
			std::string vecwords;
			for (u32 cursor = 0; cursor <= 0x40; cursor += 2)
			{
				if (cursor)
					vecwords += ',';
				vecwords += util::string_format("%06x:%04x", cursor, lowmem_word(cursor));
			}
			logerror("ASR10_TASK4_RUNTIME_VECTORS words=\"%s\"\n", vecwords.c_str());
		}
	}

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

	logerror("ASR10_ERROR_CONTEXT source=%s pc=%06x previous_pc=%06x opcode=%04x value=%04x mem_mask=%04x "
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
void asr10_boot_state::log_lowmem_service_context(bool write, u32 byte_address, u16 previous, u16 current, u16 data, u16 mem_mask)
{
	if (machine().side_effects_disabled())
		return;
	if (byte_address != 0x0d06 && byte_address != 0x0e82)
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const bool near_service_setter = pc >= 0x0000bee0 && pc <= 0x0000bf60;
	if (!m_synth_68302_timer_iack_fire_count && !near_service_setter && !m_fc6816_service_setter_dump_logged)
		return;

	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	logerror("ASR10_LOWMEM_SERVICE_CONTEXT pc=%06x previous_pc=%06x opcode=%04x rw=%c addr=%04x "
		"data=%04x mem_mask=%04x previous=%04x current=%04x changed=%04x "
		"field=%s fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x fc6884=%04x fc6894=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"sr=%04x sr_mask=%u recent_queue_pc=%06x recent_queue_slot=%u recent_rte_return_pc=%06x "
		"dispatcher_count=%u panel=\"%s\"\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), write ? 'W' : 'R', byte_address,
		data, mem_mask, previous, current, previous ^ current,
		byte_address == 0x0d06 ? "lowmem_0d06_service_flag_candidate" : "lowmem_0e82_service_argument_candidate",
		m_m68302_internal_shadow[0x12 >> 1], m_m68302_internal_shadow[0x14 >> 1],
		m_m68302_internal_shadow[0x16 >> 1], m_m68302_internal_shadow[0x18 >> 1],
		m_m68302_internal_shadow[0x84 >> 1], m_m68302_internal_shadow[0x94 >> 1],
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		sr, (sr >> 8) & 7, m_recent_queue_pc, m_recent_queue_slot,
		m_queue_rte_last_return_pc, m_runtime_dispatch_entry_count, m_panel_text);
}


void asr10_boot_state::dump_loaded_code_range(const char *tag, u32 start, u32 end)
{
	std::string words;
	for (u32 cursor = start; cursor <= end; cursor += 2)
	{
		if (cursor != start)
			words += ',';
		words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
	}
	logerror("ASR10_CODE_DUMP tag=%s range=%06x_%06x words=\"%s\"\n", tag, start, end, words.c_str());
}


namespace {

// Word-aligned scalar fields. Several ROM-referenced addresses are odd
// (byte-within-word); each entry's `lane` says which byte of the word at
// `word_address` it actually is: WHOLE (untouched by anything else sharing
// the word), HIGH (even byte), or LOW (odd byte). $04b2/$04b3 share a word
// but are tracked as separate entries (HIGH/LOW) because they are logically
// distinct -- $04b2 is the validity gate flag f894a4 itself reads, $04b3 is
// a high-frequency mode field written by fb8938/40/48; combining them into
// one "last writer" (as an earlier version of this diagnostic did) hid
// $04b2's true write history behind $04b3's much higher write volume.
enum class fsb_lane : u8 { WHOLE, HIGH, LOW };
struct fsb_field_info
{
	const char *name;
	u32 word_address;
	fsb_lane lane;
	const char *note;
};

constexpr u32 FSB_FIELD_COUNT = 10;
constexpr u32 FSB_ENTRY_COUNT = 7;
constexpr u32 FSB_DUMP_COUNT = 19;

constexpr fsb_field_info FSB_FIELDS[FSB_FIELD_COUNT] = {
	{ "descriptor_04fe",       0x04fe, fsb_lane::WHOLE, "fixed lowmem descriptor base (fb895a A2)" },
	{ "descriptor_limit_050c", 0x050c, fsb_lane::WHOLE, "descriptor+0xe size/limit field, read as .l (fb8966)" },
	{ "buffer_ptr_040e",       0x040e, fsb_lane::WHOLE, "current FDC destination buffer pointer, read/written as .l" },
	{ "buffer_ptr_0406",       0x0406, fsb_lane::WHOLE, "secondary saved buffer-pointer/byte-count slot, .l" },
	{ "device_class_049a_049b",0x049a, fsb_lane::WHOLE, "049b: raw device/media byte, then class remainder after fb83e4" },
	{ "position_cursor_040a",  0x040a, fsb_lane::WHOLE, "040a: device CLASS byte after fb83e4; also a .l position cursor in fb894e" },
	{ "validity_flag_04b2",    0x04b2, fsb_lane::HIGH,  "f894a4's own gate flag, read at f89494; candidate cache/browser-init validity predicate" },
	{ "mode_field_04b3",       0x04b2, fsb_lane::LOW,   "mode field written by fb8938/40/48 -- shares a word with validity_flag_04b2 but is a separate field" },
	{ "track_cache_04ac",      0x04ac, fsb_lane::HIGH,  "fb8c6e cached seek target" },
	{ "flags_04be_04bf",       0x04be, fsb_lane::WHOLE, "04be hi: f894a4-table type==0x20 flag; 04bf lo: fb7ba6 any-valid-entry flag" },
};

bool fsb_lane_matches(fsb_lane lane, u16 mem_mask)
{
	switch (lane)
	{
	case fsb_lane::HIGH: return (mem_mask & 0xff00) != 0;
	case fsb_lane::LOW:  return (mem_mask & 0x00ff) != 0;
	default:             return true;
	}
}

const char *FSB_ENTRY_NAMES[] = {
	"fb82a4_boot_sector_load",
	"fb846a_generic_loader",
	"fb895a_range_reader",
	"fb8c6e_seek_wrapper",
	"f894a4_routine_entry",  // true entry proxy (f89494, reads $4b2.w)
	"f894a4_table_reached",  // table-consumption path only (f894b4) -- NOT proof of entry
	"fdc_command_issue",
};
static_assert(std::size(FSB_ENTRY_NAMES) == FSB_ENTRY_COUNT);

// One-shot code-range dumps this task's live verification added (the
// pre-existing ASR10_EXPERIMENT_TUNING_STALL_TRACE dumps in
// log_tuning_stall_candidate_dump() are untouched -- these are additional).
// `trigger_pc` is the PC that arms the dump; `highview` selects
// dump_highview_code_range() over dump_loaded_code_range(). Slot 0's six
// jump-vector targets and f894a4's five internal vector calls are all
// `jsr $xxxx.w` (absolute-short addressing): on this real 68000 core, any
// such operand with bit 15 set sign-extends to a 0xFFxxxx high-view
// address, not the low 0x00xxxx address an earlier version of this
// diagnostic incorrectly dumped -- see filesystem-browser-map.md 6.4/6.6.
struct fsb_dump_target
{
	const char *tag;
	u32 trigger_pc;
	u32 start;
	u32 end;
	bool highview;
};

constexpr fsb_dump_target FSB_DUMP_TARGETS[FSB_DUMP_COUNT] = {
	{ "fsb_highview_slot1_ffc85a", 0x00ffc85a, 0x00ffc830, 0x00ffc8d0, true },
	{ "fsb_highview_slot3_ff9106", 0x00ff9106, 0x00ff90d0, 0x00ff9170, true },
	// slot 0 continuation (00ae18) jsr targets, sign-extended: 87f2->ff87f2 etc.
	{ "fsb_slot0_vector_ff87f2", 0x0000ae18, 0x00ff87d0, 0x00ff8830, true },
	{ "fsb_slot0_vector_ffbc8e", 0x0000ae18, 0x00ffbc60, 0x00ffbcc0, true },
	{ "fsb_slot0_vector_ffa26e", 0x0000ae18, 0x00ffa240, 0x00ffa2a0, true },
	{ "fsb_slot0_vector_ff8864", 0x0000ae18, 0x00ff8840, 0x00ff88a0, true },
	{ "fsb_slot0_vector_ff8bb6", 0x0000ae18, 0x00ff8b90, 0x00ff8bf0, true },
	{ "fsb_slot0_vector_ff9650", 0x0000ae18, 0x00ff9620, 0x00ff9680, true },
	// slot 4 continuation (0068ae) jsr targets: these are `jsr $fffxxxxx.l`
	// (absolute LONG), not abs.w -- no sign-extension ambiguity, and the
	// 24-bit-masked target lands in the ROM-backed 0xf80000-0xfbffff window
	// (verified against high_alias_r's body: a direct, unmodified ROM
	// fetch), so dump_loaded_code_range() already resolves these correctly.
	{ "fsb_slot4_vector_f8db4c", 0x000068ae, 0x00f8db20, 0x00f8db80, false },
	{ "fsb_slot4_vector_f8d920", 0x000068ae, 0x00f8d900, 0x00f8d960, false },
	{ "fsb_slot4_vector_f8d992", 0x000068ae, 0x00f8d970, 0x00f8d9d0, false },
	{ "fsb_slot4_vector_f8d9de", 0x000068ae, 0x00f8d9c0, 0x00f8da20, false },
	{ "fsb_slot4_vector_f8da58", 0x000068ae, 0x00f8da30, 0x00f8da90, false },
	// f894a4's own internal vector calls (path A/B/C, see 6.3): $88ac/$88bc/
	// $88c0/$88c4 sign-extend to ff88ac/ff88bc/ff88c0/ff88c4 (all close
	// together, one combined window); $8984 sign-extends to ff8984.
	{ "fsb_f894a4_vectors_ff88ac_c4", 0x00f89494, 0x00ff8880, 0x00ff8910, true },
	{ "fsb_f894a4_vector_ff8984", 0x00f89494, 0x00ff8960, 0x00ff89c0, true },
	// Item 4 follow-through: the corrected tail-jump targets reached from
	// slot 0's vectors. $2b14 (from ff87f2) and $711e (from ffa26e) are
	// low, plain jmp .w operands with bit 15 clear -- no sign-extension,
	// genuinely lowmem. ffbc8e's own internal jsr/jmp targets ($b8e2,
	// $8cc2) both have bit 15 set -- sign-extend to ffb8e2/ff8cc2.
	{ "fsb_target_002b14", 0x0000ae18, 0x00002af0, 0x00002b50, false },
	{ "fsb_target_00711e", 0x0000ae18, 0x000070fe, 0x0000715e, false },
	{ "fsb_ffbc8e_target_ff8cc2", 0x0000ae18, 0x00ff8ca0, 0x00ff8d00, true },
	{ "fsb_ffbc8e_target_ffb8e2", 0x0000ae18, 0x00ffb8c0, 0x00ffb920, true },
};

} // namespace


void asr10_boot_state::log_fsb_field_write(u32 pc, u32 byte_address, u16 previous, u16 current, u16 mem_mask)
{
	if (!m_fsb.enabled || machine().side_effects_disabled())
		return;

	if (byte_address >= 0x0544 && byte_address < 0x0544 + 40 * 0x1a)
	{
		m_fsb.table_write_count++;
		const u32 rel = byte_address - 0x0544;
		m_fsb.table_last_write_pc = pc;
		m_fsb.table_last_write_entry = rel / 0x1a;
		if (m_fsb.table_write_count <= 64 || !(m_fsb.table_write_count & (m_fsb.table_write_count - 1)))
			logerror("ASR10_FSB_FIELD event=write field=table_0544 pc=%06x address=%06x entry_index=%u "
				"byte_offset=%u previous=%04x current=%04x mem_mask=%04x count=%u\n",
				pc, byte_address, rel / 0x1a, rel % 0x1a, previous, current, mem_mask,
				m_fsb.table_write_count);
	}

	for (u32 i = 0; i < fsb_state::FIELD_COUNT; i++)
	{
		if (byte_address != FSB_FIELDS[i].word_address || !fsb_lane_matches(FSB_FIELDS[i].lane, mem_mask))
			continue;
		fsb_field_runtime &st = m_fsb.fields[i];
		st.write_count++;
		st.last_write_pc = pc;
		st.value = current;
		st.seen = true;
		if (st.write_count <= 64 || !(st.write_count & (st.write_count - 1)))
			logerror("ASR10_FSB_FIELD event=write field=%s pc=%06x address=%06x previous=%04x current=%04x "
				"mem_mask=%04x count=%u note=\"%s\"\n",
				FSB_FIELDS[i].name, pc, byte_address, previous, current, mem_mask,
				st.write_count, FSB_FIELDS[i].note);
	}
}


void asr10_boot_state::log_fsb_field_read(u32 pc, u32 byte_address, u16 data, u16 mem_mask)
{
	if (!m_fsb.enabled || machine().side_effects_disabled())
		return;

	if (byte_address >= 0x0544 && byte_address < 0x0544 + 40 * 0x1a)
	{
		m_fsb.table_read_count++;
		const u32 rel = byte_address - 0x0544;
		m_fsb.table_last_read_pc = pc;
		m_fsb.table_last_read_entry = rel / 0x1a;
		if (m_fsb.table_read_count <= 64 || !(m_fsb.table_read_count & (m_fsb.table_read_count - 1)))
			logerror("ASR10_FSB_FIELD event=read field=table_0544 pc=%06x address=%06x entry_index=%u "
				"byte_offset=%u data=%04x mem_mask=%04x count=%u\n",
				pc, byte_address, rel / 0x1a, rel % 0x1a, data, mem_mask, m_fsb.table_read_count);
	}

	for (u32 i = 0; i < fsb_state::FIELD_COUNT; i++)
	{
		if (byte_address != FSB_FIELDS[i].word_address || !fsb_lane_matches(FSB_FIELDS[i].lane, mem_mask))
			continue;
		fsb_field_runtime &st = m_fsb.fields[i];
		st.read_count++;
		st.last_read_pc = pc;
		if (st.read_count <= 64 || !(st.read_count & (st.read_count - 1)))
			logerror("ASR10_FSB_FIELD event=read field=%s pc=%06x address=%06x data=%04x mem_mask=%04x "
				"count=%u note=\"%s\"\n",
				FSB_FIELDS[i].name, pc, byte_address, data, mem_mask, st.read_count,
				FSB_FIELDS[i].note);
	}
}


void asr10_boot_state::log_fsb_entry(u32 entry_id, u32 pc)
{
	if (!m_fsb.enabled || machine().side_effects_disabled())
		return;
	u32 &counter = m_fsb.entry_counts[entry_id];
	counter++;
	if (counter > 64 && (counter & (counter - 1)))
		return;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 caller = read_stack_long(sp) & 0x00ffffff;
	logerror("ASR10_FSB_ENTRY name=%s pc=%06x caller=%06x count=%u "
		"d0=%08x d1=%08x d2=%08x d3=%08x d6=%08x d7=%08x a0=%08x a1=%08x a2=%08x panel=\"%s\"\n",
		FSB_ENTRY_NAMES[entry_id], pc, caller, counter,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), m_panel_text);
}


void asr10_boot_state::log_fsb_code_dumps(u32 pc)
{
	if (!m_fsb.enabled)
		return;
	for (u32 i = 0; i < fsb_state::DUMP_COUNT; i++)
	{
		if (pc != FSB_DUMP_TARGETS[i].trigger_pc || m_fsb.dump_logged[i])
			continue;
		m_fsb.dump_logged[i] = true;
		if (FSB_DUMP_TARGETS[i].highview)
			dump_highview_code_range(FSB_DUMP_TARGETS[i].tag, FSB_DUMP_TARGETS[i].start, FSB_DUMP_TARGETS[i].end);
		else
			dump_loaded_code_range(FSB_DUMP_TARGETS[i].tag, FSB_DUMP_TARGETS[i].start, FSB_DUMP_TARGETS[i].end);
	}
}


namespace {
// The 18 ROM-wide `jsr (An)` sites found in section 4.1's enumeration
// (JMP(An) sites push no return address and cannot be tapped this way --
// see the m_pti comment at the member declaration), plus the one
// live-discovered lowmem site (002b38, reached from slot 0's ff87f2
// vector via the 002b14 tail-jump, per filesystem-browser-map.md 4.5).
struct pti_indirect_site
{
	const char *name;
	u32 pc;
	int reg; // M68K_A0..M68K_A5
};

constexpr u32 PTI_SITE_COUNT = 19;

constexpr pti_indirect_site PTI_INDIRECT_SITES[PTI_SITE_COUNT] = {
	{ "f87ef6_jsr_a0", 0x00f87ef6, M68K_A0 },
	{ "f87efc_jsr_a0", 0x00f87efc, M68K_A0 },
	{ "f88024_jsr_a1", 0x00f88024, M68K_A1 },
	{ "f881e2_jsr_a0", 0x00f881e2, M68K_A0 },
	{ "f88228_jsr_a0", 0x00f88228, M68K_A0 },
	{ "f88352_jsr_a1", 0x00f88352, M68K_A1 },
	{ "f8d0b8_jsr_a3", 0x00f8d0b8, M68K_A3 },
	{ "f8f0ea_jsr_a3", 0x00f8f0ea, M68K_A3 },
	{ "f8f0ee_jsr_a3", 0x00f8f0ee, M68K_A3 },
	{ "f8f100_jsr_a3", 0x00f8f100, M68K_A3 },
	{ "f8f126_jsr_a2", 0x00f8f126, M68K_A2 },
	{ "f8f12a_jsr_a2", 0x00f8f12a, M68K_A2 },
	{ "f8f13c_jsr_a2", 0x00f8f13c, M68K_A2 },
	{ "f8fcfc_jsr_a5", 0x00f8fcfc, M68K_A5 },
	{ "f8ff56_jsr_a2", 0x00f8ff56, M68K_A2 },
	{ "fa07fc_jsr_a1", 0x00fa07fc, M68K_A1 },
	{ "faae16_jsr_a1", 0x00faae16, M68K_A1 },
	{ "fb0f90_jsr_a0", 0x00fb0f90, M68K_A0 },
	{ "002b38_jsr_a2", 0x00002b38, M68K_A2 }, // lowmem: slot0 ff87f2 -> 002b14 -> here
};

const char *pti_classify_target(u32 target)
{
	if (target >= 0x00f8948e && target <= 0x00f894f4)
		return "f894a4_routine_range";
	if (target >= 0x00f89a72 && target <= 0x00f89a94)
		return "panel_enqueue_range";
	if ((target >= 0x00fb7c00 && target <= 0x00fb9700) || (target >= 0x00fba000 && target <= 0x00fbb600))
		return "mapped_filesystem_range";
	return "other";
}
} // namespace


void asr10_boot_state::check_pti_sites(u32 pc)
{
	for (u32 i = 0; i < PTI_SITE_COUNT; i++)
	{
		if (pc == PTI_INDIRECT_SITES[i].pc)
			log_pti_indirect_call(pc, i);
	}
	if (pc == 0x00ffc8c0)
		log_pti_trap9(pc, "slot1_ffc85a_resume");
	else if (pc == 0x0000713a)
		log_pti_trap9(pc, "slot0_a26e_00711e_target");
}


// Proves (or disproves) that the active stack is where the JSR/TRAP#9/RTS
// hooks assume it is, before trusting their (null or non-null) results.
// M68K_SP is the currently-active stack pointer (USP if the CPU is in user
// mode, ISP/MSP if supervisor) per m68kcommon.h; M68K_USP/M68K_ISP are the
// two register values directly, regardless of which is currently active,
// so a user/supervisor mismatch is visible even if M68K_SP alone would
// hide it.
void asr10_boot_state::log_pti_stack_probe(const char *milestone, u32 pc)
{
	if (!m_pti.enabled)
		return;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 usp = m_maincpu->state_int(M68K_USP) & 0x00ffffff;
	const u32 isp = m_maincpu->state_int(M68K_ISP) & 0x00ffffff;
	logerror("ASR10_PTI_STACK_PROBE milestone=%s pc=%06x sp=%06x usp=%06x isp=%06x "
		"sr=%04x supervisor=%u active_slot=%u sp_region=%s usp_region=%s isp_region=%s panel=\"%s\"\n",
		milestone, pc, sp, usp, isp, sr, BIT(sr, 13) ? 1 : 0, m_dispatcher_rte_frame_slot,
		address_region_guess(sp), address_region_guess(usp), address_region_guess(isp), m_panel_text);
}


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
void asr10_boot_state::log_pti_vector_call(u32 vector_index, const char *vector_name, u32 pc,
	u32 target, u32 byte_address, u16 data, const char *gate_note)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	u32 &counter = m_pti.vector_hit_counts[vector_index];
	counter++;
	const u32 usp = m_maincpu->state_int(M68K_USP) & 0x00ffffff;
	const u32 expected_return = pc + 4;
	const s32 usp_minus_observed = s32(usp) - s32(byte_address);
	const u16 expected_word = (byte_address & 2) == (usp & 2)
		? u16(expected_return) : u16(expected_return >> 16);
	logerror("ASR10_PTI_VECTOR_CALL name=%s pc=%06x target=%06x word_count=%u "
		"observed_write_address=%06x observed_write_value=%04x expected_return=%06x "
		"usp=%06x usp_minus_observed=%d match_expected_word=%u "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"active_slot=%u gate=%s panel=\"%s\"\n",
		vector_name, pc, target, counter,
		byte_address, data, expected_return,
		usp, usp_minus_observed, data == expected_word ? 1 : 0,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		m_dispatcher_rte_frame_slot, gate_note, m_panel_text);
}


void asr10_boot_state::log_pti_vector_return(u32 vector_index, const char *vector_name, u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	if (m_pti.vector_hit_counts[vector_index] == 0)
		return; // return-point reached without a matching call this run -- not this vector's return
	m_pti.vector_return_counts[vector_index]++;
	logerror("ASR10_PTI_VECTOR_RETURN name=%s pc=%06x call_count=%u return_count=%u active_slot=%u panel=\"%s\"\n",
		vector_name, pc, m_pti.vector_hit_counts[vector_index], m_pti.vector_return_counts[vector_index],
		m_dispatcher_rte_frame_slot, m_panel_text);
}


void asr10_boot_state::log_pti_gate(const char *name, u32 pc, u32 value, u32 gate_index)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.gate_hit_counts[gate_index]++;
	logerror("ASR10_PTI_GATE name=%s pc=%06x value=%04x count=%u active_slot=%u panel=\"%s\"\n",
		name, pc, value, m_pti.gate_hit_counts[gate_index], m_dispatcher_rte_frame_slot, m_panel_text);
}


// 002b1a (`cmpa.w $35e.w,A5`) and 002b26 (`move.w $31c.w,D5`) -- the two
// genuine reads inside 002b14's node-classify sequence (see
// filesystem-browser-map.md 4.10 for the authoritative disassembly and
// the corrected register usage: A2/A5, not "D2.w" as informally assumed
// before disassembling). Logs D0-D3/A2/A5 (full 32-bit and the low word
// separately), the value actually read, USP, and active slot/node
// context, so the branch this read feeds can be reconstructed rather
// than assumed.
void asr10_boot_state::log_pti_node_gate(const char *name, u32 pc, u32 byte_address, u32 value, u32 index)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.node_gate_hit_counts[index]++;
	const u32 d2 = u32(m_maincpu->state_int(M68K_D2));
	const u32 d5 = u32(m_maincpu->state_int(M68K_D5));
	const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u32 usp = m_maincpu->state_int(M68K_USP) & 0x00ffffff;
	logerror("ASR10_PTI_NODE_GATE name=%s pc=%06x address=%06x value=%04x count=%u "
		"d2=%08x d2w=%04x d5=%08x d5w=%04x a2=%06x a5=%06x usp=%06x "
		"active_slot=%u active_slot_a2=%06x panel=\"%s\"\n",
		name, pc, byte_address, value, m_pti.node_gate_hit_counts[index],
		d2, u16(d2), d5, u16(d5), a2, a5, usp,
		m_dispatcher_rte_frame_slot, m_dispatcher_rte_frame_a2, m_panel_text);
}


// trap #4 at 002b2a. TRAP always pushes a genuine exception frame
// (SR+PC) regardless of addressing context, so it's tappable the same
// way as trap #9 (see 4.8) even though this specific call site is
// register-only right up to the trap. Vector 36 (32+4); handler address
// at lowmem $90 (36*4).
void asr10_boot_state::log_pti_trap4(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.trap4_hit_count++;
	const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	logerror("ASR10_PTI_TRAP4 pc=%06x count=%u a2_before=%06x a5=%06x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.trap4_hit_count, a2, a5, m_dispatcher_rte_frame_slot, m_panel_text);
	if (!m_pti.trap4_vector_logged)
	{
		m_pti.trap4_vector_logged = true;
		const u32 handler = read_loaded_long(0x00000090) & 0x00ffffff;
		logerror("ASR10_PTI_TRAP4_VECTOR vector=36 vector_addr=000090 handler=%06x\n", handler);
		if (handler >= 0x00fc6900)
			dump_highview_code_range("pti_trap4_handler", handler, handler + 0x40);
		else
			dump_loaded_code_range("pti_trap4_handler", handler, handler + 0x40);
	}
}


// Vectors 34 (trap #2), 44 (trap #12), 46 (trap #14) -- read and dumped
// once, unconditionally at the f880fc gate, independent of whether
// 007000 (which issues all three) ever actually executes. A static
// ROM-wide search for the code that populates these vector-table slots
// found only false-positive byte matches (same pattern as the 6b1c/6cf2/
// d10a search in 4.10) -- the setup is evidently not simple per-vector
// immediate stores, so the handler addresses are obtained by reading the
// already-populated table live instead.
void asr10_boot_state::log_pti_vectors_34_44_46()
{
	if (!m_pti.enabled || m_pti.vectors_34_44_46_logged)
		return;
	m_pti.vectors_34_44_46_logged = true;
	const u32 h2 = read_loaded_long(0x00000088) & 0x00ffffff;
	const u32 h12 = read_loaded_long(0x000000b0) & 0x00ffffff;
	const u32 h13 = read_loaded_long(0x000000b4) & 0x00ffffff;
	const u32 h14 = read_loaded_long(0x000000b8) & 0x00ffffff;
	const u32 h15 = read_loaded_long(0x000000bc) & 0x00ffffff;
	logerror("ASR10_PTI_VECTORS_2_12_13_14_15 vector34_addr=000088 handler=%06x "
		"vector44_addr=0000b0 handler=%06x vector45_addr=0000b4 handler=%06x "
		"vector46_addr=0000b8 handler=%06x vector47_addr=0000bc handler=%06x\n",
		h2, h12, h13, h14, h15);
	auto dump = [this](const char *tag, u32 handler, u32 len)
	{
		if (handler >= 0x00fc6900)
			dump_highview_code_range(tag, handler, handler + len);
		else if (handler <= 0x00fbffff)
			dump_loaded_code_range(tag, handler, handler + len);
	};
	dump("pti_trap2_handler", h2, 0x40);
	dump("pti_trap12_handler", h12, 0x40);
	dump("pti_trap13_handler", h13, 0x40);
	dump("pti_trap14_handler", h14, 0x40);
	dump("pti_trap15_handler", h15, 0x40);
	// f881f6 control-flow correction: f881e6's handler RTEs at f881f4, so
	// f881f6 cannot be its fall-through body. Dump a wider window spanning
	// both f881e6-f881f4 and the separate f881f6+ routine, plus whichever
	// vector's handler equals f881f6 exactly, if any.
	dump("pti_f881e6_wide", 0x00f881e6, 0x60);
	if (h13 == 0x00f881f6)
		logerror("ASR10_PTI_F881F6_XREF vector45(trap13) handler equals f881f6\n");
	if (h14 == 0x00f881f6)
		logerror("ASR10_PTI_F881F6_XREF vector46(trap14) handler equals f881f6\n");
	if (h15 == 0x00f881f6)
		logerror("ASR10_PTI_F881F6_XREF vector47(trap15) handler equals f881f6\n");
	if (h13 != 0x00f881f6 && h14 != 0x00f881f6 && h15 != 0x00f881f6)
		logerror("ASR10_PTI_F881F6_XREF none of vectors 45/46/47 point to f881f6\n");
}


// filesystem-browser-map.md 4.19: mandatory run-config header, logged
// once per machine_reset(), unconditionally (not gated on any experiment
// flag) so every capture is self-describing. Enumerates every known
// ASR10_* environment flag (requested env string, parsed/effective
// value, default when unset), ROM/floppy identity, and what is
// statically known about the DUART/IRQ6 wiring at this point (the
// counter's actual period/IACK vector are runtime facts that only exist
// once the firmware arms the counter -- see the ASR10_DUART_COUNTER
// arm-time log lines for those, not duplicated here).
void asr10_boot_state::log_run_config_header()
{
	auto flag = [](const char *name) -> std::string
	{
		const char *const v = std::getenv(name);
		return v ? std::string(v) : std::string("<unset>");
	};

	logerror("ASR10_RUN_CONFIG_HEADER begin ==========================================\n");
	logerror("ASR10_RUN_CONFIG_BUILD mame_version=%s source=src/mame/ensoniq/asr10_boot.cpp "
		"romset=asr10booth\n",
		emulator_info::get_build_version());
	logerror("ASR10_RUN_CONFIG_ROM lo=asr-648c-lo-1.5b.bin CRC(8e437843) "
		"SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0) "
		"hi=asr-65e0-hi-1.5b.bin CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904)\n");
	{
		floppy_image_device *const floppy = m_floppy_connector->get_device();
		if (floppy && floppy->exists())
		{
			const std::string hash = floppy->hash().macro_string();
			logerror("ASR10_RUN_CONFIG_FLOPPY mounted=1 basename=%s hash=%s\n",
				floppy->basename() ? floppy->basename() : "<null>",
				hash.empty() ? "<unavailable_this_mame_version_does_not_precompute_floppy_hash>" : hash.c_str());
		}
		else
		{
			logerror("ASR10_RUN_CONFIG_FLOPPY mounted=0\n");
		}
	}
	logerror("ASR10_RUN_CONFIG_IRQ6 wiring=fixed vector=0x56 condition=duart_irq_model_enabled()&&"
		"level==6&&m_panel_c_irq6_asserted duart_irq_model_enabled=%u note=actual_counter_period_and_"
		"first_iack_vector_are_runtime_facts_see_ASR10_DUART_COUNTER_and_ASR10_M68K_IACK_tags\n",
		duart_irq_model_enabled() ? 1u : 0u);

	// requested (raw env string) / effective (parsed bool actually used) /
	// default-when-unset, for every ASR10_* flag found in this source file.
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_DIAG_PANEL_AUTORESPOND requested=%s effective=%u default_when_unset=0\n",
		flag("ASR10_DIAG_PANEL_AUTORESPOND").c_str(), m_panel_autorespond_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_DIAG_PANEL_C_PARSER_TRACE requested=%s effective=%u "
		"default_when_unset=0 note=also_forced_on_by_any_panel_reply_71_family_flag\n",
		flag("ASR10_DIAG_PANEL_C_PARSER_TRACE").c_str(), m_panel_c_parser_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_DIAG_PAR_VALUE requested=%s effective_value=0x%04x "
		"default_when_unset=0x0200 note=value_only_applied_if_ASR10_EXPERIMENT_PAR_DIAGNOSTIC_also_set\n",
		flag("ASR10_DIAG_PAR_VALUE").c_str(), m_es5506_diag_par_value);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_DISK_SIGNATURE_TRACE").c_str(), m_disk_sig_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_DOWNLOAD_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_DOWNLOAD_TRACE").c_str(), m_download_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_DUART_COUNTER_TIMER requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_DUART_COUNTER_TIMER").c_str(), m_duart_counter_timer_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_ES5506_HOST requested=%s effective=%u "
		"default_when_unset=0 note=config_time_only_device_instantiation\n",
		flag("ASR10_EXPERIMENT_ES5506_HOST").c_str(), m_es5506_host_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_ES5510_HOST requested=%s effective=%u "
		"device_exists=%u provisional_clock_hz=10000000 default_when_unset=0 "
		"note=config_time_only_device_instantiation_set_disable_no_execute_run_no_irq\n",
		flag("ASR10_EXPERIMENT_ES5510_HOST").c_str(), m_es5510_host_enabled ? 1u : 0u,
		m_es5510_host.found() ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_FC3000_VERIFY_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_FC3000_VERIFY_TRACE").c_str(), m_fc3000_verify_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_FDC_SYNTH_TC requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_FDC_SYNTH_TC").c_str(), m_fdc_synth_tc_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_FILESYSTEM_BROWSER_TRACE").c_str(), m_fsb.enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_MC68302_GPIO_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_MC68302_GPIO_TRACE").c_str(), m_gpio_stage1_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING requested=%s effective=%u "
		"default_when_unset=0 note=mutually_exclusive_with_autorespond_and_other_panel_reply_flags_priority_order\n",
		flag("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING").c_str(), m_panel_e_ff_drain_known_ring_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS requested=%s effective=%u "
		"default_when_unset=0 note=mutually_exclusive_see_above\n",
		flag("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS").c_str(), m_panel_l_ff_drain_later_rings_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF requested=%s effective=%u "
		"default_when_unset=0 note=mutually_exclusive_see_above\n",
		flag("ASR10_EXPERIMENT_PANEL_REPLY_71_7E_FF").c_str(), m_panel_d2_reply_71_7e_ff_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PANEL_REPLY_71_FF requested=%s effective=%u "
		"default_when_unset=0 note=mutually_exclusive_see_above\n",
		flag("ASR10_EXPERIMENT_PANEL_REPLY_71_FF").c_str(), m_panel_d1_reply_71_ff_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO requested=%s effective=%u "
		"default_when_unset=0 note=mutually_exclusive_see_above\n",
		flag("ASR10_EXPERIMENT_PANEL_REPLY_71_ZERO").c_str(), m_panel_c_reply_71_zero_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_PAR_DIAGNOSTIC requested=%s "
		"effective_paired_with_DIAG_PAR_VALUE=%u default_when_unset=0 "
		"note=BOTH_this_AND_ASR10_DIAG_PAR_VALUE_required_or_read_port_cb_stays_unbound_baseline_error130\n",
		flag("ASR10_EXPERIMENT_PAR_DIAGNOSTIC").c_str(), m_es5506_diag_par_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_POST_TUNING_INDIRECT_TRACE").c_str(), m_pti.enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_FLAG name=ASR10_EXPERIMENT_TUNING_STALL_TRACE requested=%s effective=%u "
		"default_when_unset=0\n",
		flag("ASR10_EXPERIMENT_TUNING_STALL_TRACE").c_str(), m_tuning_stall_trace_enabled ? 1u : 0u);
	logerror("ASR10_RUN_CONFIG_HEADER end ============================================\n");
}


// filesystem-browser-map.md 4.14 TASK 1/2: a one-shot scan of every
// autovector (1-7, offsets 0x64-0x7c) and every trap vector (0-15,
// offsets 0x80-0xbc), to identify which vector (if any) owns the three
// newly-found static "bra $f87f80" handler fragments (f880b6, f880d6/
// f88108 -- found to fall through into one another with no vector-driven
// frame-build of their own) and f87f76 (a fourth f87f80-continuation
// entry point found in the f87f1a-f87fd0 static map with zero call/branch
// references anywhere in the ROM).
void asr10_boot_state::log_pti_full_vector_scan()
{
	if (!m_pti.enabled || m_pti.full_vector_scan_logged)
		return;
	m_pti.full_vector_scan_logged = true;

	static constexpr std::array<u32, 7> AUTOVECTOR_OFFSETS = { 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c };
	for (size_t i = 0; i < AUTOVECTOR_OFFSETS.size(); i++)
	{
		const u32 h = read_loaded_long(AUTOVECTOR_OFFSETS[i]) & 0x00ffffff;
		logerror("ASR10_PTI_AUTOVECTOR level=%zu addr=%04x handler=%06x\n", i + 1, AUTOVECTOR_OFFSETS[i], h);
	}
	for (u32 trap_num = 0; trap_num <= 15; trap_num++)
	{
		const u32 offset = (32 + trap_num) * 4;
		const u32 h = read_loaded_long(offset) & 0x00ffffff;
		logerror("ASR10_PTI_TRAPVECTOR trap=%u addr=%04x handler=%06x\n", trap_num, offset, h);
		if (h == 0x00f880b6)
			logerror("ASR10_PTI_F880B6_XREF trap %u handler equals f880b6\n", trap_num);
		if (h == 0x00f88108)
			logerror("ASR10_PTI_F88108_XREF trap %u handler equals f88108\n", trap_num);
		if (h == 0x00f87f76)
			logerror("ASR10_PTI_F87F76_XREF trap %u handler equals f87f76\n", trap_num);
		if (h == 0x00012f24)
			logerror("ASR10_PTI_12F24_XREF trap %u handler equals 12f24\n", trap_num);
		if (h == 0x00012f7e)
			logerror("ASR10_PTI_12F7E_XREF trap %u handler equals 12f7e\n", trap_num);
	}
	// filesystem-browser-map.md 4.17 TASK 5: unconditional, one-shot dump
	// of high-view 0xffd0e0-0xffd160 (around the FFFFD10A jsr target used
	// by 002b14's third whitelisted branch) -- read regardless of whether
	// any live path ever reaches it, since this is a pure data read with
	// no side effect on emulated state.
	dump_highview_code_range("pti_ffd10a_region", 0x00ffd0e0, 0x00ffd160);
	// filesystem-browser-map.md 4.17 TASK 2: the raw word at lowmem $00ca
	// itself, one-shot, regardless of live $00ca-list activity.
	logerror("ASR10_PTI_00CA_WORD value=%04x\n", lowmem_word(0x000ca));
}


// filesystem-browser-map.md 4.14 TASK 3: entry-id -> name lookup for the
// nine distinct static paths found into f87f3e/f87f80 (5 direct f87f3e
// callers, plus the 4 trap-vector paths -- trap #1 direct, the trap
// #5/#6 fall-through chain, trap #7, and trap #15 -- that reach f87f80
// without going through f87f3e at all).
static const char *pti_entry_name(u32 entry_id)
{
	switch (entry_id)
	{
		case 1: return "trap1_f87f76_direct";
		case 2: return "trap5_6_chain_f880b6_f880d6";
		case 3: return "trap7_f88108";
		case 4: return "trap15_f88056";
		case 5: return "f87f3e_via_f881d0_trap12_bsr";
		case 6: return "f87f3e_via_f881f0_trap14_jsr";
		case 7: return "f87f3e_via_f89a28";
		case 8: return "f87f3e_via_f89a4e";
		case 9: return "f87f3e_via_f89a68";
		case 10: return "trap6_direct_f880d6";
		default: return "unknown";
	}
}


// Common full-register snapshot logged at every distinct entry point.
void asr10_boot_state::log_pti_entry_stash(u32 entry_id, const char *name, u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.entry_hit_counts[entry_id]++;
	m_pti.last_entry_valid = true;
	m_pti.last_entry_consumed = false;
	m_pti.last_entry_id = entry_id;
	m_pti.last_entry_pc = pc;

	const u32 usp = u32(m_maincpu->state_int(M68K_USP)) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	logerror("ASR10_PTI_ENTRY id=%u name=%s pc=%06x count=%u active_slot=%u "
		"d0=%08x d1=%08x d2=%08x d3=%08x "
		"a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x "
		"usp=%06x sr=%04x panel=\"%s\"\n",
		entry_id, name, pc, m_pti.entry_hit_counts[entry_id], m_dispatcher_rte_frame_slot,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u32(m_maincpu->state_int(M68K_A4)), u32(m_maincpu->state_int(M68K_A5)),
		u32(m_maincpu->state_int(M68K_A6)), usp, sr, m_panel_text);
}


// f87f3e's own body entry (tapped at f87f40's genuine write, since f87f3e
// itself is `move USP,A0`, register-only). Confirms *some* caller of the
// five reached here, and reports which one via the stash -- consuming it
// so a later, unrelated hit can't be misattributed.
void asr10_boot_state::log_pti_f87f3e_body_entry(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.f87f3e_body_entry_count++;
	const bool have_source = m_pti.last_entry_valid && !m_pti.last_entry_consumed;
	logerror("ASR10_PTI_F87F3E_BODY_ENTRY pc=%06x count=%u source_known=%u source_id=%u "
		"source_name=%s source_pc=%06x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.f87f3e_body_entry_count, have_source ? 1u : 0u,
		have_source ? m_pti.last_entry_id : 0u,
		have_source ? pti_entry_name(m_pti.last_entry_id) : "UNKNOWN_STALE",
		have_source ? m_pti.last_entry_pc : 0u, m_dispatcher_rte_frame_slot, m_panel_text);
	if (have_source)
		m_pti.last_entry_consumed = true;
}


// The four trap-vector paths that reach f87f80 WITHOUT going through
// f87f3e (trap #1 direct fall-through, the #5/#6 chain, #7, #15). Each
// re-stashes its own identity as the "last entry" so f87f82's correlation
// below attributes correctly even for these non-f87f3e paths.
void asr10_boot_state::log_pti_f87f80_direct(u32 entry_id, const char *name, u32 pc)
{
	log_pti_entry_stash(entry_id, name, pc);
}


// f87f82 (`move.w A0,($e,A2)`) -- the shared, caller-agnostic proxy.
// Assigns a monotonic sequence number and reports the correlated entry
// only if one is stashed and not yet consumed (i.e. genuinely proven to
// precede this specific hit, not just "the nearest earlier log line").
void asr10_boot_state::log_pti_f87f82_correlated(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.dispatcher_f87f82_hit_count++;
	m_pti.f87f82_seq++;
	const bool have_source = m_pti.last_entry_valid && !m_pti.last_entry_consumed;
	logerror("ASR10_PTI_F87F82_SEQ seq=%u pc=%06x active_slot=%u source_known=%u source_id=%u "
		"source_name=%s source_pc=%06x panel=\"%s\"\n",
		m_pti.f87f82_seq, pc, m_dispatcher_rte_frame_slot, have_source ? 1u : 0u,
		have_source ? m_pti.last_entry_id : 0u,
		have_source ? pti_entry_name(m_pti.last_entry_id) : "UNKNOWN_STALE",
		have_source ? m_pti.last_entry_pc : 0u, m_panel_text);
	if (have_source)
		m_pti.last_entry_consumed = true;
}


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
void asr10_boot_state::log_pti_trap15(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.trap15_hit_count++;
	const u32 slot_ptr = u32(lowmem_word(0x00b6a)) & 0xffff;
	m_pti.trap15_slot_before2 = lowmem_byte(slot_ptr + 2);
	m_pti.trap15_slot_before3 = lowmem_byte(slot_ptr + 3);
	logerror("ASR10_PTI_TRAP15 pc=%06x count=%u d0_bit=%08x b6a_slot_ptr=%04x "
		"slot+2_before=%02x slot+3_before=%02x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.trap15_hit_count, u32(m_maincpu->state_int(M68K_D0)), slot_ptr,
		m_pti.trap15_slot_before2, m_pti.trap15_slot_before3, m_dispatcher_rte_frame_slot, m_panel_text);
	log_pti_entry_stash(4, pti_entry_name(4), pc);
}


// f880d6 (trap #6)'s first genuine access (`move.w ($10,A2),D0`, a read)
// fires unconditionally whether reached via trap #5's fall-through or
// trap #6's own vector directly. If a fresh, unconsumed trap-#5 stash
// (id 2) already precedes it, this is the normal chain -- leave it
// attributed to trap #5 and just consume it here instead of at f87f80,
// since f880d6 is the true unconditional convergence point for that
// chain. Otherwise, trap #6 was entered directly, bypassing trap #5:
// stash a distinct id (10) so it is not misattributed.
void asr10_boot_state::log_pti_f880e0_check(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	const bool via_trap5 = m_pti.last_entry_valid && !m_pti.last_entry_consumed && m_pti.last_entry_id == 2;
	logerror("ASR10_PTI_F880D6_ENTRY pc=%06x via_trap5_chain=%u active_slot=%u panel=\"%s\"\n",
		pc, via_trap5 ? 1u : 0u, m_dispatcher_rte_frame_slot, m_panel_text);
	if (!via_trap5)
		log_pti_entry_stash(10, pti_entry_name(10), pc);
}


// filesystem-browser-map.md 4.15 TASK 3: trap #8 (vector 40, f8812c) --
// `movea.w $b6a.w,A0 / move.w D0,(A0) / rte`: writes D0 into the
// *currently active* slot's own +0 field. Gated purely on `enabled`
// (from reset), per the gate correction -- f880fc/seen_f880fc is proven
// too late (it is a PC inside trap #6's own body) to bound this search.
void asr10_boot_state::log_pti_trap8(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.trap8_hit_count++;
	const u32 slot_ptr = u32(lowmem_word(0x00b6a)) & 0xffff;
	logerror("ASR10_PTI_TRAP8 pc=%06x caller_pc=%06x count=%u d0_delay=%08x "
		"target_slot_ptr=%04x active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_last_distinct_pc, m_pti.trap8_hit_count, u32(m_maincpu->state_int(M68K_D0)),
		slot_ptr, m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
}


// filesystem-browser-map.md 4.15 TASK 3: trap #9 (vector 41, f88138) --
// enqueues node A5 onto an explicit queue A1 (head +0x10, tail +0x12),
// clearing bit 7 of A1+2 once posted. Unlike trap #6/#7/#15, A1 is an
// explicit argument, not $b6a.w -- this is the "post to an arbitrary
// target" primitive, the leading candidate for what actually wakes a
// specific target slot (including slot 0) rather than the caller's own.
void asr10_boot_state::log_pti_trap9_epoch(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.trap9_epoch_hit_count++;
	const u32 a1 = u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u32 slot0_ptr = u32(lowmem_word(0x000c6)) & 0xffff;
	logerror("ASR10_PTI_TRAP9_EPOCH pc=%06x caller_pc=%06x count=%u a1_target=%06x a5_node=%06x "
		"targets_slot0=%u active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_last_distinct_pc, m_pti.trap9_epoch_hit_count, a1, a5,
		a1 == slot0_ptr ? 1u : 0u, m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
}


// filesystem-browser-map.md 4.15 TASK 3: the "$00ca-list" -- a table of
// 26-byte (0x1a) records from $ca.w to $cc.w, each with a countdown at
// +0x14 and a callback function pointer at +0x16, serviced once per tick
// (f88338-f88360, already statically mapped in 4.13's site-1 context).
void asr10_boot_state::log_pti_ca_list_decrement(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ca_list_decrement_count++;
	logerror("ASR10_PTI_CA_LIST_DECREMENT pc=%06x count=%u a0_record=%06x d1_remaining=%08x "
		"active_slot=%u time=%s\n",
		pc, m_pti.ca_list_decrement_count, u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_D1)), m_dispatcher_rte_frame_slot, machine().time().to_string());
}


void asr10_boot_state::log_pti_ca_list_callback(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ca_list_callback_count++;
	logerror("ASR10_PTI_CA_LIST_CALLBACK pc=%06x count=%u a0_record=%06x a1_callback_target=%06x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.ca_list_callback_count, u32(m_maincpu->state_int(M68K_A0)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


// filesystem-browser-map.md 4.15 TASK 3/7: broad net on slot 0's own
// +2/+3 ready-flag bytes specifically (address computed live from $c6.w,
// slot 0 being the table's first entry), independent of which PC writes
// them -- directly answers "other scheduler-ready-bit changes affecting
// slot 0" and TASK 7's "is an expected later request for slot 0 absent".
void asr10_boot_state::log_pti_slot0_ready_bit_write(u32 pc, u32 byte_address, u16 value)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.slot0_ready_bit_write_count++;
	logerror("ASR10_PTI_SLOT0_READY_BIT_WRITE pc=%06x count=%u byte_address=%06x value=%04x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.slot0_ready_bit_write_count, byte_address, value,
		m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
}


void asr10_boot_state::log_pti_trap2_entry(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.trap2_entry_count++;
	m_pti.trap2_a5_before = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	logerror("ASR10_PTI_TRAP2_ENTRY pc=%06x count=%u a5_before=%06x d0=%08x d1=%08x d2=%08x d3=%08x "
		"a6=%06x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.trap2_entry_count, m_pti.trap2_a5_before,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff, m_dispatcher_rte_frame_slot, m_panel_text);
}


// Not a hook on trap #2's actual RTE -- that instruction has no memory
// reference to tap. 7004 is the first write reached *after* rte, and only
// on the path where the carry-clear (success) branch at 7002 was taken;
// this is used purely as a proxy observation that trap #2 succeeded, not
// as a direct measurement of the trap's own return.
void asr10_boot_state::log_pti_trap2_success_observation(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	if (m_pti.trap2_entry_count == 0)
		return; // reached 7004 without a matching trap #2 entry this run -- not this call
	m_pti.trap2_return_count++;
	const u32 a5_after = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	logerror("ASR10_PTI_TRAP2_SUCCESS_OBSERVATION pc=%06x count=%u a5_before=%06x a5_after=%06x "
		"a5_changed=%u sr=%04x carry=%u active_slot=%u panel=\"%s\"\n",
		pc, m_pti.trap2_return_count, m_pti.trap2_a5_before, a5_after,
		a5_after != m_pti.trap2_a5_before ? 1 : 0, sr, BIT(sr, 0), m_dispatcher_rte_frame_slot, m_panel_text);
}


void asr10_boot_state::log_pti_node_field_write(const char *name, u32 pc, u32 byte_address, u32 value)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	u32 &counter = !strcmp(name, "node+2=0x1a") ? m_pti.node_write_1a_count : m_pti.node_write_a6_count;
	counter++;
	logerror("ASR10_PTI_NODE_FIELD_WRITE name=%s pc=%06x count=%u a5=%06x write_address=%06x "
		"write_value=%06x matches_a5_plus_offset=%u active_slot=%u panel=\"%s\"\n",
		name, pc, counter, a5, byte_address, value,
		(byte_address == a5 + 2 || byte_address == a5 + 4) ? 1 : 0,
		m_dispatcher_rte_frame_slot, m_panel_text);
}


void asr10_boot_state::log_pti_trap12_or_14(u32 pc, u32 trap_num)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	u32 &counter = (trap_num == 12) ? m_pti.trap12_count : m_pti.trap14_count;
	counter++;
	const u32 a1 = u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u32 a6 = u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff;
	logerror("ASR10_PTI_TRAP%u pc=%06x count=%u a1=%06x a5=%06x a6=%06x d0=%08x active_slot=%u panel=\"%s\"\n",
		trap_num, pc, counter, a1, a5, a6, u32(m_maincpu->state_int(M68K_D0)),
		m_dispatcher_rte_frame_slot, m_panel_text);
}


// filesystem-browser-map.md 4.13: the three static call sites of `trap #d`
// (trap #13) found outside its own vector-46 nested caller -- f883ac,
// f88df8, f89b54. Static analysis established all three load A1 from the
// same lowmem cell $dc.w immediately before the trap. Entry tap logs full
// register state, the node at A5, and the queue header at A1, all *before*
// the trap runs (i.e. before anything here could be side-effected by it).
void asr10_boot_state::log_pti_trap13_call(u32 pc, u32 site_index)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.trap13_call_counts[site_index]++;
	const u32 a1_full = m_maincpu->state_int(M68K_A1);
	const u32 a5_full = m_maincpu->state_int(M68K_A5);
	const u32 a1 = u32(a1_full) & 0x00ffffff;
	const u32 a5 = u32(a5_full) & 0x00ffffff;
	const u32 a6 = u32(m_maincpu->state_int(M68K_A6)) & 0x00ffffff;
	const u32 usp = u32(m_maincpu->state_int(M68K_USP)) & 0x00ffffff;
	m_pti.trap13_a5_before[site_index] = a5;
	m_pti.trap13_a1_before[site_index] = a1;

	const u16 node0 = lowmem_word(a5 + 0);
	const u16 node2 = lowmem_word(a5 + 2);
	const u16 node4 = lowmem_word(a5 + 4);
	const u16 node6 = lowmem_word(a5 + 6);
	const u16 node8 = lowmem_word(a5 + 8);
	const u16 nodea = lowmem_word(a5 + 0xa);
	const s32 node2_sign_extended = s16(node2);
	const char *type_match =
		node2 == 0x6b1c ? "matches_6b1c" :
		node2 == 0x6cf2 ? "matches_6cf2" :
		node2 == 0xd10a ? "matches_d10a_lo16" :
		u32(node2_sign_extended) == 0xffffd10a ? "matches_ffffd10a_signext" :
		node2 == 0x89a2 ? "matches_89a2_lo16" :
		node2 == 0x001a ? "matches_001a" : "no_known_match";

	const u32 q_act = lowmem_word(a1 + 0);
	const u32 q_side = lowmem_word(a1 + 4);
	const u32 q_head = lowmem_word(a1 + 6);
	const u32 q_tail = lowmem_word(a1 + 8);
	const u32 q_limit = lowmem_word(a1 + 0xa);
	const u32 q_count = lowmem_word(a1 + 0xc);
	const u32 q_aux_e = lowmem_word(a1 + 0xe);
	const u32 q_gate = lowmem_word(a1 + 0x10);
	const u32 q_aux_12 = lowmem_word(a1 + 0x12);
	m_pti.trap13_queue_head_before[site_index] = q_head;
	m_pti.trap13_queue_tail_before[site_index] = q_tail;
	m_pti.trap13_queue_count_before[site_index] = q_count;
	m_pti.trap13_queue_gate_before[site_index] = q_gate;

	logerror("ASR10_PTI_TRAP13_CALL site=%u pc=%06x count=%u a1_full=%08x a1_24=%06x "
		"a5_full=%08x a5_24=%06x a6=%06x d0=%08x d1=%08x d2=%08x d3=%08x usp=%06x "
		"active_slot=%u panel=\"%s\"\n",
		site_index, pc, m_pti.trap13_call_counts[site_index], a1_full, a1, a5_full, a5, a6,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		usp, m_dispatcher_rte_frame_slot, m_panel_text);
	logerror("ASR10_PTI_TRAP13_NODE site=%u pc=%06x a5=%06x node+0=%04x node+2_raw=%04x "
		"node+2_signext=%08x node+2_bus24=%06x node+4=%04x node+6=%04x node+8=%04x "
		"node+a=%04x type_match=%s\n",
		site_index, pc, a5, node0, node2, u32(node2_sign_extended), u32(node2_sign_extended) & 0x00ffffff,
		node4, node6, node8, nodea, type_match);
	logerror("ASR10_PTI_TRAP13_QUEUE_BEFORE site=%u pc=%06x a1=%06x act_ptr=%04x side=%04x "
		"head=%04x tail=%04x limit=%04x count=%04x aux_e=%04x gate=%04x aux_12=%04x\n",
		site_index, pc, a1, q_act, q_side, q_head, q_tail, q_limit, q_count, q_aux_e, q_gate, q_aux_12);
}


// Return-side observation: sites 0/1 tap the genuine lowmem write at the
// instruction immediately after the trap (f883ae, f88dfa); site 2 has no
// write there (f89b56 is a bare `rts`), so it is tapped as a stack-pop
// *read* from low_rom_or_lowmem_r instead -- same technique already used
// for the ae20 slot-0 early-return proxy (4.9).
void asr10_boot_state::log_pti_trap13_return(u32 pc, u32 site_index)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	if (m_pti.trap13_call_counts[site_index] == 0)
		return;
	m_pti.trap13_return_counts[site_index]++;
	const u32 a1 = u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u32 a1_before = m_pti.trap13_a1_before[site_index];
	const u32 a5_before = m_pti.trap13_a5_before[site_index];

	const u32 q_head = lowmem_word(a1_before + 6);
	const u32 q_tail = lowmem_word(a1_before + 8);
	const u32 q_count = lowmem_word(a1_before + 0xc);
	const u32 q_gate = lowmem_word(a1_before + 0x10);

	// Positive-control style reachability check: walk up to 8 links from
	// the (before-call) queue head and see if the enqueued node (a5_before)
	// appears anywhere in that chain.
	u32 walk = q_head;
	bool reachable = false;
	for (int i = 0; i < 8 && walk != 0; i++)
	{
		if (walk == a5_before) { reachable = true; break; }
		walk = lowmem_word(walk + 0);
	}
	if (q_tail == a5_before)
		reachable = true;

	logerror("ASR10_PTI_TRAP13_RETURN site=%u pc=%06x count=%u a1_before=%06x a1_after=%06x "
		"a1_unchanged=%u a5_before=%06x a5_after=%06x a5_unchanged=%u "
		"head_before=%04x head_after=%04x tail_before=%04x tail_after=%04x "
		"count_before=%04x count_after=%04x gate_before=%04x gate_after=%04x "
		"node_reachable_from_head_or_tail=%u active_slot=%u panel=\"%s\"\n",
		site_index, pc, m_pti.trap13_return_counts[site_index], a1_before, a1, a1 == a1_before ? 1u : 0u,
		a5_before, a5, a5 == a5_before ? 1u : 0u,
		m_pti.trap13_queue_head_before[site_index], q_head,
		m_pti.trap13_queue_tail_before[site_index], q_tail,
		m_pti.trap13_queue_count_before[site_index], q_count,
		m_pti.trap13_queue_gate_before[site_index], q_gate,
		reachable ? 1u : 0u, m_dispatcher_rte_frame_slot, m_panel_text);
}


// Shared, caller-agnostic proxy for "execution reached the dispatcher's
// context-save continuation at f87f80" -- f87f80 itself is register-only
// (`move USP,A0`), so f87f82 (`move.w A0,($e,A2)`, a genuine write) is
// tapped instead. Not tied to any one of the three call sites above; a
// hit here only proves *some* path reached this shared code, correlated
// with the call-site counts above by proximity/timing, not causally.
void asr10_boot_state::log_pti_dispatcher_f87f82(u32 pc)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.dispatcher_f87f82_hit_count++;
	logerror("ASR10_PTI_DISPATCHER_F87F82 pc=%06x count=%u active_slot=%u panel=\"%s\"\n",
		pc, m_pti.dispatcher_f87f82_hit_count, m_dispatcher_rte_frame_slot, m_panel_text);
}


void asr10_boot_state::log_pti_dc_pointer_write(u32 pc, u16 value)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.dc_pointer_write_count++;
	logerror("ASR10_PTI_DC_POINTER_WRITE pc=%06x count=%u value=%04x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.dc_pointer_write_count, value, m_dispatcher_rte_frame_slot, m_panel_text);
}


void asr10_boot_state::log_pti_dc_pointer_read(u32 pc, u16 value)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.dc_pointer_read_count++;
	logerror("ASR10_PTI_DC_POINTER_READ pc=%06x count=%u value=%04x active_slot=%u panel=\"%s\"\n",
		pc, m_pti.dc_pointer_read_count, value, m_dispatcher_rte_frame_slot, m_panel_text);
}


// filesystem-browser-map.md 4.16 TASK 3/6: the three confirmed static
// producers of node+2=0x16 (f88e2a, f9068a, f942f2), all reached via
// `trap #3` allocation (alloc without the count-limit check, 4.14). Gated
// purely on `enabled` (from reset), not seen_f880fc, per the 4.15
// correction. Dumps the surrounding gate bytes ($17e.w, $8434.w/$8435.w)
// as plain facts, not asserted as *the* gating condition for all three
// (only f88e2a's own static gate, $17e.w, was confirmed by disassembly).
void asr10_boot_state::log_pti_node16_producer(u32 pc, const char *site_name)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	const u32 site_index = !strcmp(site_name, "f88e2a") ? 0 : !strcmp(site_name, "f9068a") ? 1 : 2;
	m_pti.node16_producer_hit_counts[site_index]++;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	logerror("ASR10_PTI_NODE16_PRODUCER site=%s pc=%06x count=%u a5=%06x d3=%08x "
		"flag_17e=%02x flag_8434=%02x flag_8435=%02x active_slot=%u panel=\"%s\" time=%s\n",
		site_name, pc, m_pti.node16_producer_hit_counts[site_index], a5,
		u32(m_maincpu->state_int(M68K_D3)), lowmem_byte(0x0017e), lowmem_byte(0x08434),
		lowmem_byte(0x08435), m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
}


// filesystem-browser-map.md 4.16 TASK 4/6: the one confirmed static
// producer of node+2=0xd10a, found via disk-image byte search (RAM
// 0x012f88, +0x2600 delta verified). Unlike the 0x16/0x89a2 producers,
// this one targets slot 0 via a *hardcoded* immediate (#$23d4), not a
// variable ($d6.w/$d8.w) -- logged as a plain fact, not generalized.
void asr10_boot_state::log_pti_d10a_producer(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.d10a_producer_hit_count++;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	logerror("ASR10_PTI_D10A_PRODUCER pc=%06x count=%u a5=%06x active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.d10a_producer_hit_count, a5, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


// filesystem-browser-map.md 4.16 TASK 3/6: broad net on $d6.w, the
// confirmed destination of both f88e2a's and f942f2's node+2=0x16 posts
// (parallel to $dc.w's treatment in 4.13).
void asr10_boot_state::log_pti_d6_pointer_write(u32 pc, u16 value)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.d6_pointer_write_count++;
	logerror("ASR10_PTI_D6_POINTER_WRITE pc=%06x count=%u value=%04x active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.d6_pointer_write_count, value, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


void asr10_boot_state::log_pti_d6_pointer_read(u32 pc, u16 value)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.d6_pointer_read_count++;
	logerror("ASR10_PTI_D6_POINTER_READ pc=%06x count=%u value=%04x active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.d6_pointer_read_count, value, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


// filesystem-browser-map.md 4.17 TASK 2: every write to lowmem $00ca
// itself (the candidate list-root cell).
void asr10_boot_state::log_pti_ca_root_write(u32 pc, u16 value)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ca_root_write_count++;
	logerror("ASR10_PTI_CA_ROOT_WRITE pc=%06x count=%u value=%04x active_slot=%u panel=\"%s\" time=%s\n",
		pc, m_pti.ca_root_write_count, value, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


// filesystem-browser-map.md 4.17 TASK 3: every write to $14d4 (countdown)
// or $14d6-$14d9 (long callback pointer) -- the object corrected this
// round to be reached via a SEPARATE tail case (12f6a) from the
// unconditional-branch d10a producer (12f7e), not directly preceding it.
void asr10_boot_state::log_pti_timer_field_write(const char *name, u32 pc, u32 byte_address, u16 value)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	const u32 index = !strcmp(name, "14d4") ? 0 : 1;
	m_pti.timer_field_write_counts[index]++;
	logerror("ASR10_PTI_TIMER_FIELD_WRITE name=%s pc=%06x count=%u byte_address=%06x value=%04x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		name, pc, m_pti.timer_field_write_counts[index], byte_address, value,
		m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
}


// filesystem-browser-map.md 4.17 TASK 1/4: the five distinct entry points
// in the 012efc-012f94 dispatcher family (see the corrected control-flow
// account: 12f66's arm-timer tail loops back to 12f3a, NOT into 12f7e --
// they are proven-separate branches, not a sequential pair).
void asr10_boot_state::log_pti_12_family_entry(const char *name, u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	static const std::array<const char *, 5> NAMES = { "12efc", "12f24", "12f66", "12f76", "12f7e" };
	u32 index = 0;
	for (u32 i = 0; i < NAMES.size(); i++)
		if (!strcmp(name, NAMES[i])) { index = i; break; }
	m_pti.family12_entry_counts[index]++;
	logerror("ASR10_PTI_12FAMILY_ENTRY name=%s pc=%06x count=%u a5=%06x node_plus4=%08x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		name, pc, m_pti.family12_entry_counts[index], u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff,
		lowmem_long(u32(m_maincpu->state_int(M68K_A5)) + 4), m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
}


// filesystem-browser-map.md 4.18: one full 22-byte slot-record dump, used
// by the ae12/ae1a/ae20 taps and the system-wide snapshot at ae20.
void asr10_boot_state::dump_slot_record(const char *tag, u32 slot_addr)
{
	logerror("ASR10_PTI_SLOT_RECORD tag=%s addr=%06x +00=%04x +02=%04x +03=%02x +06=%08x "
		"+0a=%04x +0c=%04x +0e=%04x +10=%04x +12=%04x +14=%04x\n",
		tag, slot_addr, lowmem_word(slot_addr + 0), lowmem_word(slot_addr + 2),
		u32(lowmem_byte(slot_addr + 3)), lowmem_long(slot_addr + 6), lowmem_word(slot_addr + 0xa),
		lowmem_word(slot_addr + 0xc), lowmem_word(slot_addr + 0xe), lowmem_word(slot_addr + 0x10),
		lowmem_word(slot_addr + 0x12), lowmem_word(slot_addr + 0x14));
}


// filesystem-browser-map.md 4.18 TASK 1: tapped at ae12, trap #5's own
// exception-frame push. D0 is always 0 on entry here (established); logs
// the FULL slot-0 record before trap #5/#6 run at all.
void asr10_boot_state::log_pti_ae12_before(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ae12_seq++;
	const u32 slot_ptr = u32(lowmem_word(0x00b6a)) & 0xffff;
	logerror("ASR10_PTI_AE12_BEFORE seq=%u pc=%06x d0=%08x slot_ptr=%04x a5_before=%06x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		m_pti.ae12_seq, pc, u32(m_maincpu->state_int(M68K_D0)), slot_ptr,
		u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff, m_dispatcher_rte_frame_slot, m_panel_text,
		machine().time().to_string());
	dump_slot_record("ae12_before", slot_ptr);
}


// filesystem-browser-map.md 4.18 TASK 1/2: tapped at ae1a, `jsr $87f2.w`'s
// own genuine return-address push -- fires ONLY when ae18's `beq` was NOT
// taken (i.e. bit 7 of D0, post-AND, was set). D0 here is whatever trap
// #5/#6 left it as (established: overwritten at f880e0 to the popped
// queue-head pointer, or 0 if the queue was empty) -- NOT preserved from
// entry, and NOT restored by the trap mechanism.
void asr10_boot_state::log_pti_ae1a_taken(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ae1a_seq++;
	const u32 slot_ptr = u32(lowmem_word(0x00b6a)) & 0xffff;
	const u32 d0 = u32(m_maincpu->state_int(M68K_D0));
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	logerror("ASR10_PTI_AE1A_TAKEN seq=%u pc=%06x d0_full=%08x d0_low_byte=%02x sr=%04x ccr=%02x "
		"slot_ptr=%04x a5=%06x node+0=%04x node+2=%04x node+4=%08x node+6=%04x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		m_pti.ae1a_seq, pc, d0, u32(d0 & 0xff), sr, u32(sr & 0xff), slot_ptr, a5,
		lowmem_word(a5 + 0), lowmem_word(a5 + 2), lowmem_long(a5 + 4), lowmem_word(a5 + 6),
		m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
	dump_slot_record("ae1a_after_trap5_6", slot_ptr);
}


// filesystem-browser-map.md 4.18 TASK 1/3/4: tapped at ae20 (the `rts`
// stack-pop read) -- fires when ae18's `beq` WAS taken (bit 7 of D0 was
// clear). Full slot-0 record, A5/node state, the return address on the
// stack, and a system-wide snapshot of every scheduler slot plus the
// known queue/free-list/timer cells.
void asr10_boot_state::log_pti_ae20_full(u32 pc)
{
	if (!m_pti.enabled || machine().side_effects_disabled())
		return;
	m_pti.ae20_seq++;
	const u32 slot_ptr = u32(lowmem_word(0x00b6a)) & 0xffff;
	const u32 sp = u32(m_maincpu->state_int(M68K_SP)) & 0x00ffffff;
	const u32 a5 = u32(m_maincpu->state_int(M68K_A5)) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u32 return_address = read_stack_long(sp);
	logerror("ASR10_PTI_AE20_FULL seq=%u pc=%06x d0=%08x sr=%04x ccr=%02x sp=%06x "
		"return_address=%06x slot_ptr=%04x a5=%06x node+0=%04x node+2=%04x node+4=%08x node+6=%04x "
		"active_slot=%u panel=\"%s\" time=%s\n",
		m_pti.ae20_seq, pc, u32(m_maincpu->state_int(M68K_D0)), sr, u32(sr & 0xff), sp,
		return_address, slot_ptr, a5, lowmem_word(a5 + 0), lowmem_word(a5 + 2), lowmem_long(a5 + 4),
		lowmem_word(a5 + 6), m_dispatcher_rte_frame_slot, m_panel_text, machine().time().to_string());
	logerror("ASR10_PTI_AE20_STACK_WORDS seq=%u sp=%06x words=\"%04x,%04x,%04x,%04x,%04x,%04x\"\n",
		m_pti.ae20_seq, sp, lowmem_word(sp + 0), lowmem_word(sp + 2), lowmem_word(sp + 4),
		lowmem_word(sp + 6), lowmem_word(sp + 8), lowmem_word(sp + 0xa));
	dump_slot_record("ae20_slot0", slot_ptr);

	// System-wide slot walk, $c6.w-$c8.w, stride 0x16 (4.14).
	const u32 slot_base = u32(lowmem_word(0x000c6)) & 0xffff;
	const u32 slot_bound = u32(lowmem_word(0x000c8)) & 0xffff;
	for (u32 s = slot_base; s < slot_bound; s += 0x16)
	{
		char tag[24];
		snprintf(tag, sizeof(tag), "ae20_snapshot_%06x", s);
		dump_slot_record(tag, s);
	}
	logerror("ASR10_PTI_AE20_GLOBALS seq=%u d6=%04x d8=%04x da=%04x dc=%04x free_list_root_b6c=%04x "
		"alloc_count_b7f=%02x timer_14c0_plus14=%04x timer_14c0_plus16=%08x ca_root=%04x\n",
		m_pti.ae20_seq, lowmem_word(0x000d6), lowmem_word(0x000d8), lowmem_word(0x000da),
		lowmem_word(0x000dc), lowmem_word(0x00b6c), u32(lowmem_byte(0x00b7f)),
		lowmem_word(0x014d4), lowmem_long(0x014d6), lowmem_word(0x000ca));
}


void asr10_boot_state::log_pti_indirect_call(u32 pc, u32 site_index)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	u32 &counter = m_pti.indirect_hit_counts[site_index];
	counter++;
	if (counter > 64 && (counter & (counter - 1)))
		return;
	const pti_indirect_site &site = PTI_INDIRECT_SITES[site_index];
	const u32 target = u32(m_maincpu->state_int(site.reg)) & 0x00ffffff;
	const u32 queue_base = m_lowmem_shadow[0x00c6 >> 1] & 0x00ffffff;
	logerror("ASR10_PTI_INDIRECT name=%s pc=%06x target=%06x classify=%s "
		"active_slot=%u active_slot_a2=%06x queue_base=%06x count=%u panel=\"%s\"\n",
		site.name, pc, target, pti_classify_target(target),
		m_dispatcher_rte_frame_slot, m_dispatcher_rte_frame_a2, queue_base,
		counter, m_panel_text);
	if (site.pc == 0x00002b38 && !m_pti.target_002b38_dump_logged)
	{
		m_pti.target_002b38_dump_logged = true;
		log_pti_dump_target(target);
	}
}


void asr10_boot_state::log_pti_dump_target(u32 address)
{
	address &= 0x00ffffff;
	char tag[48];
	snprintf(tag, sizeof(tag), "pti_002b38_target_%06x", address);
	if (address >= 0x00fc6900)
		dump_highview_code_range(tag, address >= 0x20 ? address - 0x20 : 0, address + 0x40);
	else
		dump_loaded_code_range(tag, address >= 0x20 ? address - 0x20 : 0, address + 0x40);
}


// Both known "trap #9" call sites are in loaded overlay RAM, not static
// ROM (a ROM-wide search found none) -- these two were found live in
// filesystem-browser-map.md 4.5 (slot 1's own resume code, and slot 0's
// ffa26e->00711e tail-jump target) and are not claimed to be exhaustive;
// any other call site would need to be found the same way, live, first.
void asr10_boot_state::log_pti_trap9(u32 pc, const char *caller_tag)
{
	if (!m_pti.enabled || !m_pti.seen_f880fc || machine().side_effects_disabled())
		return;
	m_pti.trap9_hit_count++;
	const u32 a1 = u32(m_maincpu->state_int(M68K_A1)) & 0x00ffffff;
	std::string record;
	for (u32 offset = 0; offset < 0x16; offset += 2)
	{
		if (offset)
			record += ',';
		record += util::string_format("+%02x:%04x", offset, read_loaded_word(a1 + offset));
	}
	logerror("ASR10_PTI_TRAP9 caller=%s pc=%06x node_a1=%06x count=%u active_slot=%u "
		"active_slot_a2=%06x panel=\"%s\" record=\"%s\"\n",
		caller_tag, pc, a1, m_pti.trap9_hit_count, m_dispatcher_rte_frame_slot,
		m_dispatcher_rte_frame_a2, m_panel_text, record.c_str());

	if (!m_pti.vector41_logged)
	{
		m_pti.vector41_logged = true;
		const u32 handler = read_loaded_long(0x000000a4) & 0x00ffffff;
		logerror("ASR10_PTI_TRAP9_VECTOR vector=41 vector_addr=0000a4 handler=%06x\n", handler);
		if (handler >= 0x00fc6900)
			dump_highview_code_range("pti_trap9_handler", handler, handler + 0x60);
		else
			dump_loaded_code_range("pti_trap9_handler", handler, handler + 0x60);
	}
}


void asr10_boot_state::log_fsb_snapshot(const char *milestone, u32 pc)
{
	if (!m_fsb.enabled)
		return;

	std::string fields;
	for (u32 i = 0; i < fsb_state::FIELD_COUNT; i++)
	{
		const fsb_field_runtime &st = m_fsb.fields[i];
		if (i)
			fields += ',';
		fields += util::string_format("%s:seen=%u|value=%04x|wpc=%06x|rpc=%06x|wc=%u|rc=%u",
			FSB_FIELDS[i].name, st.seen ? 1u : 0u, st.value,
			st.last_write_pc, st.last_read_pc, st.write_count, st.read_count);
	}
	std::string entries;
	for (u32 i = 0; i < fsb_state::ENTRY_COUNT; i++)
	{
		if (i)
			entries += ',';
		entries += util::string_format("%s:%u", FSB_ENTRY_NAMES[i], m_fsb.entry_counts[i]);
	}

	logerror("ASR10_FSB_SNAPSHOT milestone=%s pc=%06x slot=%u "
		"table_0544_write_count=%u table_0544_read_count=%u "
		"table_0544_last_write_pc=%06x table_0544_last_read_pc=%06x "
		"table_0544_last_write_entry=%u table_0544_last_read_entry=%u "
		"fdc_last_aux_command=%02x panel=\"%s\" entries=\"%s\" fields=\"%s\"\n",
		milestone, pc, m_dispatcher_rte_frame_slot,
		m_fsb.table_write_count, m_fsb.table_read_count,
		m_fsb.table_last_write_pc, m_fsb.table_last_read_pc,
		m_fsb.table_last_write_entry, m_fsb.table_last_read_entry,
		m_fdc_last_aux_command, m_panel_text, entries.c_str(), fields.c_str());
}


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
void asr10_boot_state::log_fsb_milestone_check_panel_text(u32 pc, u8 data, u32 length_after)
{
	if (!m_fsb.enabled)
		return;
	if (!m_fsb.milestone_a_logged && data == 'T' && length_after <= 2)
	{
		m_fsb.milestone_a_logged = true;
		log_fsb_snapshot("A_before_tuning_kbd", pc);
	}
	if (!m_fsb.milestone_b_logged && data == 'K' && length_after >= 4 && length_after <= 7)
	{
		m_fsb.milestone_b_logged = true;
		log_fsb_snapshot("B_before_keyboard_tuned", pc);
	}
}


// Same shape as dump_loaded_code_range(), but resolves the 0xfc6900-0xffffff
// high-view RAM region via read_highview_word() (a genuine CPU-space read)
// instead of returning the 0xffff placeholder read_loaded_word() gives for
// that range.
void asr10_boot_state::dump_highview_code_range(const char *tag, u32 start, u32 end)
{
	std::string words;
	for (u32 cursor = start; cursor <= end; cursor += 2)
	{
		if (cursor != start)
			words += ',';
		words += util::string_format("%06x:%04x", cursor, read_highview_word(cursor));
	}
	logerror("ASR10_CODE_DUMP tag=%s range=%06x_%06x words=\"%s\"\n", tag, start, end, words.c_str());
}


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


void asr10_boot_state::log_synth_68302_irq_vectors(u8 irq_level, u32 pc, u16 sr)
{
	if (!m_synth_68302_timer_irq_code_dump_logged)
	{
		dump_loaded_code_range("synthetic_irq_target_candidate", 0x00f8b800, 0x00f8b850);
		dump_loaded_code_range("common_error_routine", 0x00f88260, 0x00f882a0);
		dump_loaded_code_range("panel_error_prompt_path", 0x00f89c90, 0x00f89d10);
		m_synth_68302_timer_irq_code_dump_logged = true;
	}

	if (m_synth_68302_timer_irq_vector_dump_logged)
		return;

	const u16 fc6812 = m_m68302_internal_shadow[0x12 >> 1];
	const u16 fc6814 = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816 = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818 = m_m68302_internal_shadow[0x18 >> 1];
	const u16 fc6850 = m_m68302_internal_shadow[0x50 >> 1];
	const u16 fc6852 = m_m68302_internal_shadow[0x52 >> 1];
	const u32 selected_vector_offset = 0x60 + (u32(irq_level) * 4);
	const u32 selected_handler = read_loaded_long(selected_vector_offset) & 0x00ffffff;
	const bool selected_is_f8b826 = selected_handler == 0x00f8b826;
	const bool selected_is_f8b828 = selected_handler == 0x00f8b828;
	std::string vector_words;
	for (u8 level = 1; level <= 7; level++)
	{
		const u32 vector_offset = 0x60 + (u32(level) * 4);
		const u32 handler = read_loaded_long(vector_offset) & 0x00ffffff;
		if (level != 1)
			vector_words += ',';
		vector_words += util::string_format("irq%u@%02x=%06x%s", level, vector_offset, handler,
			(handler == 0x00f8b826 || handler == 0x00f8b828) ? ":target_match" : "");
	}

	logerror("ASR10_SYNTH_IRQ_VECTOR_DUMP selected_irq_level=%u selected_vector_offset=%02x "
		"selected_handler=%06x selected_equals_f8b826=%u selected_equals_f8b828=%u current_pc=%06x "
		"sr=%04x sr_mask=%u fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x fc6850=%04x fc6852=%04x "
		"vectors=\"%s\"\n",
		irq_level, selected_vector_offset, selected_handler,
		selected_is_f8b826 ? 1 : 0, selected_is_f8b828 ? 1 : 0,
		pc, sr, (sr >> 8) & 7, fc6812, fc6814, fc6816, fc6818, fc6850, fc6852,
		vector_words.c_str());
	m_synth_68302_timer_irq_vector_dump_logged = true;
}


void asr10_boot_state::log_runtime_vector_table_for_iack_experiment(u32 pc, u16 sr)
{
	if (m_synth_68302_timer_iack_runtime_vector_dump_logged)
		return;

	m_synth_68302_timer_iack_runtime_vector_dump_logged = true;
	const u16 fc6812 = m_m68302_internal_shadow[0x12 >> 1];
	const u16 fc6814 = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816 = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818 = m_m68302_internal_shadow[0x18 >> 1];
	const u16 fc684a = m_m68302_internal_shadow[0x4a >> 1];
	const u16 fc6850 = m_m68302_internal_shadow[0x50 >> 1];
	const u16 fc6852 = m_m68302_internal_shadow[0x52 >> 1];

	logerror("ASR10_RUNTIME_VECTOR_TABLE_BEGIN pc=%06x sr=%04x sr_mask=%u "
		"dispatcher_count=%u fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x "
		"fc684a=%04x fc6850=%04x fc6852=%04x panel=\"%s\"\n",
		pc, sr, (sr >> 8) & 7, m_runtime_dispatch_entry_count,
		fc6812, fc6814, fc6816, fc6818, fc684a, fc6850, fc6852, m_panel_text);

	std::string candidates;
	for (u16 base = 0; base < 0x100; base += 0x20)
	{
		std::string entries;
		for (u16 vector = base; vector < base + 0x20; vector++)
		{
			const u32 handler = read_loaded_long(u32(vector) * 4) & 0x00ffffff;
			std::string classification;
			if (handler == 0x00f882da)
				classification += "default_error_139";
			if (handler >= 0x00f87f40 && handler <= 0x00f87fc0)
			{
				if (!classification.empty()) classification += '|';
				classification += "dispatcher_related";
			}
			if ((handler >= 0x00f88efc && handler <= 0x00f88f5c))
			{
				if (!classification.empty()) classification += '|';
				classification += "fc6818_rte_candidate";
			}
			if (handler >= 0x00f80000 && handler <= 0x00fbffff)
			{
				if (!classification.empty()) classification += '|';
				classification += "rom";
			}
			else if (handler <= 0x0000ffff)
			{
				if (!classification.empty()) classification += '|';
				classification += "ram";
			}
			if (classification.empty())
				classification = "other";

			if (!entries.empty())
				entries += ',';
			entries += util::string_format("%02x:%06x:%04x:%s",
				vector, handler, read_loaded_word(handler), classification.c_str());

			if (handler != 0x00f882da && handler != 0x00ffffff && handler != 0x00000000)
			{
				if (!candidates.empty())
					candidates += ',';
				candidates += util::string_format("%02x:%06x:%04x:%s",
					vector, handler, read_loaded_word(handler), classification.c_str());
			}
		}
		logerror("ASR10_RUNTIME_VECTOR_TABLE_CHUNK range=%02x_%02x entries=\"%s\"\n",
			base, base + 0x1f, entries.c_str());
	}

	logerror("ASR10_RUNTIME_VECTOR_CANDIDATES non_default=\"%s\"\n", candidates.c_str());
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
			log_runtime_vector_table_for_iack_experiment(pc, sr);

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
		log_synth_68302_irq_vectors(irq_level, pc, sr);
		m_maincpu->set_input_line(irq_level, HOLD_LINE);
	}
}


void asr10_boot_state::log_f87f96_queue_read(u32 byte_address, u16 data, u16 mem_mask)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (pc != 0x00f87f96 && pc != 0x00f87f9a && byte_address != 0x00c6)
		return;

	if (!m_f87f96_code_dump_logged)
	{
		std::string words;
		for (u32 cursor = 0x00f87f40; cursor <= 0x00f87fd0; cursor += 2)
		{
			if (cursor != 0x00f87f40)
				words += ',';
			words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_F87F96_CODE_DUMP words=\"%s\"\n", words.c_str());
		m_f87f96_code_dump_logged = true;
	}

	const u32 a2 = m_maincpu->state_int(M68K_A2) & 0x00ffffff;
	const u32 target_2 = (a2 + 2) & 0x00ffffff;
	const u32 target_3 = (a2 + 3) & 0x00ffffff;
	if (byte_address != 0x00c6 && byte_address != (target_2 & ~1U) && byte_address != (target_3 & ~1U))
		return;

	const bool low_byte = bool(mem_mask & 0x00ff);
	const u8 relevant_byte = low_byte ? u8(data) : u8(data >> 8);
	const u32 effective_address = pc == 0x00f87f96 ? target_2 : pc == 0x00f87f9a ? target_3 : byte_address;
	m_recent_queue_pc = pc;
	m_recent_queue_address = effective_address;
	m_recent_queue_record_base = (pc == 0x00f87f96 || pc == 0x00f87f9a) ? a2 : 0xffffffff;
	m_recent_queue_slot = (m_recent_queue_record_base != 0xffffffff && m_lowmem_shadow[0x00c6 >> 1] &&
		m_recent_queue_record_base >= m_lowmem_shadow[0x00c6 >> 1]) ?
		((m_recent_queue_record_base - m_lowmem_shadow[0x00c6 >> 1]) / 0x16) : 0xffffffff;
	m_recent_queue_previous = (target_2 >> 1) < LOWMEM_WORDS ? m_lowmem_shadow[target_2 >> 1] : 0xffff;
	m_recent_queue_current = m_recent_queue_previous;
	m_recent_queue_data = data;
	m_recent_queue_mem_mask = mem_mask;
	m_recent_queue_write = false;
	m_recent_queue_handler_clear = false;

	m_f87f96_queue_read_count++;
	if (m_f87f96_queue_read_count > 96 && (m_f87f96_queue_read_count & (m_f87f96_queue_read_count - 1)))
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u32 frame_return_pc = ((stack0 & 0x0000ffff) << 16) | (stack1 >> 16);
	const u16 frame_sr = stack0 >> 16;
	const u16 current_sr = u16(m_maincpu->state_int(M68K_SR));

	logerror("ASR10_F87F96_QUEUE_READ pc=%06x previous_pc=%06x opcode=%04x addr=%06x effective_addr=%06x "
		"data=%04x mem_mask=%04x relevant_byte=%02x byte_role=%s "
		"a2=%08x queue_word=%04x queue_byte2=%02x queue_byte3=%02x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a3=%08x sr=%04x sr_mask=%u "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x frame_sr_guess=%04x frame_sr_mask_guess=%u "
		"frame_return_pc_guess=%06x "
		"fc68_last_pc=%06x fc68_last_addr=%06x fc68_last_rw=%c fc68_last_data=%04x "
		"fc68_last_mem_mask=%04x fc68_last_shadow=%04x fc68_last_detail=%s read_count=%u\n",
		pc, m_last_distinct_pc, read_code_word(pc), byte_address, effective_address,
		data, mem_mask, relevant_byte, pc == 0x00f87f96 ? "queue_byte2_to_d0" :
			pc == 0x00f87f9a ? "queue_byte3_to_d1" : "queue_pointer_00c6_to_a2",
		a2, (target_2 >> 1) < LOWMEM_WORDS ? m_lowmem_shadow[target_2 >> 1] : 0xffff,
		(target_2 >> 1) < LOWMEM_WORDS ? u8(m_lowmem_shadow[target_2 >> 1] >> 8) : 0xff,
		(target_2 >> 1) < LOWMEM_WORDS ? u8(m_lowmem_shadow[target_2 >> 1]) : 0xff,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A3)), current_sr, (current_sr >> 8) & 7,
		sp, stack0, stack1, read_stack_long(sp + 8), frame_sr, (frame_sr >> 8) & 7,
		frame_return_pc & 0x00ffffff,
		m_last_fc68_pc, m_last_fc68_address, m_last_fc68_write ? 'W' : 'R',
		m_last_fc68_data, m_last_fc68_mem_mask, m_last_fc68_shadow,
		m68302_register_name(m_last_fc68_address),
		m_f87f96_queue_read_count);
}


void asr10_boot_state::log_f87f96_queue_write(u32 byte_address, u16 previous, u16 current, u16 data, u16 mem_mask)
{
	const u16 queue_pointer_word = m_lowmem_shadow[0x00c6 >> 1];
	const u32 queue_base = queue_pointer_word & 0x00ffffff;
	const bool queue_pointer_write = byte_address == 0x00c6;
	const u32 queue_offset = queue_base ? (byte_address - queue_base) : 0xffffffffU;
	const bool queue_record_index_write =
		queue_base >= 0x0200 && byte_address >= queue_base && byte_address < queue_base + 0x0200 &&
		((queue_offset % 0x16) == 2);
	if (!queue_pointer_write && !queue_record_index_write)
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 record_base = queue_record_index_write ? byte_address - (queue_offset % 0x16) : queue_base;
	const u8 previous_byte2 = u8(previous >> 8);
	const u8 previous_byte3 = u8(previous);
	const u8 current_byte2 = u8(current >> 8);
	const u8 current_byte3 = u8(current);
	if (!m_f880_queue_code_dump_logged && pc >= 0x00f880e0 && pc <= 0x00f88130)
	{
		std::string words;
		for (u32 cursor = 0x00f880e0; cursor <= 0x00f88130; cursor += 2)
		{
			if (cursor != 0x00f880e0)
				words += ',';
			words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_QUEUE_PRODUCER_CODE_DUMP range=f880e0_f88130 trigger_pc=%06x words=\"%s\"\n", pc, words.c_str());
		m_f880_queue_code_dump_logged = true;
	}
	if (pc == 0x00f880fc && !m_fsb.milestone_c_logged)
	{
		m_fsb.milestone_c_logged = true;
		log_fsb_snapshot("C_after_f880fc_finalizer", pc);
	}
	if (pc == 0x00f880fc && m_pti.enabled && !m_pti.seen_f880fc)
	{
		m_pti.seen_f880fc = true;
		logerror("ASR10_PTI_GATE_OPEN pc=%06x panel=\"%s\"\n", pc, m_panel_text);
		log_pti_stack_probe("f880fc", pc);
		log_pti_vectors_34_44_46();
		log_pti_full_vector_scan();
	}
	if (!m_f8ce_queue_code_dump_logged && pc >= 0x00f8ce20 && pc <= 0x00f8ce50)
	{
		std::string words;
		for (u32 cursor = 0x00f8ce20; cursor <= 0x00f8ce50; cursor += 2)
		{
			if (cursor != 0x00f8ce20)
				words += ',';
			words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_QUEUE_PRODUCER_CODE_DUMP range=f8ce20_f8ce50 trigger_pc=%06x words=\"%s\"\n", pc, words.c_str());
		m_f8ce_queue_code_dump_logged = true;
	}
	m_recent_queue_pc = pc;
	m_recent_queue_address = byte_address;
	m_recent_queue_record_base = record_base;
	m_recent_queue_slot = queue_record_index_write ? queue_offset / 0x16 : 0xffffffffU;
	m_recent_queue_previous = previous;
	m_recent_queue_current = current;
	m_recent_queue_data = data;
	m_recent_queue_mem_mask = mem_mask;
	m_recent_queue_write = true;
	m_recent_queue_handler_clear = pc == 0x00f87fb0 && current == 0;

	m_f87f96_queue_write_count++;
	if (m_f87f96_queue_write_count > 96 && (m_f87f96_queue_write_count & (m_f87f96_queue_write_count - 1)))
		return;

	logerror("ASR10_F87F96_QUEUE_WRITE pc=%06x addr=%06x data=%04x mem_mask=%04x previous=%04x current=%04x "
		"queue_base_from_00c6=%04x record_base=%06x record_offset=%02x record_slot=%u "
		"previous_byte2=%02x previous_byte3=%02x current_byte2=%02x current_byte3=%02x "
		"made_unequal=%u made_equal=%u changed_by_handler_clear=%u "
		"fc6814=%04x fc6814_bits_3_1_0=%u%u%u fc6816=%04x fc6816_bits_15_14_13_10_7=%u%u%u%u%u fc6818=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x sr_mask=%u "
		"sp=%06x stack0=%08x stack1=%08x "
		"fc68_last_pc=%06x fc68_last_addr=%06x fc68_last_rw=%c fc68_last_data=%04x "
		"fc68_last_mem_mask=%04x fc68_last_shadow=%04x fc68_last_detail=%s write_count=%u\n",
		pc, byte_address, data, mem_mask, previous, current, queue_pointer_word, record_base,
		queue_record_index_write ? queue_offset : 0xffffffffU,
		queue_record_index_write ? queue_offset / 0x16 : 0xffffffffU,
		previous_byte2, previous_byte3, current_byte2, current_byte3,
		current_byte2 != current_byte3 ? 1 : 0, current_byte2 == current_byte3 ? 1 : 0,
		(pc == 0x00f87fb0 && current == 0) ? 1 : 0,
		m_m68302_internal_shadow[0x14 >> 1],
		BIT(m_m68302_internal_shadow[0x14 >> 1], 3), BIT(m_m68302_internal_shadow[0x14 >> 1], 1),
		BIT(m_m68302_internal_shadow[0x14 >> 1], 0),
		m_m68302_internal_shadow[0x16 >> 1],
		BIT(m_m68302_internal_shadow[0x16 >> 1], 15), BIT(m_m68302_internal_shadow[0x16 >> 1], 14),
		BIT(m_m68302_internal_shadow[0x16 >> 1], 13), BIT(m_m68302_internal_shadow[0x16 >> 1], 10),
		BIT(m_m68302_internal_shadow[0x16 >> 1], 7),
		m_m68302_internal_shadow[0x18 >> 1],
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), (u16(m_maincpu->state_int(M68K_SR)) >> 8) & 7,
		sp, read_stack_long(sp), read_stack_long(sp + 4),
		m_last_fc68_pc, m_last_fc68_address, m_last_fc68_write ? 'W' : 'R',
		m_last_fc68_data, m_last_fc68_mem_mask, m_last_fc68_shadow,
		m68302_register_name(m_last_fc68_address),
		m_f87f96_queue_write_count);
}


void asr10_boot_state::log_dispatcher_rte_first_pc_probe(u32 pc)
{
	if (!m_dispatcher_rte_first_pc_pending || pc == 0x00f87fc0)
		return;

	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const bool frame_pc_seen = pc == m_dispatcher_rte_frame_pc;
	const bool candidate_downstream =
		pc == 0x00007308 || pc == 0x00f8d020 || pc == 0x00f8d05a ||
		pc == 0x00f88f06 || pc == 0x00f88f22 || pc == 0x00f8d072;

	logerror("ASR10_DISPATCHER_RTE_FIRST_PC actual_pc=%06x previous_pc=%06x "
		"actual_sr=%04x actual_sr_mask=%u actual_sp=%06x "
		"frame_pc=%06x frame_sr=%04x dispatcher_sr_before_rte=%04x frame_pc_seen=%u "
		"candidate_downstream=%u immediate_iack_seen=%u iack_pc=%06x iack_sr=%04x "
		"iack_level=%u iack_vector=%02x "
		"fc6814_before_rte=%04x fc6816_before_rte=%04x fc6818_before_rte=%04x "
		"fc6814_now=%04x fc6816_now=%04x fc6818_now=%04x "
		"rte_count=%u slot=%u slot_base=%06x panel=\"%s\"\n",
		pc, m_last_pc, sr, (sr >> 8) & 7, sp,
		m_dispatcher_rte_frame_pc, m_dispatcher_rte_frame_sr, m_dispatcher_rte_current_sr,
		frame_pc_seen ? 1 : 0, candidate_downstream ? 1 : 0,
		m_dispatcher_rte_iack_seen ? 1 : 0, m_dispatcher_rte_iack_pc,
		m_dispatcher_rte_iack_sr, m_dispatcher_rte_iack_level, m_dispatcher_rte_iack_vector,
		m_dispatcher_rte_fc6814, m_dispatcher_rte_fc6816, m_dispatcher_rte_fc6818,
		m_m68302_internal_shadow[0x14 >> 1], m_m68302_internal_shadow[0x16 >> 1],
		m_m68302_internal_shadow[0x18 >> 1],
		m_f87f96_queue_rte_count, m_dispatcher_rte_frame_slot, m_dispatcher_rte_frame_a2,
		m_panel_text);

	m_dispatcher_rte_first_pc_pending = false;
	m_dispatcher_rte_first_pc_logged = true;

	log_tuning_stall_candidate_dump(pc);
	if (m_fsb.enabled)
	{
		char milestone[48];
		snprintf(milestone, sizeof(milestone), "D_slot%u_resume", m_dispatcher_rte_frame_slot);
		log_fsb_snapshot(milestone, pc);
		log_fsb_code_dumps(pc);
	}
	if (m_pti.enabled && m_pti.seen_f880fc)
	{
		char milestone[48];
		snprintf(milestone, sizeof(milestone), "rte_resume_slot%u", m_dispatcher_rte_frame_slot);
		log_pti_stack_probe(milestone, pc);
	}
}


void asr10_boot_state::log_tuning_stall_candidate_dump(u32 pc)
{
	if (!m_tuning_stall_trace_enabled)
		return;

	bool *logged = nullptr;
	u32 start = 0;
	u32 end = 0;

	switch (pc)
	{
	case 0x00ffc85a: logged = &m_tuning_stall_dump_ffc85a_logged; start = 0x00ffc830; end = 0x00ffc8d0; break;
	case 0x00ff9106: logged = &m_tuning_stall_dump_ff9106_logged; start = 0x00ff90d0; end = 0x00ff9170; break;
	case 0x0000ae18: logged = &m_tuning_stall_dump_00ae14_logged; start = 0x0000adf0; end = 0x0000ae90; break;
	case 0x000068ae: logged = &m_tuning_stall_dump_0068a8_logged; start = 0x00006880; end = 0x00006920; break;
	case 0x000077a0: logged = &m_tuning_stall_dump_00779c_logged; start = 0x00007770; end = 0x00007810; break;
	default: return;
	}

	if (*logged)
		return;
	*logged = true;
	dump_loaded_code_range("tuning_stall_callback", start, end);

	// Slot 0/4's downstream vector targets and slots 1/3's own resume code
	// are dumped via the table-driven FSB_DUMP_TARGETS -- ffc85a/ff9106/
	// ae18/68ae are all trigger_pc values in that table -- from the
	// log_fsb_code_dumps() call in log_dispatcher_rte_first_pc_probe(),
	// which runs regardless of m_tuning_stall_trace_enabled (this function
	// is gated on it, so a call added here would depend on that unrelated
	// flag too).

	// One-shot, read-only: capture the trap #7/#8 vector targets (vectors 39/40,
	// addresses 0x9c/0xa0) and the four subroutines slot5's main loop calls
	// ($7cc4, $7164, $bf28, $bf5a), to trace whether they lead back into the
	// dispatcher/scheduler (f87f40-f87fd0) rather than assuming it from shape
	// alone.
	if (!m_tuning_stall_dump_trap_vectors_logged)
	{
		m_tuning_stall_dump_trap_vectors_logged = true;
		logerror("ASR10_TUNING_STALL_TRAP_VECTORS trap7_vector_addr=0000009c trap7_target=%06x "
			"trap8_vector_addr=000000a0 trap8_target=%06x\n",
			read_loaded_long(0x0000009c) & 0x00ffffff, read_loaded_long(0x000000a0) & 0x00ffffff);
	}
	if (!m_tuning_stall_dump_7cc4_logged)
	{
		m_tuning_stall_dump_7cc4_logged = true;
		dump_loaded_code_range("tuning_stall_sub_7cc4", 0x00007ca0, 0x00007d40);
	}
	if (!m_tuning_stall_dump_7164_logged)
	{
		m_tuning_stall_dump_7164_logged = true;
		dump_loaded_code_range("tuning_stall_sub_7164", 0x00007140, 0x000071e0);
	}
	if (!m_tuning_stall_dump_bf28_logged)
	{
		m_tuning_stall_dump_bf28_logged = true;
		dump_loaded_code_range("tuning_stall_sub_bf28", 0x0000bf00, 0x0000bfa0);
	}
	if (!m_tuning_stall_dump_bf5a_logged)
	{
		m_tuning_stall_dump_bf5a_logged = true;
		dump_loaded_code_range("tuning_stall_sub_bf5a", 0x0000bf30, 0x0000bfd0);
	}
}


void asr10_boot_state::log_tuning_stall_save_probe(u32 pc)
{
	// Read-only: fires at the two points in the ROM dispatcher-suspend path
	// (f87f5c, immediately after `movea.w $b6a.w,A2` and before the two
	// `bset D0,(n,A2)` instructions; f87f64, immediately after both bsets)
	// that determine whether suspending the currently-running task makes its
	// own slot "pending" again (byte2 != byte3), which would explain how a
	// task whose own code never falls through to rts can still be the thing
	// the f87f92 scan loop keeps re-dispatching. Gated on having already seen
	// the slot5 tuning-stall callback dispatch (m_tuning_stall_dump_00779c_
	// logged) -- otherwise the first hit here is from unrelated, much-earlier
	// boot activity, since this dispatcher path is used throughout the whole
	// run, not just for this investigation. Bounded to a handful of samples,
	// not a strict one-shot, to see the pattern rather than a single instant.
	if (!m_tuning_stall_trace_enabled || !m_tuning_stall_dump_00779c_logged)
		return;
	const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
	if (a2 != 0x002442)
		return;
	if (pc == 0x00f87f5c && m_tuning_stall_save_before_count < 5)
	{
		m_tuning_stall_save_before_count++;
		const u32 d0 = u32(m_maincpu->state_int(M68K_D0));
		logerror("ASR10_TUNING_STALL_SAVE_BEFORE sample=%u pc=%06x a2=%06x d0=%08x "
			"byte2=%02x byte3=%02x lowmem_0b6a=%04x\n",
			m_tuning_stall_save_before_count, pc, a2, d0,
			lowmem_byte(a2 + 2), lowmem_byte(a2 + 3), lowmem_word(0x0b6a));
	}
	else if (pc == 0x00f87f64 && m_tuning_stall_save_after_count < 5)
	{
		m_tuning_stall_save_after_count++;
		const u32 d0 = u32(m_maincpu->state_int(M68K_D0));
		logerror("ASR10_TUNING_STALL_SAVE_AFTER sample=%u pc=%06x a2=%06x d0=%08x "
			"byte2=%02x byte3=%02x lowmem_0b6a=%04x\n",
			m_tuning_stall_save_after_count, pc, a2, d0,
			lowmem_byte(a2 + 2), lowmem_byte(a2 + 3), lowmem_word(0x0b6a));
	}
}


void asr10_boot_state::log_dispatcher_rte_candidate_pc(u32 pc)
{
	bool *logged = nullptr;
	const char *tag = nullptr;
	u32 start = 0;
	u32 end = 0;

	switch (pc)
	{
	case 0x00007308:
		logged = &m_dispatcher_rte_candidate_dump_007308_logged;
		tag = "ASR10_DISPATCHER_RTE_CANDIDATE_007308";
		start = 0x000072c0;
		end = 0x00007380;
		break;
	case 0x00f8d020:
		logged = &m_dispatcher_rte_candidate_dump_f8d020_logged;
		tag = "ASR10_DISPATCHER_RTE_CANDIDATE_F8D020";
		start = 0x00f8d000;
		end = 0x00f8d080;
		break;
	case 0x00f8d05a:
		logged = &m_dispatcher_rte_candidate_dump_f8d05a_logged;
		tag = "ASR10_DISPATCHER_RTE_CANDIDATE_F8D05A";
		start = 0x00f8d040;
		end = 0x00f8d080;
		break;
	case 0x00f88f06:
		logged = &m_dispatcher_rte_candidate_dump_f88f06_logged;
		tag = "ASR10_DISPATCHER_RTE_VECTOR_F88F06";
		start = 0x00f88efc;
		end = 0x00f88f30;
		break;
	case 0x00f88f22:
		logged = &m_dispatcher_rte_candidate_dump_f88f22_logged;
		tag = "ASR10_DISPATCHER_RTE_VECTOR_F88F22";
		start = 0x00f88f18;
		end = 0x00f88f4c;
		break;
	case 0x00f8d072:
		logged = &m_dispatcher_rte_candidate_dump_f8d072_logged;
		tag = "ASR10_DISPATCHER_RTE_VECTOR_F8D072";
		start = 0x00f8d040;
		end = 0x00f8d090;
		break;
	default:
		return;
	}

	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	logerror("%s pc=%06x previous_pc=%06x opcode=%04x sr=%04x sr_mask=%u sp=%06x "
		"stack0=%08x stack1=%08x recent_frame_pc=%06x recent_frame_sr=%04x "
		"recent_iack_seen=%u recent_iack_vector=%02x rte_count=%u\n",
		tag, pc, m_last_pc, read_loaded_word(pc), sr, (sr >> 8) & 7, sp,
		read_stack_long(sp), read_stack_long(sp + 4),
		m_dispatcher_rte_frame_pc, m_dispatcher_rte_frame_sr,
		m_dispatcher_rte_iack_seen ? 1 : 0, m_dispatcher_rte_iack_vector,
		m_f87f96_queue_rte_count);

	if (!*logged)
	{
		dump_loaded_code_range(tag, start, end);
		*logged = true;
	}
}


void asr10_boot_state::log_f87f96_queue_rte(int state)
{
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (pc != 0x00f87fc0 && m_last_pc != 0x00f87fc0)
		return;

	m_f87f96_queue_rte_count++;
	if (m_f87f96_queue_rte_count > 96 && (m_f87f96_queue_rte_count & (m_f87f96_queue_rte_count - 1)))
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u32 stack0 = read_stack_long(sp);
	const u32 stack1 = read_stack_long(sp + 4);
	const u16 frame_sr = stack0 >> 16;
	const u32 frame_return_pc = ((stack0 & 0x0000ffff) << 16) | (stack1 >> 16);
	const u16 current_sr = u16(m_maincpu->state_int(M68K_SR));
	const u16 queue_pointer_word = m_lowmem_shadow[0x00c6 >> 1];
	const u32 a2 = m_maincpu->state_int(M68K_A2) & 0x00ffffff;
	const u32 queue_base = queue_pointer_word;
	const u32 slot_index = (queue_base >= 0x0200 && a2 >= queue_base && a2 < queue_base + 0x0200) ?
		((a2 - queue_base) / 0x16) : 0xffffffffU;
	const u16 fc6814_before = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816_before = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818_before = m_m68302_internal_shadow[0x18 >> 1];
	if constexpr (ASR10_EXPERIMENT_FC6814_ACK_PENDING_000B)
	{
		if (fc6814_before == 0x000b)
		{
			m_m68302_internal_shadow[0x14 >> 1] = fc6814_before & ~u16(0x000b);
			logerror("ASR10_EXPERIMENT_FC6814_ACK pc=%06x old=%04x new=%04x reason=dispatcher_rte "
				"fc6816_before=%04x fc6816_after=%04x fc6818=%04x\n",
				pc, fc6814_before, m_m68302_internal_shadow[0x14 >> 1],
				fc6816_before, m_m68302_internal_shadow[0x16 >> 1], fc6818_before);
		}
	}
	if constexpr (ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2480)
	{
		if (fc6816_before & 0x2480)
		{
			m_m68302_internal_shadow[0x16 >> 1] = fc6816_before & ~u16(0x2480);
			logerror("ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE pc=%06x old=%04x new=%04x reason=dispatcher_rte "
				"fc6814=%04x fc6818=%04x\n",
				pc, fc6816_before, m_m68302_internal_shadow[0x16 >> 1],
				m_m68302_internal_shadow[0x14 >> 1], fc6818_before);
		}
	}
	if constexpr (ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER)
	{
		const u16 fc6814_now = m_m68302_internal_shadow[0x14 >> 1];
		const u16 fc6816_now = m_m68302_internal_shadow[0x16 >> 1];
		if (!m_fc6816_service_2400_clear_experiment_done &&
			m_fc6816_service_2400_set_by_runtime &&
			m_fc6816_service_0d06_set_after_runtime &&
			!(fc6814_now & 0x2400) &&
			(m_f87f96_queue_rte_count > m_fc6816_service_0d06_rte_count) &&
			(fc6816_now & 0x2400))
		{
			const u16 fc6816_new = fc6816_now & ~u16(0x2400);
			m_m68302_internal_shadow[0x16 >> 1] = fc6816_new;
			m_fc6816_service_2400_clear_experiment_done = true;
			logerror("ASR10_EXPERIMENT_FC6816_CLEAR_SERVICE_2400_AFTER_SETTER pc=%06x previous_pc=%06x "
				"reason=dispatcher_rte_after_00bf22 old_fc6816=%04x new_fc6816=%04x fc6814=%04x fc6818=%04x "
				"fc6884=%04x fc6894=%04x lowmem_0d06=%04x lowmem_0e82=%04x "
				"dispatcher_count=%u rte_count=%u setter_pc=%06x setter_rte_count=%u "
				"lowmem_0d06_pc=%06x lowmem_0d06_rte_count=%u "
				"recent_queue_pc=%06x recent_queue_rw=%c recent_queue_addr=%06x recent_queue_record=%06x "
				"recent_queue_slot=%u recent_queue_previous=%04x recent_queue_current=%04x "
				"recent_queue_data=%04x recent_queue_mem_mask=%04x recent_queue_handler_clear=%u panel=\"%s\"\n",
				pc, m_last_distinct_pc, fc6816_now, fc6816_new, fc6814_now, fc6818_before,
				m_m68302_internal_shadow[0x84 >> 1], m_m68302_internal_shadow[0x94 >> 1],
				m_lowmem_shadow[0x0d06 >> 1], m_lowmem_shadow[0x0e82 >> 1],
				m_runtime_dispatch_entry_count, m_f87f96_queue_rte_count,
				m_fc6816_service_setter_pc, m_fc6816_service_setter_rte_count,
				m_fc6816_service_0d06_pc, m_fc6816_service_0d06_rte_count,
				m_recent_queue_pc, m_recent_queue_write ? 'W' : 'R', m_recent_queue_address,
				m_recent_queue_record_base, m_recent_queue_slot, m_recent_queue_previous,
				m_recent_queue_current, m_recent_queue_data, m_recent_queue_mem_mask,
				m_recent_queue_handler_clear ? 1 : 0, m_panel_text);
		}
	}
	const u16 fc6814_after_experiment = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816_after_experiment = m_m68302_internal_shadow[0x16 >> 1];

	m_queue_rte_after_pending = true;
	m_queue_rte_after_count = m_f87f96_queue_rte_count;
	m_queue_rte_before_pc = pc;
	m_queue_rte_last_return_pc = frame_return_pc & 0x00ffffff;
	m_queue_rte_before_fc6814 = fc6814_after_experiment;
	m_queue_rte_before_fc6816 = fc6816_after_experiment;
	m_queue_rte_before_fc6818 = fc6818_before;
	m_dispatcher_rte_first_pc_pending = true;
	m_dispatcher_rte_first_pc_logged = false;
	m_dispatcher_rte_iack_seen = false;
	m_dispatcher_rte_iack_vector = 0xff;
	m_dispatcher_rte_iack_level = 0xff;
	m_dispatcher_rte_iack_pc = 0xffffffff;
	m_dispatcher_rte_iack_sr = 0;
	m_dispatcher_rte_frame_pc = frame_return_pc & 0x00ffffff;
	m_dispatcher_rte_frame_sp = sp;
	m_dispatcher_rte_frame_a2 = a2;
	m_dispatcher_rte_frame_slot = slot_index;
	m_dispatcher_rte_frame_sr = frame_sr;
	m_dispatcher_rte_current_sr = current_sr;
	m_dispatcher_rte_fc6814 = fc6814_after_experiment;
	m_dispatcher_rte_fc6816 = fc6816_after_experiment;
	m_dispatcher_rte_fc6818 = fc6818_before;

	std::string stack_words;
	for (u8 index = 0; index < 16; index++)
	{
		if (index)
			stack_words += ',';
		const u32 address = (sp + index * 2) & 0x00ffffff;
		stack_words += util::string_format("%06x:%04x", address, read_loaded_word(address));
	}

	std::string slot_words;
	if (slot_index != 0xffffffffU)
	{
		for (u8 offset = 0; offset < 0x16; offset += 2)
		{
			if (offset)
				slot_words += ',';
			const u32 address = (a2 + offset) & 0x00ffffff;
			slot_words += util::string_format("+%02x@%06x:%04x", offset, address, read_loaded_word(address));
		}
	}
	else
	{
		slot_words = "not_queue_slot";
	}

	logerror("ASR10_DISPATCHER_RTE_PRE pc=%06x previous_pc=%06x sr=%04x sr_mask=%u "
		"sp=%06x usp=%06x ssp=%06x stack_words=\"%s\" "
		"frame_sr=%04x frame_sr_mask=%u frame_pc=%06x "
		"a2=%06x slot=%u slot_record=\"%s\" "
		"lowmem_0b6a=%04x lowmem_0b6c=%04x lowmem_0b7f=%04x "
		"fc6814=%04x fc6816=%04x fc6818=%04x rte_count=%u\n",
		pc, m_last_distinct_pc, current_sr, (current_sr >> 8) & 7,
		sp, u32(m_maincpu->state_int(M68K_USP)) & 0x00ffffff,
		u32(m_maincpu->state_int(M68K_ISP)) & 0x00ffffff,
		stack_words.c_str(), frame_sr, (frame_sr >> 8) & 7,
		frame_return_pc & 0x00ffffff, a2, slot_index, slot_words.c_str(),
		m_lowmem_shadow[0x0b6a >> 1], m_lowmem_shadow[0x0b6c >> 1],
		m_lowmem_shadow[0x0b7e >> 1] & 0x00ff,
		fc6814_after_experiment, fc6816_after_experiment, fc6818_before,
		m_f87f96_queue_rte_count);

	logerror("ASR10_F87F96_QUEUE_RTE state=%d pc=%06x previous_pc=%06x opcode=%04x "
		"sr=%04x sr_mask=%u sp=%06x stack0=%08x stack1=%08x stack2=%08x "
		"frame_sr_guess=%04x frame_sr_mask_guess=%u frame_return_pc_guess=%06x "
		"queue_base_from_00c6=%04x "
		"fc68_int_mask=%04x fc68_int_pending=%04x fc68_int_in_service=%04x fc68_int_control=%04x "
		"fc6814_bits_3_1_0=%u%u%u fc6816_bits_15_14_13_10_7=%u%u%u%u%u "
		"fc68_last_pc=%06x fc68_last_addr=%06x fc68_last_rw=%c fc68_last_data=%04x "
		"fc68_last_mem_mask=%04x fc68_last_shadow=%04x fc68_last_detail=%s rte_count=%u\n",
		state, pc, m_last_distinct_pc, read_loaded_word(0x00f87fc0),
		current_sr, (current_sr >> 8) & 7, sp, stack0, stack1, read_stack_long(sp + 8),
		frame_sr, (frame_sr >> 8) & 7, frame_return_pc & 0x00ffffff,
		queue_pointer_word,
		m_m68302_internal_shadow[0x12 >> 1], fc6814_after_experiment, fc6816_after_experiment, fc6818_before,
		BIT(fc6814_after_experiment, 3), BIT(fc6814_after_experiment, 1), BIT(fc6814_after_experiment, 0),
		BIT(fc6816_after_experiment, 15), BIT(fc6816_after_experiment, 14), BIT(fc6816_after_experiment, 13),
		BIT(fc6816_after_experiment, 10), BIT(fc6816_after_experiment, 7),
		m_last_fc68_pc, m_last_fc68_address, m_last_fc68_write ? 'W' : 'R',
		m_last_fc68_data, m_last_fc68_mem_mask, m_last_fc68_shadow,
		m68302_register_name(m_last_fc68_address), m_f87f96_queue_rte_count);
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
		"media_mounted=%u ready=%u motor=%u density=%s read_source=upd72069_device stubbed=0 "
		"total_fifo_reads=%u data_phase_reads=%u result_phase_reads=%u tc_asserted=0\n",
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
		floppy && floppy->floppy_is_hd() ? "hd" : "dd",
		m_fdc_cmd46_total_fifo_reads, m_fdc_cmd46_msr_exm_seen_count,
		m_fdc_cmd46_total_fifo_reads - m_fdc_cmd46_msr_exm_seen_count);

	if (m_fdc_cmd46_transaction == 1 && !m_fdc_cmd46_first_data_logged)
	{
		m_fdc_cmd46_first_data_logged = true;
		std::string hex;
		for (u8 b : m_fdc_cmd46_first_data_bytes)
			hex += util::string_format("%02x", b);
		logerror("ASR10_TASK5_SECTOR_DATA txn=1 c=%02x h=%02x r=%02x first64=\"%s\"\n",
			c, h, r, hex.c_str());
	}

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
void asr10_boot_state::log_pc_summary(const char *reason, u32 pc)
{
	const u16 opcode = read_loaded_word(pc);
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
		accessed_address = (u32(read_loaded_word(pc + 2)) << 16) | read_loaded_word(pc + 4);
	else if ((opcode & 0xfff8) == 0x0810)
	{
		accessed_address = m_maincpu->state_int(M68K_A0 + (opcode & 7)) & 0x00ffffff;
		tested_bit = read_loaded_word(pc + 2) & 7;
	}

	for (u32 candidate = pc; candidate < pc + 0x20; candidate += 2)
	{
		const u16 candidate_opcode = read_loaded_word(candidate);
		u32 candidate_target = 0xffffffffU;
		const char *candidate_kind = nullptr;
		if ((candidate_opcode & 0xf000) == 0x6000)
		{
			const s8 displacement = s8(candidate_opcode);
			candidate_kind = "bcc_bra_bsr";
			candidate_target = displacement ?
				((candidate + 2 + displacement) & 0x00ffffff) :
				((candidate + 2 + s16(read_loaded_word(candidate + 2))) & 0x00ffffff);
		}
		else if ((candidate_opcode & 0xf0f8) == 0x50c8)
		{
			candidate_kind = "dbcc";
			candidate_target = (candidate + 2 + s16(read_loaded_word(candidate + 2))) & 0x00ffffff;
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
	const u16 poll_opcode = read_loaded_word(poll_pc);
	if (poll_opcode == 0x0839)
	{
		tested_bit = read_loaded_word(poll_pc + 2) & 7;
		accessed_address = (u32(read_loaded_word(poll_pc + 4)) << 16) | read_loaded_word(poll_pc + 6);
	}
	else if (poll_opcode == 0x1039)
	{
		accessed_address = (u32(read_loaded_word(poll_pc + 2)) << 16) | read_loaded_word(poll_pc + 4);
	}
	else if ((poll_opcode & 0xfff8) == 0x0810)
	{
		accessed_address = m_maincpu->state_int(M68K_A0 + (poll_opcode & 7)) & 0x00ffffff;
		tested_bit = read_loaded_word(poll_pc + 2) & 7;
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
		if (read_loaded_word(candidate) == 0xb683 && read_loaded_word(candidate + 2) == 0x2f03 &&
			read_loaded_word(candidate + 4) == 0x261f && read_loaded_word(candidate + 6) == 0x5383 &&
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

	if (m_fsb.enabled && !m_fsb.milestone_e_logged)
	{
		m_fsb.milestone_e_logged = true;
		log_fsb_snapshot("E_final_idle", pc);
	}
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
	if (m_panel_c_parser_trace_active && pc != m_panel_c_parser_trace_last_pc)
	{
		m_panel_c_parser_trace_last_pc = pc;
		m_panel_c_parser_trace_count++;
		log_panel_c_parser_trace(pc, "pc");
		if (pc == 0x00f89a9a)
			panel_c_parser_trace_stop("reached_f89a9a", pc);
		else if (pc == 0x00f89aec)
			panel_c_parser_trace_stop("reached_f89aec", pc);
		else if (pc == 0x00ffb3e4 && !m_panel_l_final_idle_probe_waiting)
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
	if (m_panel_l_ff_drain_later_rings_enabled && m_panel_l_final_idle_probe_waiting &&
		!m_panel_l_aborted && !m_panel_l_done && !machine().side_effects_disabled())
	{
		if (pc == 0x00f89a9a && !m_panel_l_final_idle_f89a9a_zero_seen)
		{
			logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=final_idle_f89a9a_entry "
				"pc=%06x previous_pc=%06x count_03bc=%02x idle_03c5=%02x "
				"parser_state_03c0=%04x last_parser_pc=%06x rx_valid=%u\n",
				pc, m_last_distinct_pc, lowmem_byte(0x03bc), lowmem_byte(0x03c5),
				lowmem_word(0x03c0), m_panel_b_last_parser_pc, m_panel_c_rx_valid ? 1 : 0);
			if (lowmem_byte(0x03bc) != 0)
			{
				panel_l_abort("final_idle_f89a9a_entry_count_not_zero", pc, lowmem_byte(0x03bc));
				return;
			}
			m_panel_l_final_idle_f89a9a_zero_seen = true;
		}
		else if (pc == 0x00f89ace && !m_panel_l_final_idle_f89ace_seen)
		{
			m_panel_l_final_idle_f89ace_seen = true;
			logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=final_idle_f89ace_reached "
				"pc=%06x previous_pc=%06x count_03bc=%02x idle_03c5_before_clear=%02x "
				"f89a9a_zero_seen=%u parser_state_03c0=%04x\n",
				pc, m_last_distinct_pc, lowmem_byte(0x03bc), lowmem_byte(0x03c5),
				m_panel_l_final_idle_f89a9a_zero_seen ? 1 : 0, lowmem_word(0x03c0));
		}
	}
	if (m_panel_e_ff_drain_known_ring_enabled && m_panel_e_ff_drain_done && !machine().side_effects_disabled())
	{
		if (pc == 0x00f89abe && !m_panel_e_f89abe_logged)
		{
			m_panel_e_f89abe_logged = true;
			logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=f89abe_reached "
				"pc=%06x previous_pc=%06x count_03bc=%02x value_00d8=%04x "
				"slot0_state=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
				"node_14f4_00=%04x node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x\n",
				pc, m_last_distinct_pc, lowmem_byte(0x03bc), lowmem_word(0x00d8),
				lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6),
				lowmem_word(0x14f4), lowmem_word(0x14f6), lowmem_word(0x14f8),
				lowmem_word(0x14fa));
		}
		else if (pc == 0x00f89ac2 && !m_panel_e_f89ac2_logged)
		{
			m_panel_e_f89ac2_logged = true;
			logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_KNOWN_RING event=f89ac2_reached "
				"pc=%06x previous_pc=%06x count_03bc=%02x value_00d8=%04x "
				"slot0_state_before=%04x slot0_queue_head=%04x slot0_queue_tail=%04x "
				"node_14f4_type=%04x\n",
				pc, m_last_distinct_pc, lowmem_byte(0x03bc), lowmem_word(0x00d8),
				lowmem_word(0x23d6), lowmem_word(0x23e4), lowmem_word(0x23e6),
				lowmem_word(0x14f6));
		}
	}
	if (m_panel_l_ff_drain_later_rings_enabled && m_panel_l_later_started &&
		!m_panel_l_aborted && !m_panel_l_done && !machine().side_effects_disabled() &&
		(u64(m_maincpu->total_cycles()) - m_panel_l_later_start_cycle) > ASR10_PANEL_L_MAX_CYCLES_AFTER_LATER_START)
	{
		panel_l_abort("cycle_safety_limit_after_later_start", pc);
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
		if (m_panel_l_ff_drain_later_rings_enabled && m_panel_l_later_started && !machine().side_effects_disabled())
		{
			if (pc == 0x00f87f92 || pc == 0x00f87f96 || pc == 0x00f87fc0)
			{
				const u32 a2 = u32(m_maincpu->state_int(M68K_A2)) & 0x00ffffff;
				logerror("ASR10_EXPERIMENT_PANEL_FF_DRAIN_LATER_RINGS event=dispatcher_scan "
					"pc=%06x semantic=%s zero_index=%u a2=%06x slot0_state=%04x "
					"slot0_queue_head=%04x slot0_queue_tail=%04x node_14f4_00=%04x "
					"node_14f4_02=%04x node_14f4_04=%04x node_14f4_06=%04x "
					"restored_continuation_pc=%06x stop_after_dispatch=%u\n",
					pc, semantic, m_panel_l_zero_crossing_count, a2, lowmem_word(0x23d6),
					lowmem_word(0x23e4), lowmem_word(0x23e6), lowmem_word(0x14f4),
					lowmem_word(0x14f6), lowmem_word(0x14f8), lowmem_word(0x14fa),
					m_dispatcher_rte_frame_pc, m_panel_l_stop_after_dispatch ? 1 : 0);
				if (m_panel_l_stop_after_dispatch && a2 == 0x0023d4)
					panel_l_stop("successful_later_slot0_wake_and_dispatch", pc);
			}
		}
	}
	log_dispatcher_rte_first_pc_probe(pc);
	log_dispatcher_rte_candidate_pc(pc);
	log_tuning_stall_save_probe(pc);
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
		log_watched_pc(pc);
		log_duart_counter_watched_pc(pc);
		log_primary_slot_snapshot_once(pc);
		log_timer_secondary_callback(pc);
		log_divzero_exception_frame(pc);
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
		log_pc_summary("max_poll_count", pc);
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
	m_maincpu->set_rte_callback(FUNC(asr10_boot_state::log_f87f96_queue_rte));

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
	// exists.
	SCN2681(config, m_duart, XTAL(16'000'000) / 4);

	// Phase 1 host-port fingerprint experiment (ASR10_EXPERIMENT_ES5506_HOST).
	// Flagged premise, not board-proven: see
	// docs/asr10/es5506-chain-verification.md. Instantiated only when the
	// env var is set, so the baseline (flag absent) build/run is
	// byte-for-byte identical to before this device existed.
	const char *const es5506_host_env = std::getenv("ASR10_EXPERIMENT_ES5506_HOST");
	if (es5506_host_env && es5506_host_env[0] && es5506_host_env[0] != '0')
	{
		// Provisional/uncalibrated: no ASR-10-specific clock citation exists
		// for this chip in any driver; es550x_device::device_start() divides
		// by clock() to compute m_sample_rate, so a nonzero clock is required
		// simply to construct the device.
		es5506_device &es5506_host(ES5506(config, m_es5506_host, XTAL(16'000'000)));

		// Narrowly-gated diagnostic PAR test (ASR10_EXPERIMENT_PAR_DIAGNOSTIC=1
		// + ASR10_DIAG_PAR_VALUE=<n>): binds a single fixed diagnostic PAR
		// value so the DIVU's downstream propagation can be observed. NOT
		// an analog model, NOT a claim that any injected value is a real
		// resting value -- see es5506_host_read_par_diag(). Both env vars
		// are required; either absent leaves read_port_cb unbound (baseline
		// ERROR 130 / PAR=0 path, matching Phase 1).
		const char *const par_diagnostic_env = std::getenv("ASR10_EXPERIMENT_PAR_DIAGNOSTIC");
		const bool par_diagnostic_enabled =
			par_diagnostic_env && par_diagnostic_env[0] && par_diagnostic_env[0] != '0';
		const char *const par_value_env = std::getenv("ASR10_DIAG_PAR_VALUE");
		if (par_diagnostic_enabled && par_value_env && par_value_env[0])
			es5506_host.read_port_cb().set(FUNC(asr10_boot_state::es5506_host_read_par_diag));
		// read_port_cb left unbound otherwise.
	}

	// ASR10_EXPERIMENT_ES5510_HOST (filesystem-browser-map.md 4.24):
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
	const char *const es5510_host_env = std::getenv("ASR10_EXPERIMENT_ES5510_HOST");
	if (es5510_host_env && es5510_host_env[0] && es5510_host_env[0] != '0')
	{
		// Provisional/uncalibrated clock: 10MHz matches the real
		// ASR-10/ESQ-1-family precedent (esqasr.cpp and esq5505.cpp both
		// use XTAL(10'000'000) / 10_MHz_XTAL for this exact chip); not
		// derived from ASR-10 schematics this round, and irrelevant to
		// host_r()/host_w() correctness since the device never executes.
		es5510_device &es5510_host(ES5510(config, m_es5510_host, XTAL(10'000'000)));
		es5510_host.set_disable();
	}

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
