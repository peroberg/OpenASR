// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Ensoniq ASR-10 boot harness

    Experimental upstream-MAME-oriented boot tracer for the ASR-10 1.5B ROM.
    This is not a full ASR-10 driver and not an emulator contract.

    Canonical project references:
    - docs/hardware-identity.md
    - docs/address-model.md
    - docs/mame-asr10-boot-harness.md

    Current conservative model:
    - ASR-10 1.5B EPROM pair
    - 68000-compatible big-endian boot code on likely MC68302-family board
    - $fc68xx is board/MMIO
    - $fc6830 is control_register_candidate
    - high-runtime alias behavior is an experiment, not proven hardware

***************************************************************************/

#include "emu.h"

#include "cpu/m68000/m68000.h"

#include <algorithm>


namespace {

class asr10_boot_state : public driver_device
{
public:
	asr10_boot_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rom(*this, "maincpu")
	{
	}

	void asr10_boot(machine_config &config) ATTR_COLD;

private:
	static constexpr u32 ROM_MASK = 0x0003ffff;
	static constexpr u32 LOWMEM_LOG_END = 0x00000200;
	static constexpr u32 CONTROL_REGISTER_CANDIDATE = 0x00fc6830;
	static constexpr u32 PROBE_OR_ALIAS_REGION_COUNT = 4;

	required_device<m68000_device> m_maincpu;
	required_memory_region m_rom;

	emu_timer *m_pc_timer = nullptr;
	u16 m_lowmem_shadow[LOWMEM_LOG_END / 2]{};
	u16 m_probe_or_alias_region_shadow[PROBE_OR_ALIAS_REGION_COUNT][2]{};
	bool m_high_alias_enabled = false;
	bool m_lowmem_overlay_enabled = false;
	u64 m_pc_poll_count = 0;
	u32 m_last_pc = 0xffffffffU;
	u32 m_dispatcher_hits = 0;

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
	u16 fc68xx_r(offs_t offset, u16 mem_mask = ~0);
	void fc68xx_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(pc_poll);

	void log_lowmem_write(u32 address, u16 data, u16 mem_mask);
	bool probe_or_alias_region_index(u32 address, u32 &index, u32 &word_index) const;
	u16 probe_or_alias_region_r_at(u32 base, offs_t offset, u16 mem_mask);
	void probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask);
	void log_probe_or_alias_region(const char *op, u32 address, u16 data, u16 mem_mask);
	void log_fc68xx_write(u32 address, u16 data, u16 mem_mask);
	void log_watched_pc(u32 pc);
};


void asr10_boot_state::machine_start()
{
	m_pc_timer = timer_alloc(FUNC(asr10_boot_state::pc_poll), this);

	save_item(NAME(m_lowmem_shadow));
	save_item(NAME(m_probe_or_alias_region_shadow));
	save_item(NAME(m_high_alias_enabled));
	save_item(NAME(m_lowmem_overlay_enabled));
	save_item(NAME(m_pc_poll_count));
	save_item(NAME(m_last_pc));
	save_item(NAME(m_dispatcher_hits));
}


void asr10_boot_state::machine_reset()
{
	m_high_alias_enabled = false;
	m_lowmem_overlay_enabled = false;
	m_pc_poll_count = 0;
	m_last_pc = 0xffffffffU;
	m_dispatcher_hits = 0;
	std::fill(std::begin(m_lowmem_shadow), std::end(m_lowmem_shadow), 0);
	for (auto &entry : m_probe_or_alias_region_shadow)
		std::fill(std::begin(entry), std::end(entry), 0);

	// Coarse landmark polling only. This does not replace instruction tracing.
	m_pc_timer->adjust(attotime::zero, 0, attotime::from_ticks(64, m_maincpu->clock()));

	logerror("ASR10BOOT reset: expected SP=$00000300 PC=$0000000c from ROM vectors\n");
}


void asr10_boot_state::mem_map(address_map &map)
{
	// Low boot region. Reads normally come from ROM; writes are logged and
	// shadowed only where the static analysis expects low-memory/vector data.
	map(0x000000, 0x03ffff).rw(FUNC(asr10_boot_state::low_rom_or_lowmem_r), FUNC(asr10_boot_state::lowmem_w));

	// Minimal RAM/MMIO map for boot/remap tracing only.
	map(0x408000, 0x408003).rw(FUNC(asr10_boot_state::probe_or_alias_region_408000_r), FUNC(asr10_boot_state::probe_or_alias_region_408000_w));
	map(0x808000, 0x808003).rw(FUNC(asr10_boot_state::probe_or_alias_region_808000_r), FUNC(asr10_boot_state::probe_or_alias_region_808000_w));
	map(0xc08000, 0xc08003).rw(FUNC(asr10_boot_state::probe_or_alias_region_c08000_r), FUNC(asr10_boot_state::probe_or_alias_region_c08000_w));
	map(0xf00000, 0xf7ffff).ram();
	map(0xf80000, 0xfbffff).rw(FUNC(asr10_boot_state::high_alias_r), FUNC(asr10_boot_state::high_alias_w));
	map(0xfc0000, 0xfc67ff).ram();
	map(0xfc6800, 0xfc68ff).rw(FUNC(asr10_boot_state::fc68xx_r), FUNC(asr10_boot_state::fc68xx_w));
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
		log_probe_or_alias_region("read", byte_address, data, mem_mask);
		return data;
	}

	if (m_lowmem_overlay_enabled && byte_address < LOWMEM_LOG_END)
		return m_lowmem_shadow[offset] & mem_mask;

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
		log_probe_or_alias_region("write", byte_address, data, mem_mask);
		return;
	}

	if (byte_address < LOWMEM_LOG_END)
		COMBINE_DATA(&m_lowmem_shadow[offset]);

	if (byte_address < LOWMEM_LOG_END || byte_address == 0x00ea || byte_address == 0x0b7a || byte_address == 0x0b7c || byte_address == 0x0b7e)
		log_lowmem_write(byte_address, data, mem_mask);
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
	if (!m_high_alias_enabled)
		logerror("ASR10BOOT high-alias read before enable: pc=%08x addr=%06x mask=%04x\n", m_maincpu->pc(), 0xf80000 + (offset << 1), mem_mask);

	const u8 *rom = m_rom->base();
	const u32 runtime_address_24 = 0x00f80000 | (offset << 1);
	const u32 rom_offset = runtime_address_24 & ROM_MASK;
	return (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
}


void asr10_boot_state::high_alias_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00f80000 | (offset << 1);
	logerror("ASR10BOOT high-alias write: pc=%08x addr=%06x data=%04x mask=%04x classification=unknown\n", m_maincpu->pc(), address, data, mem_mask);
}


u16 asr10_boot_state::fc68xx_r(offs_t offset, u16 mem_mask)
{
	const u32 address = 0x00fc6800 | (offset << 1);
	logerror("ASR10BOOT fc68xx read: pc=%08x addr=%06x mask=%04x classification=verified_board_mmio_access semantics=unknown\n", m_maincpu->pc(), address, mem_mask);
	return 0x0000;
}


void asr10_boot_state::fc68xx_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00fc6800 | (offset << 1);
	log_fc68xx_write(address, data, mem_mask);

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


void asr10_boot_state::log_lowmem_write(u32 address, u16 data, u16 mem_mask)
{
	const bool dispatcher_state = (address == 0x00ea || address == 0x0b7a || address == 0x0b7c || address == 0x0b7e);
	logerror("ASR10BOOT lowmem write: pc=%08x addr=%04x data=%04x mask=%04x dispatcher_state=%d classification=verified\n",
		m_maincpu->pc(), address, data, mem_mask, dispatcher_state ? 1 : 0);
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
		log_probe_or_alias_region("read", address, data, mem_mask);
		return data;
	}

	log_probe_or_alias_region("read", address, 0, mem_mask);
	return 0;
}


void asr10_boot_state::probe_or_alias_region_w_at(u32 base, offs_t offset, u16 data, u16 mem_mask)
{
	u32 index = 0;
	u32 word = 0;
	const u32 address = base + ((offset << 1) & 0x00000002);
	if (probe_or_alias_region_index(address, index, word))
		COMBINE_DATA(&m_probe_or_alias_region_shadow[index][word]);

	log_probe_or_alias_region("write", address, data, mem_mask);
}


void asr10_boot_state::log_probe_or_alias_region(const char *op, u32 address, u16 data, u16 mem_mask)
{
	logerror("ASR10BOOT probe_or_alias_region %s: pc=%08x addr=%06x data=%04x mask=%04x classification=likely_bus_or_chip_select_probe semantics=unknown\n",
		op, m_maincpu->pc(), address, data, mem_mask);
}


void asr10_boot_state::log_fc68xx_write(u32 address, u16 data, u16 mem_mask)
{
	const char *name = (address == CONTROL_REGISTER_CANDIDATE) ? "control_register_candidate" : "board_mmio_unknown";
	const char *classification = (address == CONTROL_REGISTER_CANDIDATE) ? "likely_remap_control" : "verified_board_mmio_access";
	logerror("ASR10BOOT fc68xx write: pc=%08x addr=%06x name=%s data=%04x mask=%04x classification=%s semantics=unknown\n",
		m_maincpu->pc(), address, name, data, mem_mask, classification);
}


void asr10_boot_state::log_watched_pc(u32 pc)
{
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


TIMER_CALLBACK_MEMBER(asr10_boot_state::pc_poll)
{
	const u32 pc = m_maincpu->pc() & 0x00ffffff;
	m_pc_poll_count++;

	if (pc != m_last_pc)
	{
		logerror("ASR10BOOT pc: pc=%08x pc24=%06x\n", m_maincpu->pc(), pc);
		log_watched_pc(pc);
		m_last_pc = pc;
	}

	if (m_pc_poll_count > 2000000)
	{
		logerror("ASR10BOOT stop: max poll count reached pc=%08x pc24=%06x last_pc=%06x poll_count=%llu\n",
			m_maincpu->pc(), pc, m_last_pc, m_pc_poll_count);
		machine().schedule_exit();
	}
}

static INPUT_PORTS_START(asr10_boot)
INPUT_PORTS_END


void asr10_boot_state::asr10_boot(machine_config &config)
{
	M68000(config, m_maincpu, XTAL(16'000'000)); // 68000-compatible stand-in for likely MC68302-family board
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);
}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness (experiment)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)

/**
 *
 * 68302 CPU address space
 *  $0000f2/$0000f4    initial BAR/SCR
 *  $fc6000-$fc6fff    68302 internal DPRAM/register block candidate
 *  ?                  ES5506 host regs, small window
 *  ?                  ES5510 host regs, ~0x200 window
 *  ?                  floppy controller
 *  ?                  SCSI controller
 *  ?                  frontpanel/display/keyscan/glue
 *  ?                  sample RAM window / main sample RAM
 *  ?                  work RAM / high RAM
 *  ?                  ROM / high alias
 */
