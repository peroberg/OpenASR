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
#include "sound/es5506.h"
#include "formats/hxchfe_dsk.h"

#include "asr10_boot.lh"

#include <algorithm>
#include <array>
#include <cstdlib>

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
		, m_es5506_host(*this, "es5506_host")
	        , m_display(*this, "digit%u", 0U)
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
	static constexpr bool ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3 = true;
	static constexpr u8 ASR10_EXPERIMENT_68302_LRCLK_CLOCK_PHASE_READS = 8;
	static constexpr u8 ASR10_EXPERIMENT_68302_LRCLK_CLOCK_MAX_LOGS = 64;
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
	static u16 ascii_to_14seg(u8 character);

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
	optional_device<es5506_device> m_es5506_host;

	output_finder<ASR10_DISPLAY_LENGTH> m_display;
	std::array<u8, ASR10_DISPLAY_LENGTH> m_display_chars{};
	u8 m_display_position = 0;

	emu_timer *m_pc_timer = nullptr;
	emu_timer *m_prompt_select_timer = nullptr;
	emu_timer *m_synth_68302_timer_irq_timer = nullptr;
	emu_timer *m_panel_autorespond_timer = nullptr;
	emu_timer *m_duart_counter_timer = nullptr;
	memory_passthrough_handler m_duart_counter_boundary_tap;
	memory_passthrough_handler m_hook_f8834a_tap;
	memory_passthrough_handler m_hook_f88352_tap;
	memory_passthrough_handler m_hook_006800_tap;
	memory_passthrough_handler m_hook_00680a_tap;
	bool m_hook_006800_dump_logged = false;
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
	u32 m_fc2d40_cluster_count = 0;
	u32 m_fc3000_cluster_count = 0;
	std::unique_ptr<u16[]> m_lowmem_shadow;
	u16 m_probe_or_alias_region_shadow[PROBE_OR_ALIAS_REGION_COUNT][2]{};
	u16 m_m68302_internal_shadow[0x80]{};
	u8 m_fc6860_reads_after_write = 0;
	bool m_fc6860_busy_clear_logged = false;
	u32 m_lrclk_clock_reads = 0;
	u8 m_lrclk_clock_transition_logs = 0;
	s8 m_lrclk_clock_last_bit = -1;
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
	bool m_fdc_synth_tc_enabled = false;
	bool m_fdc_synth_tc_pulsed_this_txn = false;
	bool m_disk_sig_trace_enabled = false;
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
	bool m_high_alias_enabled = false;
	bool m_lowmem_overlay_enabled = false;
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
	bool m_duart_counter_running = false;
	u8 m_duart_ctu_preload = 0;
	u8 m_duart_ctl_preload = 0;
	u32 m_duart_counter_start_count = 0;
	u32 m_duart_counter_fire_count = 0;
	u32 m_duart_counter_stop_count = 0;
	u8 m_duart_acr = 0;
	bool m_divzero_frame_logged = false;
	bool m_es5506_host_enabled = false;
	std::array<u8, 64> m_es5506_host_seen_mask{}; // bit0=read seen, bit1=write seen, per device offset
	u32 m_es5506_host_access_count = 0;
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
	TIMER_CALLBACK_MEMBER(synth_68302_timer_irq);
	TIMER_CALLBACK_MEMBER(panel_autorespond_fire);
	TIMER_CALLBACK_MEMBER(duart_counter_terminal_count);
	u8 maincpu_iack_r(u8 level);

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
	void log_fc6829_port_b_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 shadow);
	void log_68302_gpio_stage1(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow);
	void log_post_lrclk_poll_candidate(u32 address, u16 data, u16 mem_mask, u16 shadow);
	void log_loaded_0067_window_candidate(u32 pc);
	void log_fc681x_interrupt_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow);
	void log_fc6816_service_setter_context(u32 pc, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow);
	void log_fc688x_service_context(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow);
	void log_timer_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow);
	void log_lowmem_service_context(bool write, u32 byte_address, u16 previous, u16 current, u16 data, u16 mem_mask);
	void log_synth_68302_irq_vectors(u8 irq_level, u32 pc, u16 sr);
	void log_runtime_vector_table_for_iack_experiment(u32 pc, u16 sr);
	void dump_loaded_code_range(const char *tag, u32 start, u32 end);
	void scan_for_ascii_string(const char *tag, u32 start, u32 end, const char *needle);
	void log_dispatcher_rte_first_pc_probe(u32 pc);
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
	void duart_counter_start(u32 pc);
	void duart_counter_stop(u32 pc);
	void duart_counter_arm_periodic(u32 pc, const char *event);
	void duart_counter_check_implicit_start(u32 pc);
	std::string dump_cpu_registers() const;
	u16 es5506_host_read_par_diag();
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
	static const char *address_region_guess(u32 address);
	static const char *trace_detail(trace_region region, u32 address);
	static const char *region_name(trace_region region);
	static const char *m68302_register_name(u32 address);
	static const char *fdc_state_field_name(u32 address);
	static bool is_fdc_state_field(u32 address);
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
	m_duart_counter_timer = timer_alloc(FUNC(asr10_boot_state::duart_counter_terminal_count), this);
	// Temporary diagnostic: the address immediately past the mapped DUART
	// block (0xfc4820) is backed by plain .ram() with no logging. Tap it
	// read-only (no data/behavior change) so a START/STOP command issued
	// one word beyond our assumed mapping is still observable.
	m_duart_counter_boundary_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00fc4820, 0x00fc4821, "duart_counter_boundary_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_DUART_COUNTER event=raw_read pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x accessing_bits_0_7=%u accessing_bits_8_15=%u note=boundary_ram_region\n",
				pc, 0x00fc4820 + offset * 2, offset, mem_mask,
				ACCESSING_BITS_0_7 ? 1 : 0, ACCESSING_BITS_8_15 ? 1 : 0);
		});
	// Temporary, precise (non-periodic) call-chain hooks: opcode fetches are
	// ordinary reads through AS_PROGRAM for this driver (no separate decrypted
	// opcode space is mapped), so a narrow read tap over just the target
	// instruction's first word fires exactly on that instruction's fetch.
	// Each callback re-checks pc == target before logging, since the same
	// tap could in principle also see a coincidental data access.
	m_hook_f8834a_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00f8834a, 0x00f8834b, "hook_f8834a_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc != 0x00f8834a)
				return;
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			logerror("ASR10_CALLCHAIN_HOOK event=f8834a_load_callback_ptr pc=%06x sp=%06x %s "
				"fire_count=%u\n",
				pc, sp, dump_cpu_registers().c_str(), m_duart_counter_fire_count);
		});
	m_hook_f88352_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00f88352, 0x00f88353, "hook_f88352_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc != 0x00f88352)
				return;
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			const u32 entry_base = m_maincpu->state_int(M68K_A0) & 0x00ffffff;
			const u32 callback_ptr = m_maincpu->state_int(M68K_A1) & 0x00ffffff;
			logerror("ASR10_CALLCHAIN_HOOK event=f88352_jsr_a1 pc=%06x sp=%06x stack_top=%06x "
				"entry_base=%06x entry_plus14=%04x callback_ptr=%06x %s fire_count=%u\n",
				pc, sp, read_stack_long(sp), entry_base, lowmem_word(entry_base + 0x14),
				callback_ptr, dump_cpu_registers().c_str(), m_duart_counter_fire_count);
		});
	m_hook_006800_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x00006800, 0x00006801, "hook_006800_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc != 0x00006800)
				return;
			const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
			const u32 return_address = read_stack_long(sp);
			logerror("ASR10_CALLCHAIN_HOOK event=006800_entry pc=%06x sp=%06x return_address=%06x "
				"%s fire_count=%u\n",
				pc, sp, return_address, dump_cpu_registers().c_str(), m_duart_counter_fire_count);
			if (!m_hook_006800_dump_logged)
			{
				m_hook_006800_dump_logged = true;
				std::string hex;
				for (u32 addr = 0x006800; addr < 0x006800 + 96; addr++)
					hex += util::string_format("%02x", lowmem_byte(addr));
				logerror("ASR10_CALLCHAIN_HOOK event=006800_forward_dump start=006800 len=96 hex=%s\n",
					hex.c_str());
			}
		});
	m_hook_00680a_tap = m_maincpu->space(AS_PROGRAM).install_read_tap(
		0x0000680a, 0x0000680b, "hook_00680a_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			if (pc != 0x0000680a)
				return;
			logerror("ASR10_CALLCHAIN_HOOK event=00680a_pre_divu pc=%06x %s "
				"tick_0b82=%02x flag_03c5=%02x word_03bc=%04x fire_count=%u\n",
				pc, dump_cpu_registers().c_str(),
				lowmem_byte(0x0b82), lowmem_byte(0x03c5), lowmem_word(0x03bc), m_duart_counter_fire_count);
		});
	// FC2068-FC206F: unlike the four opcode-fetch taps above (which never
	// fire -- opcode fetch on this core goes through a cache-typed fast
	// path that bypasses tap dispatch entirely), these are genuine DATA
	// reads performed by the movep.l instruction at FC60B0, which uses the
	// dispatch-backed accessor. FC2068/6A/6C/6E fall in the plain .ram()
	// block (0xfc0000-0xfc3fff); no existing handler/diagnostic covers
	// them, so this is the narrowest possible addition for that purpose.
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
				"mem_mask=%04x data=%04x accessing_bits_0_7=%u accessing_bits_8_15=%u fire_count=%u\n",
				pc, address, offset, mem_mask, data,
				ACCESSING_BITS_0_7 ? 1 : 0, ACCESSING_BITS_8_15 ? 1 : 0, m_duart_counter_fire_count);
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
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			if (m_fc3000_cluster_count >= 32)
				return;
			m_fc3000_cluster_count++;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc3000_read pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, 0x00fc3000 + offset, offset, mem_mask, data, m_fc3000_cluster_count);
		});
	m_hook_fc3000_write_tap = m_maincpu->space(AS_PROGRAM).install_write_tap(
		0x00fc3000, 0x00fc31ff, "hook_fc3000_write_tap",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
				return;
			if (m_fc3000_cluster_count >= 32)
				return;
			m_fc3000_cluster_count++;
			const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
			logerror("ASR10_CLUSTER_TRACE event=fc3000_write pc=%06x address=%06x offset=%04x "
				"mem_mask=%04x data=%04x cluster_count=%u\n",
				pc, 0x00fc3000 + offset, offset, mem_mask, data, m_fc3000_cluster_count);
		});
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
	save_item(NAME(m_fc6860_reads_after_write));
	save_item(NAME(m_fc6860_busy_clear_logged));
	save_item(NAME(m_lrclk_clock_reads));
	save_item(NAME(m_lrclk_clock_transition_logs));
	save_item(NAME(m_lrclk_clock_last_bit));
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
	save_item(NAME(m_high_alias_enabled));
	save_item(NAME(m_lowmem_overlay_enabled));
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
	save_item(NAME(m_duart_counter_running));
	save_item(NAME(m_duart_ctu_preload));
	save_item(NAME(m_duart_ctl_preload));
	save_item(NAME(m_duart_counter_start_count));
	save_item(NAME(m_duart_counter_fire_count));
	save_item(NAME(m_duart_counter_stop_count));
	save_item(NAME(m_duart_acr));
	save_item(NAME(m_divzero_frame_logged));
	m_es5506_host_enabled = m_es5506_host.found();
	save_item(NAME(m_es5506_host_enabled));
	save_item(NAME(m_es5506_host_seen_mask));
	save_item(NAME(m_es5506_host_access_count));
	save_item(NAME(m_es5506_diag_par_enabled));
	save_item(NAME(m_es5506_diag_par_value));
	save_item(NAME(m_es5506_diag_par_read_count));
	save_item(NAME(m_primary_slot_snapshot_logged));
	save_item(NAME(m_hook_006800_dump_logged));
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
	m_high_alias_enabled = false;
	m_lowmem_overlay_enabled = false;
	m_seen_insert_disk_prompt = false;

	m_panel_text_length = 0;
	m_display_chars.fill(' ');
	m_display_position = 0;

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
	m_duart_counter_running = false;
	m_duart_ctu_preload = 0;
	m_duart_ctl_preload = 0;
	m_duart_counter_start_count = 0;
	m_duart_counter_fire_count = 0;
	m_duart_counter_stop_count = 0;
	m_duart_acr = 0;
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
	m_hook_006800_dump_logged = false;
	m_fc60b0_verified = false;
	m_fc2d40_cluster_count = 0;
	m_fc3000_cluster_count = 0;
	m_duart_counter_timer->adjust(attotime::never);
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
	m_fc6860_reads_after_write = 0;
	m_fc6860_busy_clear_logged = false;
	m_lrclk_clock_reads = 0;
	m_lrclk_clock_transition_logs = 0;
	m_lrclk_clock_last_bit = -1;
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
	logerror("ASR10_MAINCPU_INPUT_LINE_DRIVER source=harness default_set_input_line_calls=0 "
		"iack_map=installed_returns_autovectors_by_default pc_timer=diagnostic_poll "
		"prompt_select_timer=diagnostic_poll fc6850_fc6852_timer_binding=none\n");
}

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
u16 asr10_boot_state::ascii_to_14seg(u8 character)
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
	map(0x280000, 0x28001f).rw(FUNC(asr10_boot_state::duart_vfx_candidate_r), FUNC(asr10_boot_state::duart_vfx_candidate_w));
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
			map(0xfc2080, 0xfc3fff).ram();
		}
		else
		{
			map(0xfc0000, 0xfc3fff).ram();
		}
	}
	map(0xfc4000, 0xfc4003).rw(FUNC(asr10_boot_state::upd72069_fdc_r), FUNC(asr10_boot_state::upd72069_fdc_w));
	map(0xfc4004, 0xfc47ff).ram();
	map(0xfc4800, 0xfc481f).rw(FUNC(asr10_boot_state::duart_panel_asr_candidate_r), FUNC(asr10_boot_state::duart_panel_asr_candidate_w));
	map(0xfc4820, 0xfc4fff).ram();
	map(0xfc5000, 0xfc501f).rw(FUNC(asr10_boot_state::scsi_asr_candidate_r), FUNC(asr10_boot_state::scsi_asr_candidate_w));
	map(0xfc5020, 0xfc67ff).ram();
	map(0xfc6800, 0xfc68ff).rw(FUNC(asr10_boot_state::m68302_internal_r), FUNC(asr10_boot_state::m68302_internal_w));
	map(0xfc6900, 0xffffff).ram();
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
		if (!machine().side_effects_disabled())
			log_lowmem_service_context(false, byte_address, m_lowmem_shadow[offset], m_lowmem_shadow[offset], data, mem_mask);
		if (!machine().side_effects_disabled())
			log_f87f96_queue_read(byte_address, data, mem_mask);
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
		"read_count=%u caller_pc=%06x fire_count=%u\n",
		m_es5506_diag_par_value, m_es5506_diag_par_read_count, pc, m_duart_counter_fire_count);
	return m_es5506_diag_par_value;
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
		"tick_0b82=%02x flag_03c5=%02x word_03bc=%04x fire_count=%u\n",
		pc, sp, stacked_sr, stacked_pc,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_D4)), u32(m_maincpu->state_int(M68K_D5)),
		u32(m_maincpu->state_int(M68K_D6)), u32(m_maincpu->state_int(M68K_D7)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u32(m_maincpu->state_int(M68K_A4)), u32(m_maincpu->state_int(M68K_A5)),
		u32(m_maincpu->state_int(M68K_A6)),
		lowmem_byte(0x0b82), lowmem_byte(0x03c5), lowmem_word(0x03bc), m_duart_counter_fire_count);

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
		"entry_plus14_countdown=%04x callback_ptr=%06x tick_0b82=%02x fire_count=%u caller_pc=%06x\n",
		pc, entry_base, lowmem_word(entry_base + 0x14), callback_ptr,
		lowmem_byte(0x0b82), m_duart_counter_fire_count, m_last_distinct_pc);
}


void asr10_boot_state::duart_counter_arm_periodic(u32 pc, const char *event)
{
	// Provisional/uncalibrated: the board has no 3.6864 MHz crystal (Y1=16MHz,
	// Y2=30.476MHz, Y3=33.8688MHz), so the DUART X1 rate is unknown and likely
	// derived. Period accuracy is not the point of this experiment -- only
	// whether ISR bit 3 delivery unlocks progress. Documented SCC2681 timer
	// period is 2*N/X1; N = CTUR:CTLR.
	constexpr double PROVISIONAL_X1_HZ = 3686400.0;
	const u16 preload = u16((u16(m_duart_ctu_preload) << 8) | m_duart_ctl_preload);
	const u32 n = preload ? preload : 1;
	const double period_seconds = (2.0 * double(n)) / PROVISIONAL_X1_HZ;
	const attotime period = attotime::from_double(period_seconds);
	const bool imr_bit3 = BIT(m_panel_c_imr, 3);
	m_duart_counter_running = true;
	m_duart_counter_timer->adjust(period, 0, period);
	logerror("ASR10_DUART_COUNTER event=%s pc=%06x preload=%04x acr=%02x timer_mode=%u "
		"period_seconds=%f imr=%02x imr_bit3=%u model=provisional_x1_3686400hz_period_2n_over_x1\n",
		event, pc, preload, m_duart_acr, (m_duart_acr & 0x70) == 0x60 ? 1 : 0,
		period_seconds, m_panel_c_imr, imr_bit3 ? 1 : 0);
	if (!imr_bit3)
		logerror("ASR10_DUART_COUNTER event=START_WHILE_MASKED pc=%06x imr=%02x\n", pc, m_panel_c_imr);
}


void asr10_boot_state::duart_counter_check_implicit_start(u32 pc)
{
	// AN414-style interpretation: under this experiment only, treat the
	// SCN2681 timer as already free-running once ACR selects TIMER mode
	// (ACR[6:4]==0b110, X1 clock) and a nonzero CTUR:CTLR preload exists.
	// This is a modeling choice, not an observed firmware START read --
	// always logged as implicit_scn2681_timer_start, never as event=START.
	if (!m_duart_counter_timer_enabled || machine().side_effects_disabled() || m_duart_counter_running)
		return;
	if ((m_duart_acr & 0x70) != 0x60)
		return;
	const u16 preload = u16((u16(m_duart_ctu_preload) << 8) | m_duart_ctl_preload);
	if (preload == 0)
		return;
	duart_counter_arm_periodic(pc, "implicit_scn2681_timer_start");
}


void asr10_boot_state::duart_counter_start(u32 pc)
{
	if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
		return;

	// Explicit physical START read (FC481D): reload/resynchronize the
	// current cycle. Reuses the same periodic m_duart_counter_timer --
	// never allocates a second one, whether or not it was already running
	// (e.g. via the implicit reset-running model above).
	m_duart_counter_start_count++;
	duart_counter_arm_periodic(pc, "START");
}


void asr10_boot_state::duart_counter_stop(u32 pc)
{
	if (!m_duart_counter_timer_enabled || machine().side_effects_disabled())
		return;

	// Explicit physical STOP read (FC481F): acknowledges/clears ISR bit 3
	// and re-evaluates IRQ6. Per the corrected TIMER-mode model, this does
	// NOT cancel or suspend the periodic timer -- in TIMER mode the C/T
	// free-runs; STOP only silences the pending interrupt.
	m_duart_counter_stop_count++;
	const u8 previous_isr = m_panel_c_isr;
	m_panel_c_isr &= ~0x08;
	logerror("ASR10_DUART_COUNTER event=STOP pc=%06x running=%u previous_isr=%02x current_isr=%02x "
		"stop_count=%u\n",
		pc, m_duart_counter_running ? 1 : 0, previous_isr, m_panel_c_isr, m_duart_counter_stop_count);
	panel_c_update_irq6("duart_counter_stop", pc);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::duart_counter_terminal_count)
{
	if (!m_duart_counter_timer_enabled || !m_duart_counter_running)
		return;

	m_duart_counter_fire_count++;
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_panel_c_isr |= 0x08; // bit 3 only; bit 5 (RxRDYB) is preserved untouched
	const bool verbose = m_duart_counter_fire_count <= 5 || (m_duart_counter_fire_count % 100) == 0;
	if (verbose)
		logerror("ASR10_DUART_COUNTER event=terminal_count pc=%06x isr=%02x imr=%02x active=%02x "
			"rx_active=%02x counter_active=%02x fire_count=%u\n",
			pc, m_panel_c_isr, m_panel_c_imr, m_panel_c_isr & m_panel_c_imr,
			m_panel_c_isr & m_panel_c_imr & 0x20, m_panel_c_isr & m_panel_c_imr & 0x08,
			m_duart_counter_fire_count);
	panel_c_update_irq6("duart_counter_terminal_count", pc);
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
			"d0=%08x d1=%08x d2=%08x fire_count=%u\n",
			byte_address == 0x0dd6 ? "store_0dd6_rate_param" : "store_0df2_divider_result",
			pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
			u32(m_maincpu->state_int(M68K_D2)), m_duart_counter_fire_count);
	}
	if (m_download_trace_enabled &&
		(byte_address == 0x0e7e || byte_address == 0x0e89 || byte_address == 0x0e9d || byte_address == 0x0e8c) &&
		!machine().side_effects_disabled())
	{
		const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
		const char *const field =
			byte_address == 0x0e7e ? "table_source_pointer_a3" :
			byte_address == 0x0e89 ? "first_record_byte" :
			byte_address == 0x0e8c ? "record_scratch" : "retry_counter";
		logerror("ASR10_TASK3_DOWNLOAD_TRACE event=lowmem_store field=%s pc=%06x address=%06x "
			"previous=%04x new=%04x mem_mask=%04x d3=%08x a3=%08x\n",
			field, pc, byte_address, previous, m_lowmem_shadow[offset], mem_mask,
			u32(m_maincpu->state_int(M68K_D3)), u32(m_maincpu->state_int(M68K_A3)));
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
				}
				if (ACCESSING_BITS_0_7 && byte_address + 1 >= 0x0378 && byte_address + 1 <= 0x03b7)
				{
					m_panel_b_last_ring_write_valid = true;
					m_panel_b_last_ring_write_address = byte_address + 1;
					m_panel_b_last_ring_write_byte = u8(m_lowmem_shadow[offset]);
					m_panel_b_last_ring_write_pc = pc;
				}
			}
			if (byte_address == 0x03bc && ACCESSING_BITS_8_15)
			{
				if (pc == 0x00f89a8a)
				{
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
	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
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
				logerror("ASR10_EXPERIMENT_FC6860_CLEAR_BUSY bit0 1->0 original=%02x effective=%02x pc=%06x\n",
					original_byte, effective_byte, pc);
				m_fc6860_busy_clear_logged = true;
			}
		}
	}
	if (ASR10_EXPERIMENT_68302_LRCLK_CLOCK_BIT3 && address == 0x00fc6828 && mem_mask == 0x00ff &&
		!machine().side_effects_disabled())
	{
		const u8 original_byte = u8(effective);
		const u32 phase = m_lrclk_clock_reads / ASR10_EXPERIMENT_68302_LRCLK_CLOCK_PHASE_READS;
		const u8 bit3 = phase & 1;
		const u8 effective_byte = bit3 ? (original_byte | 0x08) : (original_byte & ~u8(0x08));
		if (m_lrclk_clock_last_bit != s8(bit3) &&
			m_lrclk_clock_transition_logs < ASR10_EXPERIMENT_68302_LRCLK_CLOCK_MAX_LOGS)
		{
			logerror("ASR10_EXPERIMENT_LRCLK_CLOCK pc=%06x original=%02x effective=%02x bit3=%u phase=%u\n",
				pc, original_byte, effective_byte, bit3, phase);
			m_lrclk_clock_transition_logs++;
		}
		m_lrclk_clock_last_bit = s8(bit3);
		m_lrclk_clock_reads++;
		effective = (effective & 0xff00) | effective_byte;
	}
	const u16 data = effective & mem_mask;
	m_last_fc68_pc = pc;
	m_last_fc68_address = address;
	m_last_fc68_data = data;
	m_last_fc68_mem_mask = mem_mask;
	m_last_fc68_shadow = shadow;
	m_last_fc68_write = false;
	trace_access(trace_region::M68302_INTERNAL, false, address, data, mem_mask, shadow);
	log_lrclk_candidate(false, address, data, mem_mask, shadow);
	log_fc6829_port_b_candidate(false, address, data, mem_mask, shadow);
	log_68302_gpio_stage1(false, address, data, mem_mask, shadow, shadow);
	log_post_lrclk_poll_candidate(address, data, mem_mask, shadow);
	log_loaded_0067_window_candidate(pc);
	log_fc681x_interrupt_candidate(false, address, data, mem_mask, shadow, shadow);
	log_fc688x_service_context(false, address, data, mem_mask, shadow, shadow);
	log_timer_candidate(false, address, data, mem_mask, shadow, shadow);
	if (address == 0x00fc6860 && !machine().side_effects_disabled())
	{
		const u8 relevant_byte = (mem_mask & 0xff00) ? u8(effective >> 8) : u8(effective);
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
	const u16 previous = m_m68302_internal_shadow[offset & 0x7f];
	COMBINE_DATA(&m_m68302_internal_shadow[offset & 0x7f]);
	m_last_fc68_pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	m_last_fc68_address = address;
	m_last_fc68_data = data;
	m_last_fc68_mem_mask = mem_mask;
	m_last_fc68_shadow = m_m68302_internal_shadow[offset & 0x7f];
	m_last_fc68_write = true;
	trace_access(trace_region::M68302_INTERNAL, true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	log_lrclk_candidate(true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	log_fc6829_port_b_candidate(true, address, data, mem_mask, m_m68302_internal_shadow[offset & 0x7f]);
	log_68302_gpio_stage1(true, address, data, mem_mask, previous, m_m68302_internal_shadow[offset & 0x7f]);
	log_loaded_0067_window_candidate(m_last_fc68_pc);
	log_fc681x_interrupt_candidate(true, address, data, mem_mask, previous, m_m68302_internal_shadow[offset & 0x7f]);
	log_fc688x_service_context(true, address, data, mem_mask, previous, m_m68302_internal_shadow[offset & 0x7f]);
	log_timer_candidate(true, address, data, mem_mask, previous, m_m68302_internal_shadow[offset & 0x7f]);
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

	if (m_duart_counter_timer_enabled && !machine().side_effects_disabled() &&
		address >= 0x00fc4818 && address <= 0x00fc481e)
	{
		logerror("ASR10_DUART_COUNTER event=raw_read pc=%06x address=%06x offset=%04x mem_mask=%04x "
			"accessing_bits_0_7=%u accessing_bits_8_15=%u\n",
			pc, address, offset, mem_mask, ACCESSING_BITS_0_7 ? 1 : 0, ACCESSING_BITS_8_15 ? 1 : 0);
	}
	if (m_duart_counter_timer_enabled && !machine().side_effects_disabled() && ACCESSING_BITS_0_7)
	{
		if (address == 0x00fc481c)
			duart_counter_start(pc);
		else if (address == 0x00fc481e)
			duart_counter_stop(pc);
	}

	// Channel B carries panel traffic on VFX-SD/SD-1 and TS-10. The ASR ROM
	// writes the channel-B TX buffer at +0x17, then polls bit 0 at +0x13.
	// Report receive-ready to expose the next boot dependency in the default harness.
	if (address == 0x00fc4812 && ACCESSING_BITS_0_7 && (!panel_reply_experiment_enabled() || !m_panel_c_rx_valid))
		m_duart_panel_asr_shadow[word] = 0x0001;

	u16 raw_data = (address == 0x00fc4816) ? 0 : m_duart_panel_asr_shadow[word];
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
				panel_reply_experiment_enabled() ? m_panel_c_srb : u8(m_duart_panel_asr_shadow[0x12 >> 1]),
				panel_reply_experiment_enabled() ? m_panel_c_isr : u8(m_duart_panel_asr_shadow[0x0a >> 1]));
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
	if (m_duart_counter_timer_enabled && !machine().side_effects_disabled() && ACCESSING_BITS_0_7)
	{
		if (address == 0x00fc4809)
		{
			m_duart_acr = u8(data);
			logerror("ASR10_DUART_COUNTER event=acr_write pc=%06x value=%02x timer_mode=%u\n",
				pc, m_duart_acr, (m_duart_acr & 0x70) == 0x60 ? 1 : 0);
			duart_counter_check_implicit_start(pc);
		}
		else if (address == 0x00fc480d)
		{
			m_duart_ctu_preload = u8(data);
			logerror("ASR10_DUART_COUNTER event=preload_ctu pc=%06x value=%02x combined_preload=%04x\n",
				pc, m_duart_ctu_preload, u16((u16(m_duart_ctu_preload) << 8) | m_duart_ctl_preload));
			duart_counter_check_implicit_start(pc);
		}
		else if (address == 0x00fc480f)
		{
			m_duart_ctl_preload = u8(data);
			logerror("ASR10_DUART_COUNTER event=preload_ctl pc=%06x value=%02x combined_preload=%04x\n",
				pc, m_duart_ctl_preload, u16((u16(m_duart_ctu_preload) << 8) | m_duart_ctl_preload));
			duart_counter_check_implicit_start(pc);
		}
	}
	if (address == 0x00fc4817 && ACCESSING_BITS_0_7)
	{
		const u8 character = u8(data);
		log_panel_b_thrb(pc, character);
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
		panel_text_byte(character, pc);
	}
	trace_access(trace_region::DUART_PANEL_ASR_CANDIDATE, true, address, data, mem_mask, m_duart_panel_asr_shadow[word]);
}



void asr10_boot_state::panel_text_byte(u8 data, u32 pc)
{
	if (data < 0x20 || data > 0x7e)
	{
		flush_panel_text();
		return;
	}

	// Ny textsekvens: töm den visuella displayen.
	if (m_panel_text_length == 0)
	{
		clear_display();
	}

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


// void asr10_boot_state::panel_text_byte(u8 data, u32 pc)
// {
// 	if (pc != 0x00f89cb0 || data < 0x20 || data > 0x7e)
// 	{
// 		flush_panel_text();
// 		return;
// 	}
//
// 	if (m_panel_text_length == PANEL_TEXT_LENGTH - 1)
// 		flush_panel_text();
//
// 	m_panel_text[m_panel_text_length++] = char(data);
// 	m_panel_text[m_panel_text_length] = 0;
//
// 	if (!m_insert_disk_decision_logged && strstr(m_panel_text, "PLEASE INSERT DISK"))
// 	{
// 		m_insert_disk_decision_logged = true;
// 		log_insert_disk_decision(pc);
// 		if (!m_seen_insert_disk_prompt)
// 		{
// 			m_seen_insert_disk_prompt = true;
// 			m_trace_slots = {};
// 			logerror("ASR10PHASE phase=post_insert_disk_prompt pc=%06x\n", pc);
// 		}
// 	}
// }


void asr10_boot_state::flush_panel_text()
{
	if (m_panel_text_length)
	{
		logerror("ASR10PANEL text=\"%s\"\n", m_panel_text);
		if (strstr(m_panel_text, "LOADING SYSTEM"))
		{
			m_seen_loading_system_prompt = true;
			m_post_loading_panel_write_count = 0;
			m_post_loading_fdc_access_count = 0;
			logerror("ASR10PHASE phase=post_loading_system_panel pc=%06x\n",
				m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff);
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


void asr10_boot_state::log_fc6829_port_b_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 shadow)
{
	if (machine().side_effects_disabled())
		return;
	if (address != 0x00fc6828 || !(mem_mask & 0x00ff))
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u8 byte = u8(data);
	const u8 shadow_byte = u8(shadow);
	const char *semantic = "port_b_data";
	if (pc == 0x000067f6)
		semantic = "port_b_bits0_2_strobe_set_ori_07";
	else if (pc == 0x00006816)
		semantic = "port_b_bits0_2_strobe_clear_andi_f8";
	else if (pc == 0x00f8c14e || pc == 0x00f8c160 || (pc >= 0x0000bfb8 && pc <= 0x0000bfe8))
		semantic = "port_b_bit3_lrclk_candidate";

	m_fc6829_trace_count++;
	if (m_fc6829_trace_count > 128 && (m_fc6829_trace_count & (m_fc6829_trace_count - 1)))
		return;

	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	logerror("ASR10_FC6829_PORTB pc=%06x previous_pc=%06x opcode=%04x rw=%c semantic=%s "
		"data=%04x mem_mask=%04x byte=%02x shadow=%04x shadow_byte=%02x "
		"bit2=%u bit1=%u bit0=%u bits0_2=%u bit3_lrclk_candidate=%u "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x trace_count=%u\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), write ? 'W' : 'R', semantic,
		data, mem_mask, byte, shadow, shadow_byte,
		BIT(byte, 2), BIT(byte, 1), BIT(byte, 0), byte & 0x07, BIT(byte, 3),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), sp, read_stack_long(sp), read_stack_long(sp + 4),
		m_fc6829_trace_count);
}


void asr10_boot_state::log_68302_gpio_stage1(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow)
{
	if (!m_gpio_stage1_trace_enabled || machine().side_effects_disabled())
		return;
	// Corrected MC68302 map (RTEMS m68302.h / MC68302 User's Manual,
	// see docs/asr10/architecture.md): PBCNT=FC6824, PBDDR=FC6826,
	// PBDAT=FC6828 (low byte FC6829 = PB7..PB0). BR0-3/OR0-3 occupy
	// FC6830-FC683e and are NOT Port B (retracts the earlier
	// "Port B may live at FC6834/35" hedge).
	if (address != 0x00fc6824 && address != 0x00fc6826 && address != 0x00fc6828)
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const char *const reg_name = address == 0x00fc6824 ? "PBCNT" :
		address == 0x00fc6826 ? "PBDDR" : "PBDAT";
	const char *const width = ACCESSING_BITS_0_15 ? "word" :
		ACCESSING_BITS_0_7 ? "byte_low" : ACCESSING_BITS_8_15 ? "byte_high" : "none";

	// Current authoritative snapshot of all three registers regardless of
	// which one triggered this access (PBDAT reflects the just-applied
	// new_shadow when this access IS the PBDAT access).
	const u16 pbcnt = m_m68302_internal_shadow[0x24 >> 1];
	const u16 pbddr = m_m68302_internal_shadow[0x26 >> 1];
	const u16 pbdat = (address == 0x00fc6828) ? new_shadow : m_m68302_internal_shadow[0x28 >> 1];
	const u8 pbcnt_low = u8(pbcnt);
	const u8 pbddr_low = u8(pbddr);
	const u8 pbdat_low = u8(pbdat);
	const u32 pb2_0 = pbdat_low & 0x07;

	m_gpio_stage1_trace_count++;

	// Required proof gate: PBCNT bits 0-2 select GPIO (not peripheral
	// IACK7/IACK6/IACK1), and PBDDR bits 0-2 are configured as outputs.
	// Stage 2/3 must not proceed past this without an observed PASS.
	const bool gate_pass = (pbcnt_low & 0x07) == 0x00 && (pbddr_low & 0x07) == 0x07;
	if (gate_pass && !m_gpio_stage1_gate_pass_logged)
	{
		logerror("ASR10_GPIO_STAGE1_GATE result=PASS pbcnt=%02x pbddr=%02x pc=%06x fire_count=%u trace_count=%u\n",
			pbcnt_low, pbddr_low, pc, m_duart_counter_fire_count, m_gpio_stage1_trace_count);
		m_gpio_stage1_gate_pass_logged = true;
	}
	else if (!gate_pass && !m_gpio_stage1_gate_fail_logged && m_gpio_stage1_trace_count >= 32)
	{
		logerror("ASR10_GPIO_STAGE1_GATE result=FAIL pbcnt=%02x pbddr=%02x pc=%06x fire_count=%u "
			"trace_count=%u note=analog_mux_select_hypothesis_unsupported_so_far\n",
			pbcnt_low, pbddr_low, pc, m_duart_counter_fire_count, m_gpio_stage1_trace_count);
		m_gpio_stage1_gate_fail_logged = true;
	}

	const bool verbose = m_gpio_stage1_trace_count <= 256 ||
		!(m_gpio_stage1_trace_count & (m_gpio_stage1_trace_count - 1));
	if (!verbose)
		return;

	logerror("ASR10_GPIO_STAGE1 pc=%06x reg=%s address=%06x rw=%c width=%s mem_mask=%04x "
		"old=%04x new=%04x pbcnt=%02x pbddr=%02x pbdat=%02x pb2=%u pb1=%u pb0=%u pb2_0=%u "
		"pbcnt_bit2_iack7=%u pbcnt_bit1_iack6=%u pbcnt_bit0_iack1=%u "
		"pbddr_bit2_out=%u pbddr_bit1_out=%u pbddr_bit0_out=%u "
		"gate_pass=%u fire_count=%u panel_text_len=%u panel_b_seq=%llu trace_count=%u\n",
		pc, reg_name, address, write ? 'W' : 'R', width, mem_mask,
		old_shadow, new_shadow, pbcnt_low, pbddr_low, pbdat_low,
		BIT(pbdat_low, 2), BIT(pbdat_low, 1), BIT(pbdat_low, 0), pb2_0,
		BIT(pbcnt_low, 2), BIT(pbcnt_low, 1), BIT(pbcnt_low, 0),
		BIT(pbddr_low, 2), BIT(pbddr_low, 1), BIT(pbddr_low, 0),
		gate_pass ? 1u : 0u, m_duart_counter_fire_count, m_panel_text_length,
		(unsigned long long)m_panel_b_seq, m_gpio_stage1_trace_count);
}


void asr10_boot_state::log_post_lrclk_poll_candidate(u32 address, u16 data, u16 mem_mask, u16 shadow)
{
	if (machine().side_effects_disabled())
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (pc < 0x0000bfb8 || pc > 0x0000bfe8 || address != 0x00fc6828 || mem_mask != 0x00ff)
		return;

	if (!m_post_lrclk_disassembly_logged)
	{
		std::string words;
		for (u32 cursor = 0x0000bfb8; cursor <= 0x0000bfe8; cursor += 2)
		{
			if (cursor != 0x0000bfb8)
				words += ',';
			words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_POST_LRCLK_CODE_DUMP words=\"%s\"\n", words.c_str());
		m_post_lrclk_disassembly_logged = true;
	}

	m_post_lrclk_poll_count++;
	if (m_post_lrclk_poll_count > 96 && (m_post_lrclk_poll_count & (m_post_lrclk_poll_count - 1)))
		return;

	const u8 relevant_byte = u8(data);
	const u8 shadow_byte = u8(shadow);
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;

	logerror("ASR10_POST_LRCLK_POLL pc=%06x previous_pc=%06x opcode=%04x "
		"op_m8=%04x op_m6=%04x op_m4=%04x op_m2=%04x op_0=%04x op_p2=%04x op_p4=%04x op_p6=%04x op_p8=%04x "
		"addr=%06x data=%04x mem_mask=%04x relevant_byte=%02x bit3=%u shadow=%04x shadow_relevant_byte=%02x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x stack3=%08x poll_count=%u\n",
		pc, m_last_distinct_pc, read_loaded_word(pc),
		read_loaded_word(pc - 8), read_loaded_word(pc - 6), read_loaded_word(pc - 4),
		read_loaded_word(pc - 2), read_loaded_word(pc), read_loaded_word(pc + 2),
		read_loaded_word(pc + 4), read_loaded_word(pc + 6), read_loaded_word(pc + 8),
		address, data, mem_mask, relevant_byte, BIT(relevant_byte, 3), shadow, shadow_byte,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), sp, read_stack_long(sp), read_stack_long(sp + 4),
		read_stack_long(sp + 8), read_stack_long(sp + 12), m_post_lrclk_poll_count);
}


void asr10_boot_state::log_loaded_0067_window_candidate(u32 pc)
{
	if (machine().side_effects_disabled() || m_fc681x_0067_code_dump_logged)
		return;
	if (pc < 0x000067d0 || pc > 0x00006820)
		return;
	if (read_loaded_word(0x000067f6) == 0x0000)
		return;

	std::string words;
	for (u32 cursor = 0x000067d0; cursor <= 0x00006820; cursor += 2)
	{
		if (cursor != 0x000067d0)
			words += ',';
		words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
	}
	logerror("ASR10_FC681X_CODE_DUMP range=0067d0_006820 trigger_pc=%06x words=\"%s\"\n", pc, words.c_str());
	m_fc681x_0067_code_dump_logged = true;

	// TASK2 investigative addition: also capture 00686e itself (the
	// "OS measurement routine" per subsystems.md) now that its containing
	// RAM window is confirmed loaded.
	std::string words_686e;
	for (u32 cursor = 0x00006840; cursor <= 0x000068c0; cursor += 2)
	{
		if (cursor != 0x00006840)
			words_686e += ',';
		words_686e += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
	}
	logerror("ASR10_TASK2_00686E_DUMP range=006840_0068c0 trigger_pc=%06x words=\"%s\"\n", pc, words_686e.c_str());
}


void asr10_boot_state::log_fc681x_interrupt_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow)
{
	if (machine().side_effects_disabled())
		return;
	if (address != 0x00fc6814 && address != 0x00fc6816 && address != 0x00fc6818)
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	if (!m_fc681x_code_dump_logged)
	{
		std::string rom_words;
		for (u32 cursor = 0x00f87ee0; cursor <= 0x00f87f30; cursor += 2)
		{
			if (cursor != 0x00f87ee0)
				rom_words += ',';
			rom_words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_FC681X_CODE_DUMP range=f87ee0_f87f30 words=\"%s\"\n", rom_words.c_str());
		m_fc681x_code_dump_logged = true;
	}
	if (!m_fc681x_00bf_code_dump_logged && pc >= 0x0000bee0 && pc <= 0x0000bf60 && read_loaded_word(0x0000bf1a) != 0x0000)
	{
		std::string post_lrclk_words;
		for (u32 cursor = 0x0000bee0; cursor <= 0x0000bf60; cursor += 2)
		{
			if (cursor != 0x0000bee0)
				post_lrclk_words += ',';
			post_lrclk_words += util::string_format("%06x:%04x", cursor, read_loaded_word(cursor));
		}
		logerror("ASR10_FC681X_CODE_DUMP range=00bee0_00bf60 trigger_pc=%06x words=\"%s\"\n", pc, post_lrclk_words.c_str());
		m_fc681x_00bf_code_dump_logged = true;
	}

	m_fc681x_trace_count++;
	const u16 changed_bits = old_shadow ^ new_shadow;
	const u16 set_bits = ~old_shadow & new_shadow;
	const u16 cleared_bits = old_shadow & ~new_shadow;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u16 current_sr = u16(m_maincpu->state_int(M68K_SR));
	const u16 int_mask = m_m68302_internal_shadow[0x12 >> 1];
	const u16 int_pending = m_m68302_internal_shadow[0x14 >> 1];
	const u16 int_in_service = m_m68302_internal_shadow[0x16 >> 1];
	const u16 int_control = m_m68302_internal_shadow[0x18 >> 1];

	logerror("ASR10_FC681X pc=%06x previous_pc=%06x opcode=%04x rw=%c addr=%06x detail=%s "
		"data=%04x mem_mask=%04x old_shadow=%04x new_shadow=%04x changed_bits=%04x set_bits=%04x cleared_bits=%04x "
		"bit15=%u bit14=%u bit13=%u bit12=%u bit11=%u bit10=%u bit9=%u bit8=%u "
		"bit7=%u bit6=%u bit5=%u bit4=%u bit3=%u bit2=%u bit1=%u bit0=%u "
		"fc6812_mask=%04x fc6814_pending=%04x fc6816_in_service=%04x fc6818_control=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x sr=%04x sr_mask=%u "
		"sp=%06x stack0=%08x stack1=%08x stack2=%08x stack3=%08x "
		"recent_queue_pc=%06x recent_queue_rw=%c recent_queue_addr=%06x recent_queue_record=%06x recent_queue_slot=%u "
		"recent_queue_data=%04x recent_queue_mem_mask=%04x recent_queue_previous=%04x recent_queue_current=%04x "
		"recent_queue_handler_clear=%u trace_count=%u\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), write ? 'W' : 'R', address, m68302_register_name(address),
		data, mem_mask, old_shadow, new_shadow, changed_bits, set_bits, cleared_bits,
		BIT(new_shadow, 15), BIT(new_shadow, 14), BIT(new_shadow, 13), BIT(new_shadow, 12),
		BIT(new_shadow, 11), BIT(new_shadow, 10), BIT(new_shadow, 9), BIT(new_shadow, 8),
		BIT(new_shadow, 7), BIT(new_shadow, 6), BIT(new_shadow, 5), BIT(new_shadow, 4),
		BIT(new_shadow, 3), BIT(new_shadow, 2), BIT(new_shadow, 1), BIT(new_shadow, 0),
		int_mask, int_pending, int_in_service, int_control,
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		current_sr, (current_sr >> 8) & 7,
		sp, read_stack_long(sp), read_stack_long(sp + 4), read_stack_long(sp + 8), read_stack_long(sp + 12),
		m_recent_queue_pc, m_recent_queue_write ? 'W' : 'R', m_recent_queue_address,
		m_recent_queue_record_base, m_recent_queue_slot, m_recent_queue_data, m_recent_queue_mem_mask,
		m_recent_queue_previous, m_recent_queue_current, m_recent_queue_handler_clear ? 1 : 0,
		m_fc681x_trace_count);

	if (write && address == 0x00fc6816 && (set_bits & 0x2400))
	{
		m_fc6816_service_2400_set_by_runtime = true;
		m_fc6816_service_setter_rte_count = m_f87f96_queue_rte_count;
		m_fc6816_service_setter_pc = pc;
		log_fc6816_service_setter_context(pc, data, mem_mask, old_shadow, new_shadow);
	}
}


void asr10_boot_state::log_fc6816_service_setter_context(u32 pc, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow)
{
	if (machine().side_effects_disabled() || m_fc6816_service_setter_dump_logged)
		return;

	m_fc6816_service_setter_dump_logged = true;
	dump_loaded_code_range("fc6816_service_setter_runtime_00bee0_00bf60", 0x0000bee0, 0x0000bf60);
	dump_loaded_code_range("fc6816_service_clear_rom_f8c0c0_f8c130", 0x00f8c0c0, 0x00f8c130);
	dump_loaded_code_range("fc6818_iack_handlers_f88ee0_f88f60", 0x00f88ee0, 0x00f88f60);

	std::string stack_words;
	std::string stack_longs;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	for (u32 index = 0; index < 16; index++)
	{
		if (index)
		{
			stack_words += ',';
			stack_longs += ',';
		}
		stack_words += util::string_format("%06x:%04x", (sp + index * 2) & 0x00ffffff, read_loaded_word((sp + index * 2) & 0x00ffffff));
		stack_longs += util::string_format("%06x:%08x", (sp + index * 4) & 0x00ffffff, read_stack_long((sp + index * 4) & 0x00ffffff));
	}

	std::string lowmem_0d_words;
	for (u32 cursor = 0x00000cfc; cursor <= 0x00000d10; cursor += 2)
	{
		if (cursor != 0x00000cfc)
			lowmem_0d_words += ',';
		lowmem_0d_words += util::string_format("%04x:%04x", cursor, m_lowmem_shadow[cursor >> 1]);
	}

	std::string lowmem_0e_words;
	for (u32 cursor = 0x00000e7c; cursor <= 0x00000e88; cursor += 2)
	{
		if (cursor != 0x00000e7c)
			lowmem_0e_words += ',';
		lowmem_0e_words += util::string_format("%04x:%04x", cursor, m_lowmem_shadow[cursor >> 1]);
	}

	logerror("ASR10_FC6816_SERVICE_SETTER_CONTEXT pc=%06x previous_pc=%06x opcode=%04x "
		"data=%04x mem_mask=%04x old_fc6816=%04x new_fc6816=%04x set_bits=%04x cleared_bits=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"sr=%04x sr_mask=%u sp=%06x return_address=%08x recent_rte_return_pc=%06x "
		"fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x fc6884=%04x fc6894=%04x "
		"lowmem_0d06=%04x lowmem_0e82=%04x lowmem_0d_window=\"%s\" lowmem_0e_window=\"%s\" "
		"stack_words=\"%s\" stack_longs=\"%s\" "
		"recent_queue_pc=%06x recent_queue_rw=%c recent_queue_addr=%06x recent_queue_record=%06x "
		"recent_queue_slot=%u recent_queue_previous=%04x recent_queue_current=%04x "
		"recent_queue_data=%04x recent_queue_mem_mask=%04x recent_queue_handler_clear=%u "
		"dispatcher_count=%u rte_count=%u panel=\"%s\"\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), data, mem_mask, old_shadow, new_shadow,
		(~old_shadow & new_shadow), (old_shadow & ~new_shadow),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		u16(m_maincpu->state_int(M68K_SR)), (u16(m_maincpu->state_int(M68K_SR)) >> 8) & 7,
		sp, read_stack_long(sp), m_queue_rte_last_return_pc,
		m_m68302_internal_shadow[0x12 >> 1], m_m68302_internal_shadow[0x14 >> 1],
		m_m68302_internal_shadow[0x16 >> 1], m_m68302_internal_shadow[0x18 >> 1],
		m_m68302_internal_shadow[0x84 >> 1], m_m68302_internal_shadow[0x94 >> 1],
		m_lowmem_shadow[0x0d06 >> 1], m_lowmem_shadow[0x0e82 >> 1],
		lowmem_0d_words.c_str(), lowmem_0e_words.c_str(),
		stack_words.c_str(), stack_longs.c_str(),
		m_recent_queue_pc, m_recent_queue_write ? 'W' : 'R', m_recent_queue_address,
		m_recent_queue_record_base, m_recent_queue_slot, m_recent_queue_previous,
		m_recent_queue_current, m_recent_queue_data, m_recent_queue_mem_mask,
		m_recent_queue_handler_clear ? 1 : 0, m_runtime_dispatch_entry_count,
		m_f87f96_queue_rte_count, m_panel_text);
}


void asr10_boot_state::log_fc688x_service_context(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow)
{
	if (machine().side_effects_disabled())
		return;
	if (address != 0x00fc6884 && address != 0x00fc6894)
		return;
	if (!m_synth_68302_timer_iack_fire_count && m_last_distinct_pc < 0x0000bee0)
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	logerror("ASR10_FC688X_SERVICE_CONTEXT pc=%06x previous_pc=%06x opcode=%04x rw=%c addr=%06x detail=%s "
		"data=%04x mem_mask=%04x old_shadow=%04x new_shadow=%04x changed_bits=%04x "
		"fc6812=%04x fc6814=%04x fc6816=%04x fc6818=%04x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"sr=%04x sr_mask=%u recent_queue_pc=%06x recent_queue_slot=%u recent_rte_return_pc=%06x panel=\"%s\"\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), write ? 'W' : 'R', address, m68302_register_name(address),
		data, mem_mask, old_shadow, new_shadow, old_shadow ^ new_shadow,
		m_m68302_internal_shadow[0x12 >> 1], m_m68302_internal_shadow[0x14 >> 1],
		m_m68302_internal_shadow[0x16 >> 1], m_m68302_internal_shadow[0x18 >> 1],
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		sr, (sr >> 8) & 7, m_recent_queue_pc, m_recent_queue_slot, m_queue_rte_last_return_pc,
		m_panel_text);
}


void asr10_boot_state::log_timer_candidate(bool write, u32 address, u16 data, u16 mem_mask, u16 old_shadow, u16 new_shadow)
{
	const u32 reg = address & 0xff;
	if (reg < 0x48 || reg > 0x56 || machine().side_effects_disabled())
		return;

	const bool focused = (reg == 0x4a || reg == 0x50 || reg == 0x52);
	m_timer_candidate_trace_count++;
	if (!focused && m_timer_candidate_trace_count > 128 &&
		(m_timer_candidate_trace_count & (m_timer_candidate_trace_count - 1)))
		return;

	const u32 pc = m_maincpu->state_int(STATE_GENPCBASE) & 0x00ffffff;
	const u32 sp = m_maincpu->state_int(M68K_SP) & 0x00ffffff;
	const u16 sr = u16(m_maincpu->state_int(M68K_SR));
	const u16 fc6812 = m_m68302_internal_shadow[0x12 >> 1];
	const u16 fc6814 = m_m68302_internal_shadow[0x14 >> 1];
	const u16 fc6816 = m_m68302_internal_shadow[0x16 >> 1];
	const u16 fc6818 = m_m68302_internal_shadow[0x18 >> 1];
	const u16 fc684a = m_m68302_internal_shadow[0x4a >> 1];
	const u16 fc6850 = m_m68302_internal_shadow[0x50 >> 1];
	const u16 fc6852 = m_m68302_internal_shadow[0x52 >> 1];
	const u16 changed = old_shadow ^ new_shadow;
	const u16 mode = (reg == 0x50) ? new_shadow : fc6850;

	logerror("ASR10_TIMER_CANDIDATE pc=%06x previous_pc=%06x opcode=%04x rw=%c addr=%06x reg=%02x detail=%s "
		"data=%04x mem_mask=%04x old_shadow=%04x new_shadow=%04x changed_bits=%04x "
		"sr=%04x sr_mask=%u sp=%06x stack0=%08x stack1=%08x "
		"d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
		"fc6812_mask=%04x fc6814_pending=%04x fc6816_in_service=%04x fc6818_control=%04x "
		"fc684a=%04x fc6850=%04x fc6852=%04x "
		"mode_bit5=%u mode_bit4=%u mode_bit3=%u mode_bit2=%u mode_bit1=%u mode_bit0=%u "
		"mode_decode=local_bits_only timer_binding=none dispatcher_count=%u last_rte_return_pc=%06x "
		"recent_queue_pc=%06x recent_queue_slot=%06x recent_queue_current=%04x phase=%s count=%u\n",
		pc, m_last_distinct_pc, read_loaded_word(pc), write ? 'W' : 'R', address, reg, m68302_register_name(address),
		data, mem_mask, old_shadow, new_shadow, changed,
		sr, (sr >> 8) & 7, sp, read_stack_long(sp), read_stack_long(sp + 4),
		u32(m_maincpu->state_int(M68K_D0)), u32(m_maincpu->state_int(M68K_D1)),
		u32(m_maincpu->state_int(M68K_D2)), u32(m_maincpu->state_int(M68K_D3)),
		u32(m_maincpu->state_int(M68K_A0)), u32(m_maincpu->state_int(M68K_A1)),
		u32(m_maincpu->state_int(M68K_A2)), u32(m_maincpu->state_int(M68K_A3)),
		fc6812, fc6814, fc6816, fc6818, fc684a, fc6850, fc6852,
		BIT(mode, 5), BIT(mode, 4), BIT(mode, 3), BIT(mode, 2), BIT(mode, 1), BIT(mode, 0),
		m_runtime_dispatch_entry_count, m_queue_rte_last_return_pc & 0x00ffffff,
		m_recent_queue_pc, m_recent_queue_slot, m_recent_queue_current,
		m_seen_loading_system_prompt ? "post_loading_system" : "boot",
		m_timer_candidate_trace_count);

	if constexpr (ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ || ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR)
	{
		if (write && reg == 0x50 && new_shadow != 0)
		{
			logerror("ASR10_EXPERIMENT_SYNTH_68302_TIMER_START pc=%06x fc6850=%04x fc6852_reference=%04x "
				"raw_irq_enabled=%u raw_irq_level=%u iack_vector_enabled=%u iack_irq_level=%u "
				"iack_vector_byte=%02x iack_source_mask=%04x period=1ms frequency=diagnostic_not_derived\n",
				pc, new_shadow, fc6852,
				ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ ? 1 : 0, ASR10_EXPERIMENT_SYNTH_68302_TIMER_IRQ_LEVEL,
				ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR ? 1 : 0,
				ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_IRQ_LEVEL,
				ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_VECTOR_BYTE,
				ASR10_EXPERIMENT_SYNTH_68302_TIMER_IACK_SOURCE_MASK);
			m_synth_68302_timer_irq_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));
		}
	}
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


u16 asr10_boot_state::read_loaded_word(u32 address) const
{
	address &= 0x00ffffff;
	if (address < 0x00100000 && m_lowmem_overlay_enabled)
		return m_lowmem_shadow[(address >> 1) & (LOWMEM_WORDS - 1)];
	return read_code_word(address);
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
	M68000(config, m_maincpu, XTAL(16'000'000)); // 68000-compatible stand-in for likely MC68302-family board
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &asr10_boot_state::cpu_space_map);
	m_maincpu->set_rte_callback(FUNC(asr10_boot_state::log_f87f96_queue_rte));

	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder for boot tracing

	// The uPD72069 sees this child connector as drive 0 via the conventional "fdc:0" tag.
	// Mounted HFE media changes Recalibrate/Sense from 68,00 (not ready) to 20,00.
	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);

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