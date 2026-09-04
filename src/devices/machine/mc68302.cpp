// license:BSD-3-Clause
// copyright-holders:
/* MC68302 minimal ASR-10-observed integration. See mc68302.h. */

#include "emu.h"
#include "mc68302.h"
#include "mc68302sim.h"

DEFINE_DEVICE_TYPE(MC68302, mc68302_device, "mc68302", "MC68302")


// Internal SIB register offsets this step recognizes by name (all other
// offsets in the documented ranges below are known-unimplemented shadow
// storage; docs/mc68302/sib-register-map.md).
static constexpr uint16_t OFFSET_PACNT  = 0x081e;
static constexpr uint16_t OFFSET_PADDR  = 0x0820;
static constexpr uint16_t OFFSET_PADAT  = 0x0822;
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

// IDMA, docs/mc68302/idma-spec.md + docs/asr10/investigations/
// idma-register-map-probe.md (measured offsets and one concrete request's
// values). SAPR/DAPR are 32-bit, stored/accessed as two 16-bit halves at
// consecutive offsets, matching the measured word-wide write pattern.
static constexpr uint16_t OFFSET_IDMA_CMR     = 0x0802;
static constexpr uint16_t OFFSET_IDMA_SAPR_HI = 0x0804;
static constexpr uint16_t OFFSET_IDMA_SAPR_LO = 0x0806;
static constexpr uint16_t OFFSET_IDMA_DAPR_HI = 0x0808;
static constexpr uint16_t OFFSET_IDMA_DAPR_LO = 0x080a;
static constexpr uint16_t OFFSET_IDMA_BCR     = 0x080c;
static constexpr uint16_t OFFSET_IDMA_CSR     = 0x080e;
static constexpr uint16_t OFFSET_IDMA_FCR     = 0x0810;

static constexpr uint16_t OFFSET_GIMR = 0x0812;
static constexpr uint16_t OFFSET_IPR  = 0x0814;
static constexpr uint16_t OFFSET_IMR  = 0x0816;
static constexpr uint16_t OFFSET_ISR  = 0x0818;

static constexpr uint16_t SCC_RX_BD_BASE[2] = { 0x0400, 0x0500 };
static constexpr uint16_t SCC_MRBLR[2]       = { 0x0482, 0x0582 };
static constexpr uint16_t SCC_SCM[2]         = { 0x0884, 0x0894 };
static constexpr uint16_t SCC_SCCE[2]        = { 0x0888, 0x0898 };
static constexpr uint16_t SCC_SCCM[2]        = { 0x088a, 0x089a };
static constexpr uint16_t SCC_IRQ_BIT[2]     = { 0x2000, 0x0400 };
static constexpr uint8_t SCC_VECTOR_LOW[2]   = { 0x0d, 0x0a };
static constexpr uint16_t SCC_IRQ_BITS       = SCC_IRQ_BIT[0] | SCC_IRQ_BIT[1];
static constexpr uint16_t IDMA_IRQ_BIT       = 0x0800;
static constexpr uint8_t IDMA_VECTOR_LOW     = 0x0b;
static constexpr uint16_t MODELED_IRQ_BITS   = SCC_IRQ_BITS | IDMA_IRQ_BIT;
static constexpr uint16_t IDMA_CMR_INTN      = 0x2000;
static constexpr uint16_t IDMA_CMR_RST       = 0x0002;
static constexpr uint16_t IDMA_CMR_STR       = 0x0001;
static constexpr uint16_t IDMA_INTERNAL_WORD = 0x37a1;


mc68302_device::mc68302_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	m68000_device(mconfig, MC68302, tag, owner, clock),
	m_bar(0),
	m_scr_high(0),
	m_scr_low(0),
	m_pa_out_cb(*this),
	m_pb_out_cb(*this),
	m_window_installed(false),
	m_window_base(0),
	m_known_count(0),
	m_internal_ram_count(0),
	m_known_unimplemented_count(0),
	m_unknown_count(0),
	m_idma_cmr(0),
	m_idma_sapr(0),
	m_idma_dapr(0),
	m_idma_bcr(0),
	m_idma_csr(0),
	m_idma_fcr(0),
	m_idma_active(false),
	m_idma_source(0),
	m_idma_dest(0),
	m_idma_remaining(0)
{
	auto bmap = address_map_constructor(FUNC(mc68302_device::bootstrap_map), this);
	m_program_config.m_internal_map = bmap;
	m_opcodes_config.m_internal_map = bmap;
	m_uprogram_config.m_internal_map = bmap;
	m_uopcodes_config.m_internal_map = bmap;

	// Allocated here, not only in device_start(): guarantees m_sim is
	// never null from construction onward, for any caller that might run
	// before this device's own device_start() completes, per MAME's
	// device start-order (owner devices start before their children).
	// The originally-cited scenario -- asr10_boot_state::machine_start()
	// calling cs0_covers() via a read_loaded_word() -- is stale:
	// read_loaded_word() does not exist anywhere in asr10_boot.cpp
	// (removed in an earlier cleanup), and machine_start() (checked
	// directly) does not call cs0_covers() or reference m_sim at all.
	// mc68302-consolidation-2.md, Del 3 item 3: confirmed the second
	// allocation this comment used to justify (device_start() below,
	// removed) was dead code, not a real reset -- m_cs[0] (cs0_covers()'s
	// only state) is never mutated between construction and
	// device_start(), since nothing writes BR0/OR0 before the SIB window
	// exists, which itself postdates device_start(). Kept as a single
	// allocation, not zero, since the early-availability guarantee this
	// comment describes may still matter for a future caller even though
	// no current one needs it before device_start().
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

	save_item(NAME(m_bar));
	save_item(NAME(m_scr_high));
	save_item(NAME(m_scr_low));
	save_item(NAME(m_window_installed));
	save_item(NAME(m_window_base));
	save_item(NAME(m_shadow));
	m_sim->register_save_items(machine().save(), *this);
	save_item(NAME(m_known_count));
	save_item(NAME(m_internal_ram_count));
	save_item(NAME(m_known_unimplemented_count));
	save_item(NAME(m_unknown_count));
	save_item(NAME(m_scc_rx_ingress));
	save_item(NAME(m_scc_rx_descriptor));

	save_item(NAME(m_idma_cmr));
	save_item(NAME(m_idma_sapr));
	save_item(NAME(m_idma_dapr));
	save_item(NAME(m_idma_bcr));
	save_item(NAME(m_idma_csr));
	save_item(NAME(m_idma_fcr));
	save_item(NAME(m_idma_active));
	save_item(NAME(m_idma_source));
	save_item(NAME(m_idma_dest));
	save_item(NAME(m_idma_remaining));
	m_idma_timer = timer_alloc(FUNC(mc68302_device::idma_internal_transfer), this);

	state_add(SCC1_RX_STATE, "SCC1RX", m_scc_rx_ingress[0]).callimport().noshow();
	state_add(SCC2_RX_STATE, "SCC2RX", m_scc_rx_ingress[1]).callimport().noshow();
}


void mc68302_device::device_stop()
{
	remove_internal_window();
}


void mc68302_device::device_post_load()
{
	m68000_device::device_post_load();

	m_sim->recompute_all_cs();
	if (m_window_installed)
	{
		m_window_base = (uint32_t(m_bar) & 0x0fff) << 12;
	}
	update_internal_irq();
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
	m_offset_access_count.fill(0);

	m_known_count = 0;
	m_internal_ram_count = 0;
	m_known_unimplemented_count = 0;
	m_unknown_count = 0;
	m_scc_rx_ingress.fill(0);
	m_scc_rx_descriptor = { SCC_RX_BD_BASE[0], SCC_RX_BD_BASE[1] };
	set_input_line(INPUT_LINE_IRQ4, CLEAR_LINE);

	m_idma_cmr = 0;
	m_idma_sapr = 0;
	m_idma_dapr = 0;
	m_idma_bcr = 0;
	m_idma_csr = 0;
	m_idma_fcr = 0;
	m_idma_active = false;
	m_idma_source = 0;
	m_idma_dest = 0;
	m_idma_remaining = 0;
	m_idma_timer->adjust(attotime::never);
}

void mc68302_device::state_import(const device_state_entry &entry)
{
	if (entry.index() == SCC1_RX_STATE || entry.index() == SCC2_RX_STATE)
	{
		const unsigned channel = entry.index() - SCC1_RX_STATE;
		scc_rx_byte(channel, m_scc_rx_ingress[channel]);
		return;
	}

	m68000_device::state_import(entry);
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
		// FIXME: partial byte writes can clear low RW bits not selected by
		// mem_mask; implement proper masked W1C/RW handling and tests.
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
	// Status (mc68302-consolidation-2.md, Del 3 item 5): BAR relocation
	// works for observed boot usage, but underlying-map restoration
	// semantics remain unverified. Partial (byte-masked) BAR writes are
	// resolved, not just assumed: sib-coverage-inventory.lua measured
	// this driver's own boot sequence writing BAR via two separate byte
	// writes (high byte then low byte, both at t~5.395s), correctly
	// combining via COMBINE_DATA to the same final value a single word
	// write had already produced -- window_base ended up correct both
	// times, confirmed by the resulting SIB coverage table. What remains
	// genuinely untested: whether unmap_readwrite() correctly restores
	// whatever the STATIC mem_map declared underneath a window_base the
	// SIB window relocates AWAY from. This driver's own BAR sequence
	// only ever relocates to $FC6000 (never elsewhere, never away from
	// it) across its whole exercised path, so that restoration path has
	// never actually run here -- untested, not proven safe.
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
	if (byte_offset <= 0x03ff) return sib_access_class::internal_ram; // dual-port RAM proper -- plain memory, not a register
	if (byte_offset <= 0x07ff) return sib_access_class::known_unimplemented; // SCC/SMC parameter RAM
	if (byte_offset <= 0x0811) return sib_access_class::known_unimplemented; // IDMA reserved bytes only (0x0800-01, 0x080f, 0x0811); CMR/SAPR/DAPR/BCR/CSR/FCR are `known`, intercepted in classify_full()/internal_r()/internal_w() before this fallback is consulted
	if (byte_offset <= 0x0819) return sib_access_class::known_unimplemented; // interrupt controller: GIMR/IPR/IMR/ISR
	if (byte_offset >= 0x081e && byte_offset <= 0x0823) return sib_access_class::known_unimplemented; // Port A
	if (byte_offset >= 0x0840 && byte_offset <= 0x084d) return sib_access_class::known_unimplemented; // Timer 1 + watchdog
	if (byte_offset >= 0x0850 && byte_offset <= 0x085a) return sib_access_class::known_unimplemented; // Timer 2
	if (byte_offset >= 0x0880 && byte_offset <= 0x08b5) return sib_access_class::known_unimplemented; // SCC1-3/SMC/SCP
	return sib_access_class::unknown;
}


// Same offset set the internal_r/w switch statements dispatch on
// explicitly -- kept in sync with those, not derived from them,
// because they're a plain switch, not a table.
mc68302_device::sib_access_class mc68302_device::classify_full(uint16_t byte_offset)
{
	switch (byte_offset)
	{
	case OFFSET_PBCNT: case OFFSET_PBDDR: case OFFSET_PBDAT: case OFFSET_FC6860:
	case OFFSET_GIMR: case OFFSET_IPR: case OFFSET_IMR: case OFFSET_ISR:
	case SCC_SCM[0]: case SCC_SCCE[0]: case SCC_SCCM[0]:
	case SCC_SCM[1]: case SCC_SCCE[1]: case SCC_SCCM[1]:
	case OFFSET_BR0: case OFFSET_OR0: case OFFSET_BR1: case OFFSET_OR1:
	case OFFSET_BR2: case OFFSET_OR2: case OFFSET_BR3: case OFFSET_OR3:
	case OFFSET_IDMA_CMR: case OFFSET_IDMA_SAPR_HI: case OFFSET_IDMA_SAPR_LO:
	case OFFSET_IDMA_DAPR_HI: case OFFSET_IDMA_DAPR_LO: case OFFSET_IDMA_BCR:
	case OFFSET_IDMA_CSR: case OFFSET_IDMA_FCR:
		return sib_access_class::known;
	default:
		return classify_offset(byte_offset);
	}
}

mc68302_device::access_class_counts mc68302_device::distinct_offset_counts() const
{
	access_class_counts result;
	for (size_t offset = 0; offset < m_offset_access_count.size(); offset++)
	{
		if (!m_offset_access_count[offset])
			continue;
		switch (classify_full(uint16_t(offset << 1)))
		{
		case sib_access_class::known: result.known++; break;
		case sib_access_class::internal_ram: result.internal_ram++; break;
		case sib_access_class::known_unimplemented: result.known_unimplemented++; break;
		case sib_access_class::unknown: result.unknown++; break;
		}
	}
	return result;
}

std::vector<mc68302_device::offset_hit> mc68302_device::top_accessed_offsets(unsigned max_entries) const
{
	std::vector<offset_hit> hits;
	hits.reserve(m_offset_access_count.size());
	for (size_t offset = 0; offset < m_offset_access_count.size(); offset++)
		if (m_offset_access_count[offset])
			hits.push_back({uint32_t(offset << 1), m_offset_access_count[offset]});

	const size_t keep = std::min<size_t>(max_entries, hits.size());
	std::partial_sort(hits.begin(), hits.begin() + keep, hits.end(),
		// TODO: add a secondary key for deterministic ordering when counts tie.
		[](const offset_hit &a, const offset_hit &b) { return a.count > b.count; });
	hits.resize(keep);
	return hits;
}


bool mc68302_device::cs0_covers(uint32_t address) const
{
	return m_sim->cs0_covers(address);
}

void mc68302_device::set_pb_input(unsigned bit, bool level)
{
	m_sim->set_external_input(bit, level);
}

uint16_t mc68302_device::pbdat_latch() const
{
	return m_sim->pbdat_latch();
}

uint16_t mc68302_device::padat_latch() const
{
	return m_sim->padat_latch();
}

void mc68302_device::handle_cp_command(uint8_t command)
{
	if (command == 0x81)
		m_scc_rx_descriptor = { SCC_RX_BD_BASE[0], SCC_RX_BD_BASE[1] };
	else if (command == 0x21)
		m_scc_rx_descriptor[0] = SCC_RX_BD_BASE[0];
	else if (command == 0x23)
		m_scc_rx_descriptor[1] = SCC_RX_BD_BASE[1];
}

void mc68302_device::update_internal_irq()
{
	uint16_t pending = m_shadow[OFFSET_IPR >> 1] & ~MODELED_IRQ_BITS;
	for (unsigned channel = 0; channel < 2; channel++)
	{
		const uint16_t events = m_shadow[SCC_SCCE[channel] >> 1];
		const uint16_t mask = m_shadow[SCC_SCCM[channel] >> 1];
		if (events & mask & 0x0100)
			pending |= SCC_IRQ_BIT[channel];
	}
	if ((m_idma_csr & 0x01) && (m_idma_cmr & IDMA_CMR_INTN))
		pending |= IDMA_IRQ_BIT;
	m_shadow[OFFSET_IPR >> 1] = pending;

	const uint16_t eligible = pending & m_shadow[OFFSET_IMR >> 1]
		& ~m_shadow[OFFSET_ISR >> 1] & MODELED_IRQ_BITS;
	set_input_line(INPUT_LINE_IRQ4, eligible ? ASSERT_LINE : CLEAR_LINE);
}

bool mc68302_device::scc_rx_byte(unsigned channel, uint8_t data)
{
	if (channel >= 2 || !m_window_installed || !BIT(m_shadow[SCC_SCM[channel] >> 1], 3))
		return false;

	const uint16_t descriptor = m_scc_rx_descriptor[channel];
	const uint16_t status = m_shadow[descriptor >> 1];
	const uint16_t length = m_shadow[(descriptor + 2) >> 1];
	const uint16_t mrblr = m_shadow[SCC_MRBLR[channel] >> 1];
	if (!(status & 0x8000) || !mrblr || length >= mrblr)
		return false;

	const uint32_t buffer = ((uint32_t(m_shadow[(descriptor + 4) >> 1]) << 16)
		| m_shadow[(descriptor + 6) >> 1]) & 0x00ffffff;
	m_s_program->write_byte(buffer + length, data);
	m_s_program->write_word(m_window_base + descriptor + 2, length + 1);

	if (length + 1 == mrblr)
	{
		m_s_program->write_word(m_window_base + descriptor, status & ~0x8000);
		m_scc_rx_descriptor[channel] = (status & 0x2000)
			? SCC_RX_BD_BASE[channel]
			: descriptor + 8;
		m_shadow[SCC_SCCE[channel] >> 1] |= 0x0100;
		update_internal_irq();
	}

	return true;
}

uint8_t mc68302_device::irq4_ack_vector()
{
	const uint16_t eligible = m_shadow[OFFSET_IPR >> 1] & m_shadow[OFFSET_IMR >> 1]
		& ~m_shadow[OFFSET_ISR >> 1] & MODELED_IRQ_BITS;
	static constexpr uint16_t priority[] = { SCC_IRQ_BIT[0], IDMA_IRQ_BIT, SCC_IRQ_BIT[1] };
	static constexpr uint8_t vector[] = { SCC_VECTOR_LOW[0], IDMA_VECTOR_LOW, SCC_VECTOR_LOW[1] };
	for (unsigned source = 0; source < std::size(priority); source++)
	{
		if (eligible & priority[source])
		{
			m_shadow[OFFSET_ISR >> 1] |= priority[source];
			update_internal_irq();
			return uint8_t((m_shadow[OFFSET_GIMR >> 1] & 0x00e0) | vector[source]);
		}
	}

	return 0x1c; // level-4 autovector when no modeled SCC source is eligible
}

TIMER_CALLBACK_MEMBER(mc68302_device::idma_internal_transfer)
{
	if (!m_idma_active || m_idma_cmr != IDMA_INTERNAL_WORD)
		return;

	while (m_idma_remaining >= 2)
	{
		const uint16_t data = m_s_program->read_word(m_idma_source);
		m_s_program->write_word(m_idma_dest, data);
		m_idma_source = (m_idma_source + 2) & 0x00ffffff;
		m_idma_dest = (m_idma_dest + 2) & 0x00ffffff;
		m_idma_remaining -= 2;
	}
	if (m_idma_remaining)
	{
		m_s_program->write_byte(m_idma_dest, m_s_program->read_byte(m_idma_source));
		m_idma_source = (m_idma_source + 1) & 0x00ffffff;
		m_idma_dest = (m_idma_dest + 1) & 0x00ffffff;
		m_idma_remaining = 0;
	}

	m_idma_sapr = m_idma_source;
	m_idma_dapr = m_idma_dest;
	m_idma_bcr = 0;
	m_idma_cmr &= ~IDMA_CMR_STR;
	m_idma_active = false;
	m_idma_csr |= 0x01;
	update_internal_irq();
}


bool mc68302_device::idma_transfer_in(uint8_t data)
{
	if (!m_idma_active || !m_idma_remaining)
		return false;

	// Destination increments (RAM buffer); source does not (this is a
	// fixed peripheral register on the real board -- see
	// docs/asr10/investigations/idma-implementation-plan.md item 4 on
	// $FC5803/SAPR). This device does not dereference SAPR at all: the
	// caller already pulled `data` from the source device directly (e.g.
	// upd765_family_device::dma_r()), matching this step's deliberate
	// choice not to implement bus-mastering/arbitration
	// (docs/mc68302/idma-spec.md's own caveat against a transfer engine
	// that "silently appears to work" without it).
	m_s_program->write_byte(m_idma_dest, data);
	m_idma_dest++;
	m_idma_remaining--;

	if (!m_idma_remaining)
	{
		m_idma_active = false;
		m_idma_csr |= 0x01; // DONE/success, docs/mc68302/idma-spec.md CSR bit 0
		return true;
	}
	return false;
}


uint16_t mc68302_device::internal_r(offs_t offset, uint16_t mem_mask)
{
	const uint16_t byte_offset = uint16_t(offset << 1);
	m_offset_access_count[offset & 0x7ff]++;

	switch (byte_offset)
	{
	case OFFSET_PACNT:
		m_known_count++;
		return m_sim->pacnt_latch() & mem_mask;
	case OFFSET_PADDR:
		m_known_count++;
		return m_sim->paddr_latch() & mem_mask;
	case OFFSET_PADAT:
		m_known_count++;
		return m_sim->read_padat(mem_mask);
	case OFFSET_PBCNT:
		m_known_count++;
		// FIXME: m_sim stores PBCNT/PBDDR, but readback is not implemented;
		// do not treat these reads as verified register semantics yet.
		return 0; // write-only in this step's model; PBCNT has no documented read-back distinct from PBDDR/PBDAT
	case OFFSET_PBDDR:
		m_known_count++;
		return 0;
	case OFFSET_PBDAT:
		m_known_count++;
		return m_sim->read_pbdat(mem_mask);
	case OFFSET_GIMR: case OFFSET_IPR: case OFFSET_IMR: case OFFSET_ISR:
	case SCC_SCM[0]: case SCC_SCCE[0]: case SCC_SCCM[0]:
	case SCC_SCM[1]: case SCC_SCCE[1]: case SCC_SCCM[1]:
		m_known_count++;
		return m_shadow[offset & 0x7ff] & mem_mask;
	case OFFSET_FC6860:
		m_known_count++;
		// NOTE: busy-bit timing is ASR-10-observed and experimental, not
		// verified general MC68302 semantics; isolate as machine policy.
		return (mem_mask & 0xff00) ? (uint16_t(m_sim->read_fc6860()) << 8) : 0x0000;
	case OFFSET_BR0: case OFFSET_BR1: case OFFSET_BR2: case OFFSET_BR3:
		m_known_count++;
		return m_sim->read_br((byte_offset - OFFSET_BR0) / 4) & mem_mask;
	case OFFSET_OR0: case OFFSET_OR1: case OFFSET_OR2: case OFFSET_OR3:
		m_known_count++;
		return m_sim->read_or((byte_offset - OFFSET_OR0) / 4) & mem_mask;
	case OFFSET_IDMA_CMR:
		m_known_count++;
		return m_idma_cmr & mem_mask;
	case OFFSET_IDMA_SAPR_HI:
		m_known_count++;
		return uint16_t(m_idma_sapr >> 16) & mem_mask;
	case OFFSET_IDMA_SAPR_LO:
		m_known_count++;
		return uint16_t(m_idma_sapr) & mem_mask;
	case OFFSET_IDMA_DAPR_HI:
		m_known_count++;
		return uint16_t(m_idma_dapr >> 16) & mem_mask;
	case OFFSET_IDMA_DAPR_LO:
		m_known_count++;
		return uint16_t(m_idma_dapr) & mem_mask;
	case OFFSET_IDMA_BCR:
		m_known_count++;
		return m_idma_bcr & mem_mask;
	case OFFSET_IDMA_CSR:
		m_known_count++;
		// CSR is documented 8-bit at the even byte of this word
		// (docs/mc68302/idma-spec.md); measured reads used mask=$FF00.
		return (mem_mask & 0xff00) ? (uint16_t(m_idma_csr) << 8) : 0x0000;
	case OFFSET_IDMA_FCR:
		m_known_count++;
		return (mem_mask & 0xff00) ? (uint16_t(m_idma_fcr) << 8) : 0x0000;
	default:
		break;
	}

	const sib_access_class cls = classify_offset(byte_offset);
	if (cls == sib_access_class::internal_ram)
	{
		m_internal_ram_count++;
		return m_shadow[offset & 0x7ff] & mem_mask;
	}
	if (cls == sib_access_class::known_unimplemented)
	{
		m_known_unimplemented_count++;
		// NOTE: this readback is shadow storage only; implemented register
		// side effects are intentionally deferred.
		return m_shadow[offset & 0x7ff] & mem_mask;
	}
	m_unknown_count++;
	return 0x0000;
}

void mc68302_device::internal_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	const uint16_t byte_offset = uint16_t(offset << 1);
	m_offset_access_count[offset & 0x7ff]++;

	switch (byte_offset)
	{
	case OFFSET_PACNT:
		m_known_count++;
		m_sim->write_pacnt(data, mem_mask);
		return;
	case OFFSET_PADDR:
		m_known_count++;
		m_sim->write_paddr(data, mem_mask);
		return;
	case OFFSET_PADAT:
	{
		m_known_count++;
		const uint16_t old_val = m_sim->padat_latch();
		m_sim->write_padat(data, mem_mask);
		const uint16_t new_val = m_sim->padat_latch();
		if (new_val != old_val)
			m_pa_out_cb(new_val);
		return;
	}
	case OFFSET_PBCNT:
		m_known_count++;
		m_sim->write_pbcnt(data, mem_mask);
		return;
	case OFFSET_PBDDR:
		m_known_count++;
		m_sim->write_pbddr(data, mem_mask);
		return;
	case OFFSET_PBDAT:
	{
		m_known_count++;
		const uint16_t old_val = m_sim->pbdat_latch();
		m_sim->write_pbdat(data, mem_mask);
		const uint16_t new_val = m_sim->pbdat_latch();
		if (new_val != old_val)
			m_pb_out_cb(new_val);
		return;
	}
	case OFFSET_FC6860:
		m_known_count++;
		if (mem_mask & 0xff00)
		{
			const uint8_t command = uint8_t(data >> 8);
			m_sim->write_fc6860(command);
			handle_cp_command(command);
		}
		return;
	case OFFSET_GIMR:
		m_known_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
		return;
	case OFFSET_IPR:
		m_known_count++;
		m_shadow[offset & 0x7ff] &= ~(data & mem_mask);
		update_internal_irq();
		return;
	case OFFSET_IMR:
		m_known_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
		update_internal_irq();
		return;
	case OFFSET_ISR:
		m_known_count++;
		m_shadow[offset & 0x7ff] &= ~(data & mem_mask);
		update_internal_irq();
		return;
	case SCC_SCCE[0]: case SCC_SCCE[1]:
		m_known_count++;
		m_shadow[offset & 0x7ff] &= ~(data & mem_mask & 0xff00);
		update_internal_irq();
		return;
	case SCC_SCCM[0]: case SCC_SCCM[1]:
		m_known_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
		update_internal_irq();
		return;
	case SCC_SCM[0]: case SCC_SCM[1]:
		m_known_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
		return;
	case OFFSET_BR0: case OFFSET_BR1: case OFFSET_BR2: case OFFSET_BR3:
		m_known_count++;
		m_sim->write_br((byte_offset - OFFSET_BR0) / 4, data, mem_mask);
		return;
	case OFFSET_OR0: case OFFSET_OR1: case OFFSET_OR2: case OFFSET_OR3:
		m_known_count++;
		m_sim->write_or((byte_offset - OFFSET_OR0) / 4, data, mem_mask);
		return;
	case OFFSET_IDMA_CMR:
	{
		m_known_count++;
		COMBINE_DATA(&m_idma_cmr);
		if (m_idma_cmr & IDMA_CMR_RST)
		{
			m_idma_timer->adjust(attotime::never);
			m_idma_active = false;
			m_idma_remaining = 0;
			m_idma_csr = 0;
			update_internal_irq();
			return;
		}
		// STR, measured: clear ($0002) in the shared vector-$51 prelude
		// write that recurs at every IACK regardless of transfer state,
		// set ($0D51, bit 0) only in the write immediately before READ
		// DATA -- the one bit in CMR with a clean, repeatable on/off
		// correlation to "this write starts a transfer"
		// (docs/asr10/investigations/idma-implementation-plan.md item 6).
		// $0D51 keeps the established external fixed-source byte feed.
		// Only the independently observed $37A1 sampling form gets an
		// internal incrementing word transfer; no wider CMR decoder is
		// inferred from these two values.
		if ((m_idma_cmr & IDMA_CMR_STR) && m_idma_bcr > 0)
		{
			m_idma_source = m_idma_sapr;
			m_idma_dest = m_idma_dapr;
			m_idma_remaining = (m_idma_cmr == IDMA_INTERNAL_WORD)
				? uint32_t(m_idma_bcr)
				: uint32_t(m_idma_bcr) - 1;
			m_idma_active = true;
			m_idma_csr = 0;
			update_internal_irq();
			if (m_idma_cmr == IDMA_INTERNAL_WORD)
				m_idma_timer->adjust(attotime::zero);
		}
		return;
	}
	case OFFSET_IDMA_SAPR_HI:
		m_known_count++;
		m_idma_sapr = (m_idma_sapr & 0x0000ffff) | (uint32_t(data & mem_mask) << 16);
		return;
	case OFFSET_IDMA_SAPR_LO:
		m_known_count++;
		m_idma_sapr = (m_idma_sapr & 0xffff0000) | uint32_t(data & mem_mask);
		return;
	case OFFSET_IDMA_DAPR_HI:
		m_known_count++;
		m_idma_dapr = (m_idma_dapr & 0x0000ffff) | (uint32_t(data & mem_mask) << 16);
		return;
	case OFFSET_IDMA_DAPR_LO:
		m_known_count++;
		m_idma_dapr = (m_idma_dapr & 0xffff0000) | uint32_t(data & mem_mask);
		return;
	case OFFSET_IDMA_BCR:
		m_known_count++;
		COMBINE_DATA(&m_idma_bcr);
		return;
	case OFFSET_IDMA_CSR:
		m_known_count++;
		// CSR is an event register, write-one-to-clear per the manual's
		// general event-register rule (docs/mc68302/idma-spec.md).
		if (mem_mask & 0xff00)
			m_idma_csr &= ~uint8_t(data >> 8);
		update_internal_irq();
		return;
	case OFFSET_IDMA_FCR:
		m_known_count++;
		if (mem_mask & 0xff00)
			m_idma_fcr = uint8_t(data >> 8);
		return;
	default:
		break;
	}

	const sib_access_class cls = classify_offset(byte_offset);
	if (cls == sib_access_class::internal_ram)
	{
		m_internal_ram_count++;
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
	}
	else if (cls == sib_access_class::known_unimplemented)
	{
		m_known_unimplemented_count++;
		// NOTE: documented register treated as RAM for temporary plumbing;
		// firmware probes may succeed without real device semantics.
		COMBINE_DATA(&m_shadow[offset & 0x7ff]);
	}
	else
	{
		m_unknown_count++;
	}
}
