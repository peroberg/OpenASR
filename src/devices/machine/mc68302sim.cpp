// license:BSD-3-Clause
// copyright-holders:
/* MC68302 SIM module -- fas 3 steg 1. See mc68302sim.h. */

#include "emu.h"
#include "mc68302sim.h"


void mc68302_device::mc68302_sim::reset()
{
	m_pbcnt = PBCNT_RESET;
	m_pbddr = 0x0000;
	m_pbdat = 0x0000;
	m_pb_external_input = 0x0000;
	m_fc6860 = 0x00;
	m_fc6860_reads_since_write = 0;

	m_br[0] = CS0_RESET_BR;
	m_or[0] = CS_RESET_OR;
	for (unsigned index = 1; index < 4; index++)
	{
		m_br[index] = CS1_3_RESET_BR;
		m_or[index] = CS_RESET_OR;
	}
	for (unsigned index = 0; index < 4; index++)
		recompute_cs(index);
}


void mc68302_device::mc68302_sim::write_pbcnt(uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_pbcnt);
}

void mc68302_device::mc68302_sim::write_pbddr(uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_pbddr);
}

uint16_t mc68302_device::mc68302_sim::read_pbdat(uint16_t mem_mask) const
{
	// PB7-PB0: PBCNT bit clear selects GPIO, set selects the dedicated
	// function (docs/mc68302/pin-function-map.md). PB11-PB8 are always
	// GPIO. PBDDR bit set selects output for a GPIO pin. An output bit
	// reads back its own latch (real hardware behavior for an output
	// pin with nothing contending it). A GPIO input bit reads back
	// whatever the driver last supplied via set_external_input() --
	// real board wiring, not 68302-internal state -- and defaults to 0
	// (undriven) until the driver says otherwise. A dedicated-function
	// pin this device doesn't implement also reads 0.
	const uint16_t gpio_mask = uint16_t(~m_pbcnt) | 0x0f00; // PB11-PB8 always GPIO
	const uint16_t output_bits = m_pbddr & gpio_mask;
	const uint16_t input_bits = gpio_mask & ~m_pbddr;
	return ((m_pbdat & output_bits) | (m_pb_external_input & input_bits)) & mem_mask;
}

void mc68302_device::mc68302_sim::write_pbdat(uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_pbdat);
}

void mc68302_device::mc68302_sim::set_external_input(unsigned bit, bool level)
{
	if (level)
		m_pb_external_input |= uint16_t(1u << bit);
	else
		m_pb_external_input &= ~uint16_t(1u << bit);
}


uint8_t mc68302_device::mc68302_sim::read_fc6860()
{
	if (m_fc6860_reads_since_write < FC6860_READ_DELAY)
	{
		m_fc6860_reads_since_write++;
		return m_fc6860;
	}
	return m_fc6860 & ~uint8_t(0x01);
}

void mc68302_device::mc68302_sim::write_fc6860(uint8_t data)
{
	m_fc6860 = data;
	m_fc6860_reads_since_write = 0;
}


// BR fields, bit-verified against docs/mc68302/sib-register-map.md's
// reset CS0 (0xc001 -> FC=6, base=0, RW=read, EN=yes) and CS0-final
// (0x1f01 -> base=0xf80000) rows:
//   bits 15-13: FC2-0 (function code compare value -- stored, not
//               enforced; see file header)
//   bits 12-2:  BASE ADDRESS, 11 bits covering A23-A13; physical base
//               is this field shifted left by 13
//   bit 1:      RW compare value (0=read, 1=write -- stored, not enforced)
//   bit 0:      EN
void mc68302_device::mc68302_sim::write_br(unsigned index, uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_br[index]);
	recompute_cs(index);
}

// OR fields, bit-verified the same way (reset CS0 0xdffd -> DTACK=6,
// mask=0x7ff i.e. no bits masked out, size=8 KiB; CS0-final 0x3f82 ->
// DTACK=1, mask has its low 5 bits clear, size=8 KiB<<5=256 KiB):
//   bits 15-13: DTACK (wait state count, 7=external -- stored, not enforced)
//   bits 12-2:  BASE ADDRESS MASK, 11 bits; a 1 bit means "compare this
//               address bit", a 0 bit means "don't care". The mask is
//               always a contiguous run of don't-care bits from the
//               low end (A13 upward), so block size is 8 KiB left-shifted
//               by however many of those low bits are clear.
//   bit 1:      MRW (stored, not enforced)
//   bit 0:      CFC (stored, not enforced -- see file header; the
//               ASR-10-observed values for this bit do not follow a
//               consistent per-CS pattern in sib-register-map.md, which
//               itself flags CFC/MRW enforcement as unmodeled)
void mc68302_device::mc68302_sim::write_or(unsigned index, uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_or[index]);
	recompute_cs(index);
}

void mc68302_device::mc68302_sim::recompute_cs(unsigned index)
{
	cs_decode &decode = m_cs[index];
	decode.enabled = BIT(m_br[index], 0);
	const uint32_t base_field = (m_br[index] >> 2) & 0x7ff;
	decode.base = base_field << 13;

	const uint16_t mask_field = (m_or[index] >> 2) & 0x7ff;
	unsigned dont_care_bits = 0;
	while (dont_care_bits < 11 && !BIT(mask_field, dont_care_bits))
		dont_care_bits++;
	// TODO: verify whether decode.base should be normalized by the OR
	// don't-care mask. This currently models only an address interval,
	// not the full FC/RW/CFC/MRW/DTACK chip-select decision.
	const uint32_t size = 0x2000u << dont_care_bits;
	decode.end = decode.base + size;
}
