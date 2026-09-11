// license:BSD-3-Clause
// copyright-holders:
/* MC68302 SIM module -- fas 3 steg 1 (+ chip select decoding, added per
   docs/asr10/PLAN.md fas 3: "BAR utan CS0 ar ingen fungerande
   minnesmodell"). Port B PIO (PBCNT/PBDDR/PBDAT), the undocumented
   FC6860 busy register, and BR0-3/OR0-3 chip select base/size/enable
   decoding. Register semantics: docs/mc68302/pin-function-map.md,
   docs/mc68302/sib-register-map.md. No Port A, no interrupt
   controller, no timers, no watchdog -- those stay known-unimplemented
   shadow storage in mc68302_device::internal_r/w for this step.

   Chip select scope: BR/OR base address and block size decoding follows
   the documented field layout used by the ASR-10 observations.
   FC-compare and RW-compare
   enforcement are explicitly out of scope (sib-register-map.md: "not
   yet modeled by any MAME device") -- only CS0's base/size/enable
   feed driver behavior (the ROM/RAM low-memory overlay), and only
   because that is the one CS with a verified, necessary side effect
   this step replaces (docs/asr10/experiment-flags.md's
   CONTROL_REGISTER_CANDIDATE finding). CS1-3 are decoded the same way
   for completeness and future use but consumed by nothing yet. */
#ifndef MAME_MACHINE_MC68302SIM_H
#define MAME_MACHINE_MC68302SIM_H

#pragma once

#include "mc68302.h"


class mc68302_device::mc68302_sim
{
public:
	// PBCNT reset value 0x0080 selects the dedicated WDOG function on
	// PB7; PBDDR resets to 0 (all input). docs/mc68302/pin-function-map.md.
	static constexpr uint16_t PBCNT_RESET = 0x0080;

	// FC6860 has no manual-documented identity (docs/mc68302/sib-register-map.md's
	// note). NOTE: this busy-bit behavior is ASR-10-observed and
	// experimental, not verified generic MC68302 semantics. TODO: move it
	// to machine policy/callback or otherwise isolate it more clearly.
	static constexpr uint8_t FC6860_READ_DELAY = 2;

	// Reset values from docs/mc68302/sib-register-map.md's "reset CS0"
	// / "reset CS1-3" rows: CS0 alone is enabled at reset, mapping the
	// boot ROM's first 8 KiB at address 0 for supervisor program
	// fetches; CS1-3 differ from CS0 only in EN (bit 0).
	static constexpr uint16_t CS0_RESET_BR = 0xc001;
	static constexpr uint16_t CS1_3_RESET_BR = 0xc000;
	static constexpr uint16_t CS_RESET_OR = 0xdffd;

	// Decoded outcome of a BR/OR pair: whether this chip select is
	// enabled, and the physical address range [base, end) it currently
	// claims. FC/RW compare fields are intentionally not decoded here
	// (see file header) -- base/size/enable are the only load-bearing
	// outputs.
	struct cs_decode
	{
		bool enabled = false;
		uint32_t base = 0;
		uint32_t end = 0;
	};

	// Constructs already reset: the driver's machine_start() can query
	// cs0_covers() (via read_loaded_word()) before this device's own
	// device_start()/device_reset() run, since the driver device starts
	// before its child devices in MAME's start order. device_reset()
	// calls reset() again later, idempotently.
	mc68302_sim() { reset(); }

	void reset();

	void write_pacnt(uint16_t data, uint16_t mem_mask);
	void write_paddr(uint16_t data, uint16_t mem_mask);
	uint16_t read_padat(uint16_t mem_mask) const;
	void write_padat(uint16_t data, uint16_t mem_mask);
	uint16_t padat_latch() const { return m_padat; }
	uint16_t pacnt_latch() const { return m_pacnt; }
	uint16_t paddr_latch() const { return m_paddr; }

	void write_pbcnt(uint16_t data, uint16_t mem_mask);
	void write_pbddr(uint16_t data, uint16_t mem_mask);
	uint16_t read_pbdat(uint16_t mem_mask) const;
	void write_pbdat(uint16_t data, uint16_t mem_mask);
	uint16_t pbdat_latch() const { return m_pbdat; }
	uint16_t pbcnt_latch() const { return m_pbcnt & 0x00ff; }
	uint16_t pbddr_latch() const { return m_pbddr & 0x0fff; }

	// External state driven onto a Port B pin from outside the chip --
	// board wiring, not 68302-internal logic (docs/mc68302/pin-function-map.md:
	// "no ASR-10 board-level source is modeled or claimed" -- this is
	// where the driver supplies one). Only takes effect on pins
	// currently configured as GPIO input; ignored otherwise, matching
	// real hardware (an output-driver pin isn't listening to the
	// outside world, and a dedicated-function pin isn't running as GPIO).
	void set_external_input(unsigned bit, bool level);

	uint8_t read_fc6860();
	void write_fc6860(uint8_t data);

	uint16_t read_br(unsigned index) const { return m_br[index]; }
	uint16_t read_or(unsigned index) const { return m_or[index]; }
	void write_br(unsigned index, uint16_t data, uint16_t mem_mask);
	void write_or(unsigned index, uint16_t data, uint16_t mem_mask);

	const cs_decode &cs(unsigned index) const { return m_cs[index]; }
	// NOTE: address range only; FC/RW/CFC/MRW and DTACK are ignored.
	bool cs0_covers(uint32_t address) const { return m_cs[0].enabled && address >= m_cs[0].base && address < m_cs[0].end; }

	void register_save_items(save_manager &save, device_t &device);
	void recompute_all_cs();

private:
	void recompute_cs(unsigned index);

	uint16_t m_pacnt = 0x0000;
	uint16_t m_paddr = 0x0000;
	uint16_t m_padat = 0x0000;
	uint16_t m_pa_external_input = 0x0000;

	uint16_t m_pbcnt = PBCNT_RESET;
	uint16_t m_pbddr = 0x0000;
	uint16_t m_pbdat = 0x0000;
	uint16_t m_pb_external_input = 0x0000;

	uint8_t m_fc6860 = 0x00;
	uint8_t m_fc6860_reads_since_write = 0;

	uint16_t m_br[4]{};
	uint16_t m_or[4]{};
	cs_decode m_cs[4];
};

#endif // MAME_MACHINE_MC68302SIM_H
