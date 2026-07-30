// license:BSD-3-Clause
// copyright-holders:
/* MC68302 -- fas 3 steg 1 (plumbing). See mc68302.h. */

#include "emu.h"
#include "mc68302.h"
#include "mc68302sim.h"

DEFINE_DEVICE_TYPE(MC68302, mc68302_device, "mc68302", "MC68302")


// Internal SIB register offsets this step recognizes by name (all other
// offsets in the documented ranges below are known-unimplemented shadow
// storage; docs/mc68302/sib-register-map.md).
static constexpr uint16_t OFFSET_PBCNT  = 0x0824;
static constexpr uint16_t OFFSET_PBDDR  = 0x0826;
static constexpr uint16_t OFFSET_PBDAT  = 0x0828;
static constexpr uint16_t OFFSET_FC6860 = 0x0860;

// BR0-3/OR0-3, docs/mc68302/sib-register-map.md. Each pair is 4 bytes
// apart; BR is the low offset of the pair, OR the high one.
static constexpr uint16_t OFFSET_BR0 = 0x0830;
static constexpr uint16_t OFFSET_OR0 = 0x0832;
static constexpr uint16_t OFFSET_BR1 = 0x0834;
static constexpr uint16_t OFFSET_OR1 = 0x0836;
static constexpr uint16_t OFFSET_BR2 = 0x0838;
static constexpr uint16_t OFFSET_OR2 = 0x083a;
static constexpr uint16_t OFFSET_BR3 = 0x083c;
static constexpr uint16_t OFFSET_OR3 = 0x083e;


mc68302_device::mc68302_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	m68000_device(mconfig, MC68302, tag, owner, clock),
	m_bar(0),
	m_scr_high(0),
	m_scr_low(0),
	m_window_installed(false),
	m_window_base(0),
	m_known_count(0),
	m_known_unimplemented_count(0),
	m_unknown_count(0)
{
	auto bmap = address_map_constructor(FUNC(mc68302_device::bootstrap_map), this);
	m_program_config.m_internal_map = bmap;
	m_opcodes_config.m_internal_map = bmap;
	m_uprogram_config.m_internal_map = bmap;
	m_uopcodes_config.m_internal_map = bmap;

	// Allocated here, not in device_start(): the driver's own
	// machine_start() can query cs0_covers() (via read_loaded_word())
	// before this device's device_start() runs, since the driver device
	// starts before its child devices in MAME's start order.
	m_sim = std::make_unique<mc68302_sim>();
}


void mc68302_device::bootstrap_map(address_map &map)
{
	// BAR at 0x000000F2 (word), SCR at 0x000000F4-F7 (two words).
	// docs/mc68302/scr-spec.md, docs/mc68302/sib-register-map.md.
	map(0x000000f0, 0x000000ff).rw(FUNC(mc68302_device::bar_scr_r), FUNC(mc68302_device::bar_scr_w));
}


void mc68302_device::device_start()
{
	m68000_device::device_start();

	m_sim = std::make_unique<mc68302_sim>();

	save_item(NAME(m_bar));
	save_item(NAME(m_scr_high));
	save_item(NAME(m_scr_low));
	save_item(NAME(m_window_installed));
	save_item(NAME(m_window_base));
	save_item(NAME(m_known_count));
	save_item(NAME(m_known_unimplemented_count));
	save_item(NAME(m_unknown_count));
}


void mc68302_device::device_stop()
{
	remove_internal_window();
}


void mc68302_device::device_reset()
{
	m68000_device::device_reset();

	remove_internal_window();

	// No manual-documented BAR reset value was found (docs/mc68302/scr-spec.md
	// only documents SCR's reset value). Zero is a safe "not yet configured"
	// sentinel: the ROM always writes BAR before the internal window is
	// used at all (docs/mc68302/observed-access-coverage.md, checkpoint 1
	// is the SCR write, checkpoint 3 is the first internal-window access,
	// both after BAR).
	m_bar = 0;
	m_scr_high = 0x0000;
	m_scr_low = 0x0f00; // reset value 0x00000F00 (docs/mc68302/scr-spec.md)

	m_sim->reset();
	m_shadow.fill(0);

	m_known_count = 0;
	m_known_unimplemented_count = 0;
	m_unknown_count = 0;
}


uint16_t mc68302_device::bar_scr_r(offs_t offset, uint16_t mem_mask)
{
	switch (offset << 1)
	{
	case 0x02: return m_bar;
	case 0x04: return m_scr_high;
	case 0x06: return m_scr_low;
	default:   return 0x0000;
	}
}

void mc68302_device::bar_scr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	switch (offset << 1)
	{
	case 0x02:
		COMBINE_DATA(&m_bar);
		install_internal_window();
		break;

	case 0x04:
	{
		// SCR high word: bits 11-8 (IPA/HWT/WPV/ADC) are write-one-to-clear
		// status; bits 6-0 excluding reserved bit 7 are plain RW; bits
		// 15-12 are reserved and always read zero. docs/mc68302/scr-spec.md.
		uint16_t new_high = m_scr_high;
		new_high &= ~(data & mem_mask & 0x0f00);
		new_high = (new_high & ~uint16_t(0x007f)) | (data & mem_mask & 0x007f);
		m_scr_high = new_high & 0x0fff;
		break;
	}

	case 0x06:
		// SCR low word: all plain RW (FRZW/FRZ2/FRZ1/SAM/HWDEN/HWDCN/
		// LPREC/LPP16/LPEN/LPCD), no status bits.
		COMBINE_DATA(&m_scr_low);
		break;

	default:
		break;
	}
}


void mc68302_device::install_internal_window()
{
	remove_internal_window();

	// The internal register window is a relocatable 4KB block; base
	// address bits 23-12 come from BAR (docs/mc68302/sib-register-map.md).
	m_window_base = (uint32_t(m_bar) & 0x0fff) << 12;
	m_s_program->install_readwrite_handler(
		m_window_base, m_window_base + 0x0fff,
		read16s_delegate(*this, FUNC(mc68302_device::internal_r)),
		write16s_delegate(*this, FUNC(mc68302_device::internal_w)));
	m_window_installed = true;
}

void mc68302_device::remove_internal_window()
{
	if (m_window_installed)
	{
		m_s_program->unmap_readwrite(m_window_base, m_window_base + 0x0fff);
		m_window_installed = false;
	}
}


// Buckets every documented-but-unimplemented-this-step register range.
// Ranges from docs/mc68302/sib-register-map.md and
// docs/mc68302/communications-block-map.md. PBCNT/PBDDR/PBDAT/FC6860 and
// BR0-3/OR0-3 are intercepted before this function is consulted (see
// internal_r/w) -- they are `known`, not `known_unimplemented`. The
// former chip-select entry (0x0830-0x083f) is gone from this table for
// that reason: every offset in that range now has an explicit case.
mc68302_device::sib_access_class mc68302_device::classify_offset(uint16_t byte_offset)
{
	if (byte_offset <= 0x07ff) return sib_access_class::known_unimplemented; // dual-port RAM / parameter RAM
	if (byte_offset <= 0x0811) return sib_access_class::known_unimplemented; // IDMA: CMR/SAPR/DAPR/BCR/CSR/FCR
	if (byte_offset <= 0x0819) return sib_access_class::known_unimplemented; // interrupt controller: GIMR/IPR/IMR/ISR
	if (byte_offset >= 0x081e && byte_offset <= 0x0823) return sib_access_class::known_unimplemented; // Port A
	if (byte_offset >= 0x0840 && byte_offset <= 0x084d) return sib_access_class::known_unimplemented; // Timer 1 + watchdog
	if (byte_offset >= 0x0850 && byte_offset <= 0x085a) return sib_access_class::known_unimplemented; // Timer 2
	if (byte_offset >= 0x0880 && byte_offset <= 0x08b5) return sib_access_class::known_unimplemented; // SCC1-3/SMC/SCP
	return sib_access_class::unknown;
}


bool mc68302_device::cs0_covers(uint32_t address) const
{
	return m_sim->cs0_covers(address);
}

void mc68302_device::set_pb_input(unsigned bit, bool level)
{
	m_sim->set_external_input(bit, level);
}


uint16_t mc68302_device::internal_r(offs_t offset, uint16_t mem_mask)
{
	const uint16_t byte_offset = uint16_t(offset << 1);

	switch (byte_offset)
	{
	case OFFSET_PBCNT:
		m_known_count++;
		return 0; // write-only in this step's model; PBCNT has no documented read-back distinct from PBDDR/PBDAT
	case OFFSET_PBDDR:
		m_known_count++;
		return 0;
	case OFFSET_PBDAT:
		m_known_count++;
		return m_sim->read_pbdat(mem_mask);
	case OFFSET_FC6860:
		m_known_count++;
		return (mem_mask & 0xff00) ? (uint16_t(m_sim->read_fc6860()) << 8) : 0x0000;
	case OFFSET_BR0: case OFFSET_BR1: case OFFSET_BR2: case OFFSET_BR3:
		m_known_count++;
		return m_sim->read_br((byte_offset - OFFSET_BR0) / 4) & mem_mask;
	case OFFSET_OR0: case OFFSET_OR1: case OFFSET_OR2: case OFFSET_OR3:
		m_known_count++;
		return m_sim->read_or((byte_offset - OFFSET_OR0) / 4) & mem_mask;
	default:
		break;
	}

	const sib_access_class cls = classify_offset(byte_offset);
	if (cls == sib_access_class::known_unimplemented)
	{
		m_known_unimplemented_count++;
		return m_shadow[offset & 0x7ff] & mem_mask;
	}
	m_unknown_count++;
	return 0x0000;
}

void mc68302_device::internal_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	const uint16_t byte_offset = uint16_t(offset << 1);

	switch (byte_offset)
	{
	case OFFSET_PBCNT:
		m_known_count++;
		m_sim->write_pbcnt(data, mem_mask);
		return;
	case OFFSET_PBDDR:
		m_known_count++;
		m_sim->write_pbddr(data, mem_mask);
		return;
	case OFFSET_PBDAT:
		m_known_count++;
		m_sim->write_pbdat(data, mem_mask);
		return;
	case OFFSET_FC6860:
		m_known_count++;
		if (mem_mask & 0xff00)
			m_sim->write_fc6860(uint8_t(data >> 8));
		return;
	case OFFSET_BR0: case OFFSET_BR1: case OFFSET_BR2: case OFFSET_BR3:
		m_known_count++;
		m_sim->write_br((byte_offset - OFFSET_BR0) / 4, data, mem_mask);
		return;
	case OFFSET_OR0: case OFFSET_OR1: case OFFSET_OR2: case OFFSET_OR3:
		m_known_count++;
		m_sim->write_or((byte_offset - OFFSET_OR0) / 4, data, mem_mask);
		return;
	default:
		break;
	}

	const sib_access_class cls = classify_offset(byte_offset);
	if (cls == sib_access_class::known_unimplemented)
	{
		m_known_unimplemented_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
	}
	else
	{
		m_unknown_count++;
	}
}
