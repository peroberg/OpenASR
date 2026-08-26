// license:BSD-3-Clause
// copyright-holders:
/* MC68302 -- BAR-relokerbart internt registerfönster, SCR, Port B PIO,
   chip selects, minimal IDMA och en receive-only SCC/CP-väg. SCC-vägen
   börjar efter seriell framing och implementerar endast det observerade
   ASR-10-kontraktet: RX-BD, MRBLR, E/W, SCCE bit 0 och SCC1/SCC2 INRQ.

   Strukturell mall: src/devices/machine/68307.h/68307sim.h. Register-
   värden och bitlayouter: docs/mc68302/ (flyttat från
   ~/develop/mc68302/docs/mc68302/), inte 68307. */
#ifndef MAME_MACHINE_MC68302_H
#define MAME_MACHINE_MC68302_H

#pragma once

#include "cpu/m68000/m68000.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>


class mc68302_device : public m68000_device
{
public:
	mc68302_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// Every access to the internal 4KB SIB window is bucketed into one of
	// these four classes. `known` = this step has deliberate handling for
	// the register, not necessarily fully verified MC68302 semantics.
	// TODO: keep `known` reserved for verified semantics as this model
	// grows; FC6860 and chip-select decode are currently partial.
	// `internal_ram` = plain dual-port RAM (0x000-0x3FF) -- genuinely just
	// memory, not a register; the ROM parks its supervisor stack there,
	// so this is where the bulk of any poll/delay-loop access volume
	// lives, and it isn't a guess in any sense that matters for the oracle. `known_
	// unimplemented` = the offset is a documented MC68302 register
	// (docs/mc68302/sib-register-map.md, communications-block-map.md,
	// including 0x400-0x7FF's SCC/SMC parameter RAM) but this device
	// only shadows the value, no side effects. `unknown` = the offset is
	// not in any of the above at all.
	enum class sib_access_class : uint8_t
	{
		known,
		internal_ram,
		known_unimplemented,
		unknown
	};

	uint32_t known_access_count() const { return m_known_count; }
	uint32_t internal_ram_access_count() const { return m_internal_ram_count; }
	uint32_t known_unimplemented_access_count() const { return m_known_unimplemented_count; }
	uint32_t unknown_access_count() const { return m_unknown_count; }

	// The raw *_access_count() totals above are dominated by tight poll
	// loops hitting a handful of addresses -- not useful as an oracle.
	// Distinct-offset counts and the hottest individual addresses are.
	struct access_class_counts
	{
		uint32_t known = 0;
		uint32_t internal_ram = 0;
		uint32_t known_unimplemented = 0;
		uint32_t unknown = 0;
	};
	access_class_counts distinct_offset_counts() const;

	struct offset_hit { uint32_t byte_offset = 0; uint32_t count = 0; };
	std::vector<offset_hit> top_accessed_offsets(unsigned max_entries) const;

	// BR0/OR0 address-range decode (docs/mc68302/sib-register-map.md):
	// true while CS0 currently claims `address`. NOTE: FC/RW/CFC/MRW and
	// DTACK are not modeled here, so this is not full chip-select decode.
	// The driver's low-memory ROM/RAM
	// overlay is a direct consequence of this -- when the ROM relocates
	// CS0 away from address 0 (BR0=0x1f01/OR0=0x3f82 -> 0xf80000), the
	// overlay must flip. See mc68302sim.h/.cpp for the decode.
	bool cs0_covers(uint32_t address) const;

	// Board-level signal into a Port B GPIO input pin (e.g. the driver's
	// LRCLK timer into PB3). See mc68302sim.h's set_external_input().
	void set_pb_input(unsigned bit, bool level);
	// Port-B output latch as last written by the CPU.  This deliberately does
	// not apply PBCNT/PBDDR input muxing: a board device that is wired to an
	// output pin needs the driven latch value, while read_pbdat() models CPU
	// readback.  Board-specific interpretation belongs to the owning machine.
	uint16_t pbdat_latch() const;

	// External IRQ6 IACK vector, MC68302 User's Manual Table 3-5 /
	// docs/mc68302/vector-origin-map.md: vector = (GIMR bits 7-5 << 5) |
	// source_low_5. Fas 3 steg 2, minimal slice: only the external
	// vector-supply formula for level 6, hardcoded to the ASR-10 boot's
	// fixed GIMR=0x8040 (docs/asr10/baseline-media.md) since GIMR itself
	// isn't a modeled register yet. No priority, no nesting, no IPR/ISR,
	// no IMR masking of internal sources -- the full interrupt controller
	// stays future work (mc68302int.cpp).
	uint8_t irq6_ack_vector() const { return 0x40 | 0x16; } // GIMR.V7_V5=010 | external level 6 low bits

	// Same formula, external IRQ1 (docs/mc68302/vector-origin-map.md
	// External Vectors table: IRQ1/level 1 EXRQ low bits 0x11, IV1=0).
	// External EXRQ levels 1/6/7 bypass the internal INRQ pending/mask/
	// priority machinery entirely -- they assert a CPU IPL line directly,
	// exactly like level 6 already does here, and only need this
	// hardcoded vector-supply formula plus a CPU-space IACK handler.
	// Board-side source assertion/deassertion is therefore a plain
	// devcb_write_line into set_inputline(), not a new register model.
	uint8_t irq1_ack_vector() const { return 0x40 | 0x11; } // GIMR.V7_V5=010 | external level 1 low bits

	// Receive-only CP boundary. The caller supplies an already-framed byte;
	// this device owns buffer-descriptor writes and SCC interrupt state.
	// Returns false when the channel cannot currently accept the byte.
	bool scc_rx_byte(unsigned channel, uint8_t data);
	uint8_t irq4_ack_vector();

	// Minimal IDMA channel with two measured modes. $0D51 remains the
	// external-request byte path used by FDC DRQ and idma_transfer_in().
	// $37A1 is the sampling path: one internally requested, incrementing
	// word copy from SAPR to DAPR followed by CSR/IPR completion. Other
	// modes, arbitration, external pins and error termination are absent.
	//
	// idma_channel_active(): true once CMR bit 0 (STR, measured: clear in
	// the $0002 shared-prelude write that recurs at every vector $51 IACK,
	// set in the $0D51 write immediately before READ DATA -- the only
	// measured bit in CMR with a clean, repeatable on/off correlation to
	// "is this the transfer-starting write") has armed the channel and the
	// byte count has not yet been exhausted. The board-side DRQ handler
	// MUST check this before calling the source device's own DMA-pop
	// accessor (e.g. upd765_family_device::dma_r()) -- upd765_family_
	// device::enable_transfer() asserts DRQ unconditionally on every
	// transfer, PIO included (no else between its PIO-internal_drq branch
	// and its DRQ-assert branch), so an unguarded handler would silently
	// steal FIFO bytes during the boot's own unrelated polled transfers.
	//
	// idma_transfer_in(data): board-side DRQ handler calls this once per
	// byte pulled from an external source device (e.g. via upd765_family_device::
	// dma_r()). Writes to the current DAPR address in program space,
	// increments DAPR, decrements the working count. Returns true when the
	// channel has just completed (this was the last byte) -- the caller
	// must then call the source device's tc_w(true)/tc_w(false) itself;
	// this device has no FDC-specific knowledge and does not call tc_w on
	// its own. Undocumented/unverified byte-count convention: BCR's raw
	// measured value was 513 for a 512-byte sector; this implements
	// "transfer (BCR-1) bytes" as the least speculative reading that still
	// produces the correct 512, not a scaling/unit reinterpretation of
	// BCR's bytes -- flagged as unverified beyond this one measurement and
	// deliberately separate from $37A1's exact BCR-byte memory copy.
	bool idma_channel_active() const { return m_idma_active && m_idma_remaining != 0; }
	bool idma_transfer_in(uint8_t data);

protected:
	class mc68302_sim;

	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;
	virtual void state_import(const device_state_entry &entry) override;

private:
	void bootstrap_map(address_map &map) ATTR_COLD;

	uint16_t bar_scr_r(offs_t offset, uint16_t mem_mask = ~0);
	void bar_scr_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	uint16_t internal_r(offs_t offset, uint16_t mem_mask = ~0);
	void internal_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	void install_internal_window();
	void remove_internal_window();
	void handle_cp_command(uint8_t command);
	void update_internal_irq();
	TIMER_CALLBACK_MEMBER(idma_internal_transfer);

	static sib_access_class classify_offset(uint16_t byte_offset);
	static sib_access_class classify_full(uint16_t byte_offset);

	std::unique_ptr<mc68302_sim> m_sim;

	// Generic shadow storage for the known-unimplemented ranges (dual-port
	// RAM, IDMA, interrupt controller, Port A, chip selects, timers,
	// watchdog, SCC/SMC/SCP) -- 0x800 words covers the full 4KB window.
	// NOTE: documented-but-unimplemented registers behave as RAM here;
	// firmware can therefore pass probes for the wrong reason. This is
	// temporary plumbing, not register semantics.
	// PBCNT/PBDDR/PBDAT/FC6860 are intercepted before reaching this and
	// live in m_sim instead.
	std::array<uint16_t, 0x800> m_shadow{};

	// Per-offset access count, same indexing as m_shadow, for
	// distinct_offset_counts()/top_accessed_offsets().
	std::array<uint32_t, 0x800> m_offset_access_count{};

	uint16_t m_bar;
	uint16_t m_scr_high;
	uint16_t m_scr_low;
	bool m_window_installed;
	uint32_t m_window_base;

	uint32_t m_known_count;
	uint32_t m_internal_ram_count;
	uint32_t m_known_unimplemented_count;
	uint32_t m_unknown_count;

	// Post-framing test ingress is exposed as hidden debugger state so Lua
	// can supply bytes without an ASR-10 driver bridge or a fake MMIO range.
	static constexpr int SCC1_RX_STATE = 0x1000;
	static constexpr int SCC2_RX_STATE = 0x1001;
	std::array<uint8_t, 2> m_scc_rx_ingress{};
	std::array<uint16_t, 2> m_scc_rx_descriptor{};

	// IDMA channel state. The existing external-request byte path is kept
	// separate from the observed $37A1 internal memory-to-memory request.
	uint16_t m_idma_cmr;
	uint32_t m_idma_sapr;
	uint32_t m_idma_dapr;
	uint16_t m_idma_bcr;
	uint8_t m_idma_csr;
	uint8_t m_idma_fcr;
	bool m_idma_active;
	uint32_t m_idma_source;
	uint32_t m_idma_dest;
	uint32_t m_idma_remaining;
	emu_timer *m_idma_timer = nullptr;
};

DECLARE_DEVICE_TYPE(MC68302, mc68302_device)

#endif // MAME_MACHINE_MC68302_H
