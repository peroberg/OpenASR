// license:BSD-3-Clause
// copyright-holders:
/* MC68302 -- fas 3 steg 1 (plumbing): BAR-relokerbart internt
   registerfönster, SCR, Port B PIO, det odokumenterade FC6860-registret,
   och klassificering av varje access mot fönstret som known /
   known-unimplemented / unknown. Ingen interruptcontroller, ingen timer,
   ingen IDMA, ingen kommunikationsprocessor i det här steget -- se
   docs/asr10/PLAN.md fas 3 och docs/mc68302/.

   Strukturell mall: src/devices/machine/68307.h/68307sim.h. Register-
   värden och bitlayouter: docs/mc68302/ (flyttat från
   ~/develop/mc68302/docs/mc68302/), inte 68307. */
#ifndef MAME_MACHINE_MC68302_H
#define MAME_MACHINE_MC68302_H

#pragma once

#include "cpu/m68000/m68000.h"

#include <array>
#include <memory>


class mc68302_device : public m68000_device
{
public:
	mc68302_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// Every access to the internal 4KB SIB window is bucketed into one of
	// these three classes. `known` = the register is implemented in this
	// device and its side effects match this step's scope (Port B PIO,
	// the FC6860 busy register). `known_unimplemented` = the offset is a
	// documented MC68302 register (docs/mc68302/sib-register-map.md,
	// communications-block-map.md) but this device only shadows the
	// value, no side effects. `unknown` = the offset is not in either
	// list at all.
	enum class sib_access_class : uint8_t
	{
		known,
		known_unimplemented,
		unknown
	};

	uint32_t known_access_count() const { return m_known_count; }
	uint32_t known_unimplemented_access_count() const { return m_known_unimplemented_count; }
	uint32_t unknown_access_count() const { return m_unknown_count; }

	// Real BR0/OR0 decode (docs/mc68302/sib-register-map.md): true while
	// CS0 currently claims `address`. The driver's low-memory ROM/RAM
	// overlay is a direct consequence of this -- when the ROM relocates
	// CS0 away from address 0 (BR0=0x1f01/OR0=0x3f82 -> 0xf80000), the
	// overlay must flip. See mc68302sim.h/.cpp for the decode.
	bool cs0_covers(uint32_t address) const;

	// Board-level signal into a Port B GPIO input pin (e.g. the driver's
	// LRCLK timer into PB3). See mc68302sim.h's set_external_input().
	void set_pb_input(unsigned bit, bool level);

protected:
	class mc68302_sim;

	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;

private:
	void bootstrap_map(address_map &map) ATTR_COLD;

	uint16_t bar_scr_r(offs_t offset, uint16_t mem_mask = ~0);
	void bar_scr_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	uint16_t internal_r(offs_t offset, uint16_t mem_mask = ~0);
	void internal_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	void install_internal_window();
	void remove_internal_window();

	static sib_access_class classify_offset(uint16_t byte_offset);

	std::unique_ptr<mc68302_sim> m_sim;

	// Generic shadow storage for the known-unimplemented ranges (dual-port
	// RAM, IDMA, interrupt controller, Port A, chip selects, timers,
	// watchdog, SCC/SMC/SCP) -- 0x800 words covers the full 4KB window.
	// PBCNT/PBDDR/PBDAT/FC6860 are intercepted before reaching this and
	// live in m_sim instead.
	std::array<uint16_t, 0x800> m_shadow{};

	uint16_t m_bar;
	uint16_t m_scr_high;
	uint16_t m_scr_low;
	bool m_window_installed;
	uint32_t m_window_base;

	uint32_t m_known_count;
	uint32_t m_known_unimplemented_count;
	uint32_t m_unknown_count;
};

DECLARE_DEVICE_TYPE(MC68302, mc68302_device)

#endif // MAME_MACHINE_MC68302_H
