// license:BSD-3-Clause
// copyright-holders:

/***************************************************************************
 *
 *     Ensoniq ASR-10 boot harness
 *
 *	Conservative upstream-MAME-oriented ASR-10 1.5B ROM boot model.
 *	This is not a full ASR-10 driver.
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
 *	- high-runtime ROM alias window
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
#include "main.h"

#include "bus/midi/midi.h"
#include "cpu/m68000/m68000.h"
#include "imagedev/floppy.h"
#include "machine/mc68302.h"
#include "machine/mc68681.h"
#include "machine/upd765.h"

#include "esqpanel.h"
#include "formats/esq16_dsk.h"
#include "sound/es5506.h"
#include "sound/esqpump.h"
#include "cpu/es5510/es5510.h"
#include "formats/hxchfe_dsk.h"
#include "speaker.h"

#include "asr10_boot.lh"

#include <algorithm>
#include <array>

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
		, m_panel(*this, "panel")
		, m_mdout(*this, "mdout")
		, m_rom(*this, "maincpu")
		, m_es5506_host(*this, "es5506_host")
		, m_es5510_host(*this, "es5510_host")
		, m_pump(*this, "pump")
		, m_sample_ram(*this, ":asr10_sample_ram")
	{
	}

	void asr10_boot(machine_config &config) ATTR_COLD;

private:
	void es5506_wavetable_map(address_map &map) ATTR_COLD;
	void es5506_unpopulated_wavetable_map(address_map &map) ATTR_COLD;
	void es5506_wavetable_bank1_map(address_map &map) ATTR_COLD;
	static constexpr u32 ROM_MASK = 0x0003ffff;
	static constexpr u32 LOWMEM_WORDS = 0x00100000 / 2;
	// docs/asr10/investigations/memory-size-belief-analysis.md +
	// base-relocation follow-up: ROM's own alias probe ($F8A166-$F8A244,
	// disassembled) computes its allocator base as reported_base+$10000.
	// For the 2 MB branch reported_base is $600000, not $000000 -- so the
	// wraparound below must cover the *whole* $200000-$EFFFFF window, not
	// just the four probe bytes, or firmware's own subsequent allocator
	// use of $610000+ lands on nothing. $600000 mod SYSTEM_RAM_BYTES == 0
	// for a power-of-two size, so a uniform wrap folds it back into the
	// same already-backed image. Stage A hardcodes the stock 2 MB size;
	// Stage B replaces this with the configured ram_device size.
	static constexpr u32 SYSTEM_RAM_BYTES = 0x00200000;
	// [Likely functional policy] The verified effect operating-mode commit
	// selects the two current-MAME ES5506 device-domain rates.  These values
	// match the board oscillator inventory, but this is not a claim about the
	// physical ASR clock mux, divider, ES5701, or pin routing.
	static constexpr u32 AUDIO_RATE_MODE0_CLOCK = 30'476'180;
	static constexpr u32 AUDIO_RATE_MODE1_CLOCK = 33'868'800;

	static constexpr bool ASR10_MISSING_FDC_RATE_SOURCE = true;

	required_device<mc68302_device> m_maincpu;
	required_device<upd72069_device> m_fdc;
	required_device<floppy_connector> m_floppy_connector;
	required_device<scn2681_device> m_duart;
	required_device<asr10panel_device> m_panel;
	required_device<midi_port_device> m_mdout;
	required_memory_region m_rom;

	optional_device<es5506_device> m_es5506_host;
	optional_device<es5510_device> m_es5510_host;
	optional_device<esq_5505_5510_pump_device> m_pump;
	// Same backing store as mem_map's own $100000-$1FFFFF (".share()"),
	// bound here so system_ram_alias_r/w can dispatch into it directly.
	required_shared_ptr<u16> m_sample_ram;

	emu_timer *m_lrclk_timer = nullptr;
	emu_timer *m_pump_release_timer = nullptr;
	bool m_lrclk_level = false;
	emu_timer *m_idma_tc_timer = nullptr;

	std::unique_ptr<u16[]> m_lowmem_shadow;
	// Raw, right-justified 10-bit board sources for the ES5506 PAR callback.
	// Source selection is PBDAT PB2-PB0, not DUART OPR; see
	// docs/asr10/investigations/analog-selector-control-map-v350.md.
	std::array<u16, 8> m_analog_values{};
	u8 m_effect_audio_mode = 0;

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void mem_map(address_map &map) ATTR_COLD;
	void cpu_space_map(address_map &map) ATTR_COLD;

	u16 low_rom_or_lowmem_r(offs_t offset, u16 mem_mask = ~0);
	void lowmem_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void apply_effect_audio_rate_policy();
	void effect_audio_rate_postload();
	void es5506_frame_rate_changed(u32 rate);
	void es5510_program_commit();
	TIMER_CALLBACK_MEMBER(pump_release);
	u16 system_ram_alias_r(offs_t offset, u16 mem_mask = ~0);
	void system_ram_alias_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 high_alias_r(offs_t offset, u16 mem_mask = ~0);
	void high_alias_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 upd72069_fdc_r(offs_t offset, u16 mem_mask = ~0);
	void upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 scsi_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void scsi_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(lrclk_toggle);
	u8 maincpu_iack_r(u8 level);
	void idma_drq_w(int state);
	TIMER_CALLBACK_MEMBER(idma_tc_deliver);


	u16 analog_r();
	void analog_w(offs_t offset, u16 data);
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


	static void floppy_drives(device_slot_interface &device);
	static void floppy_formats(format_registration &fr);
};


void asr10_boot_state::machine_start()
{
	m_lrclk_timer = timer_alloc(FUNC(asr10_boot_state::lrclk_toggle), this);
	m_pump_release_timer = timer_alloc(FUNC(asr10_boot_state::pump_release), this);
	m_idma_tc_timer = timer_alloc(FUNC(asr10_boot_state::idma_tc_deliver), this);
	m_lowmem_shadow = make_unique_clear<u16[]>(LOWMEM_WORDS);

	save_pointer(NAME(m_lowmem_shadow), LOWMEM_WORDS);
	save_item(NAME(m_lrclk_level));
	save_item(NAME(m_analog_values));
	save_item(NAME(m_effect_audio_mode));
	machine().save().register_postload(save_prepost_delegate(FUNC(asr10_boot_state::effect_audio_rate_postload), this));
}


void asr10_boot_state::machine_reset()
{
	m_effect_audio_mode = 0;
	apply_effect_audio_rate_policy();
	m_analog_values[0] = 0x200; // Pitch wheel, boot-calibrated center
	m_analog_values[1] = 0x200; // ASR-88 conditional source; no ASR-10 label
	m_analog_values[2] = 0x200; // Mod wheel
	m_analog_values[3] = 0x3ff; // Volume
	m_analog_values[4] = 0x200; // Pedal/CV
	m_analog_values[5] = 0x200; // MR. KNOB / Data Entry
	m_analog_values[6] = 0x200; // No producer in the analyzed V3.50 path
	m_analog_values[7] = 0x300; // Stable calibration reference: viewer >= 190

	// Board-level LRCLK into PB3 (GPIO input, docs/mc68302/pin-function-map.md):
	// external to the 68302, always running once the machine is up, not a
	// register-driven behavior. The rate is provisionally derived from the
	// board's 33.8688 MHz audio clock as 768 x 44.1 kHz; this tree has not
	// separately measured the divider source.
	m_lrclk_level = false;
	m_lrclk_timer->adjust(attotime::from_hz(44100), 0, attotime::from_hz(44100));
	std::fill_n(m_lowmem_shadow.get(), LOWMEM_WORDS, 0);

}

void asr10_boot_state::mem_map(address_map &map)
{
	// Low boot region. Reads normally come from ROM; writes are shadowed for
	// the boot-time remap/low-memory behavior used by the ROM.
	map(0x000000, 0x0fffff).rw(FUNC(asr10_boot_state::low_rom_or_lowmem_r), FUNC(asr10_boot_state::lowmem_w));

	// ROM's own memory-size probe (docs/asr10/investigations/
	// memory-size-belief-analysis.md, ROM $F8A166-$F8A244) reads
	// $008000/$408000/$808000/$C08000 to detect real address-line
	// aliasing. Its own subsequent allocator math ($C4E.l = reported_base
	// + $10000) can also land anywhere up to $F7FFFF depending on which
	// branch it takes (reported_base is $600000 for the 2 MB branch, not
	// $000000) -- so the whole $200000-$F7FFFF window, not just the four
	// probe bytes, must show the same wraparound. Folds into the same
	// backing as $000000-$1FFFFF above; see system_ram_alias_r/w.
	//
	// Bugfix, docs/asr10/investigations/stereo-round-trip-verification.md:
	// this line originally read $200000-$EFFFFF, one byte short of the
	// $F00000-$F7FFFF range the prior $0xf00000,0xf7ffff .ram() line it
	// replaced actually covered. $F00000-$F7FFFF is exactly where both
	// SCC receive buffers live ($F76600 channel 1, $F74B00 channel 2), so
	// the gap silently discarded both channels' recorded bytes -- caught
	// only once a stereo round-trip test actually compared byte content,
	// since no existing regression test read back what it recorded.
	map(0x200000, 0xf7ffff).rw(FUNC(asr10_boot_state::system_ram_alias_r), FUNC(asr10_boot_state::system_ram_alias_w));

	// Reference-based candidate windows that are not device implementations
	// yet stay passive unless a real device is mapped below.
	//
	// sample-ram-and-voice-registers.md / its numeric follow-up: 524,290
	// writes land here once, entirely before FILE 1 (a boot-time clear of
	// the whole megabyte -- firmware does not clear memory it has no use
	// for), then zero for the rest of the run; voice 0's own END/ACCUM
	// (word address $20000, i.e. bank 0's own $000000-$1FFFFF space)
	// converts to CPU byte address $140000 -- inside this exact range.
	// .share() here is the narrow, single-purpose exception to "no
	// mem_map change before E2 is settled": it adds and removes no
	// decoding, only names this existing allocation so
	// es5506_wavetable_map's own bank-0 window can reference the same
	// backing store instead of its own separate, disconnected one.
	map(0x100000, 0x1fffff).ram().share(":asr10_sample_ram"); // sample RAM candidate, directly tested by the boot ROM at 0x100000 -- shared with ES5506 bank 0, see es5506_wavetable_map

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
	// M68000 interrupt acknowledge cycles. IRQ6 is supplied by the MC68302
	// model; other levels preserve default autovector behavior.
	map(0xfffff3, 0xfffff3).lr8(NAME([this]() { return maincpu_iack_r(1); }));
	map(0xfffff5, 0xfffff5).lr8(NAME([this]() { return maincpu_iack_r(2); }));
	map(0xfffff7, 0xfffff7).lr8(NAME([this]() { return maincpu_iack_r(3); }));
	map(0xfffff9, 0xfffff9).lr8(NAME([this]() { return m_maincpu->irq4_ack_vector(); }));
	map(0xfffffb, 0xfffffb).lr8(NAME([this]() { return maincpu_iack_r(5); }));
	map(0xfffffd, 0xfffffd).lr8(NAME([this]() { return maincpu_iack_r(6); }));
	map(0xffffff, 0xffffff).lr8(NAME([this]() { return maincpu_iack_r(7); }));
}


void asr10_boot_state::es5506_wavetable_map(address_map &map)
{
	// Bank 0's first $80000 words ($000000-$07FFFF, word-addressed --
	// es5506_device's own m_bank0_config is (ENDIANNESS_BIG, 16, 21, -1))
	// is exactly 0x100000 bytes: the same size, width and endianness as
	// mem_map's $100000-$1FFFFF, verified against MAME's own share-size
	// check (emumem_aspace.cpp: prepare_map_generic()'s share->compare()
	// fatalerrors on any mismatch) before writing this. Shared via a
	// root-relative tag (":asr10_sample_ram", matching the cross-device
	// .share() pattern in fd1089.cpp -- a plain tag would resolve
	// per-device via device_t::subtag() and silently create two
	// disconnected allocations instead of one shared block) so this is
	// the *same* backing store as mem_map's own $100000-$1FFFFF, not a
	// private copy. sample-ram-and-voice-registers.md's numeric
	// follow-up: voice 0's own END/ACCUM already resolves into this exact
	// range. The remaining $080000-$1FFFFF stays private/unshared,
	// unchanged from before.
	map(0x000000, 0x07ffff).ram().share(":asr10_sample_ram");
	map(0x080000, 0x1fffff).ram();
}

void asr10_boot_state::es5506_unpopulated_wavetable_map(address_map &map)
{
	map(0x000000, 0x1fffff).noprw();
}

void asr10_boot_state::es5506_wavetable_bank1_map(address_map &map)
{
	// keyboard-and-sample-bridge-6.md: voice 1/2's own programmed
	// START/END, read back after a real note-on with the instrument
	// selected, decode (bank field bits 14-15 of CR = 1, per
	// es5506.h/.cpp's own get_bank()) to a bank-relative word range that
	// lands entirely inside $000944-$0552FF -- the exact CPU lowmem
	// range the loaded 172,544-byte instrument payload is already known
	// to occupy (file-loaded-verification-probe.md). Bank 1 is CPU
	// lowmem, not a second sample-RAM pool. Reuses mem_map's own
	// low_rom_or_lowmem_r/lowmem_w handlers directly -- same backing
	// store (m_lowmem_shadow), not a private copy -- word range
	// $000000-$07FFFF matches mem_map's $000000-$0FFFFF (1MB) exactly,
	// same technique already validated for bank 0's $100000-$1FFFFF
	// share. mem_map() itself is untouched.
	//
	// Known simplification (keyboard-and-sample-bridge-7.md): reusing
	// low_rom_or_lowmem_r gives ES5506 bank 1 the same ROM overlay
	// mem_map's CPU side sees during boot (cs0_covers(0)). Real
	// hardware's ES5506 sample bus faces DRAM only, never the ROM
	// overlay -- this driver's bank 1 is momentarily wider than the
	// real chip's view. Harmless for playback (notes only trigger
	// post-boot, once the overlay is long gone and cs0_covers(0) is
	// false), but written down per project policy on known
	// simplifications.
	map(0x000000, 0x07ffff).rw(FUNC(asr10_boot_state::low_rom_or_lowmem_r), FUNC(asr10_boot_state::lowmem_w));
}


u8 asr10_boot_state::maincpu_iack_r(u8 level)
{
	if (level == 6)
		return m_maincpu->irq6_ack_vector();
	if (level == 1)
		return m_maincpu->irq1_ack_vector();

	return m68000_base_device::autovector(level);
}


// Minimal IDMA board policy (docs/asr10/investigations/
// idma-implementation-plan.md): board-side glue only. The generic
// register/transfer/count bookkeeping lives on mc68302_device
// (idma_transfer_in()); this driver just supplies FDC-specific knowledge
// -- pull a byte via dma_r() (not the CPU-visible FIFO port) and assert
// terminal count once the channel reports the transfer complete, so the
// FDC's own live transfer state machine ends the command and moves to
// result phase (upd765_family_device::tc_w()). Without the tc_w() call,
// upd765.cpp's own sector-boundary check (command[4]==command[6], i.e.
// R==EOT, guarded by `if(!tc_done)`) reports abnormal termination/
// end-of-cylinder even after every byte has been correctly transferred --
// exactly the "overrun again, later" failure mode this was written to
// avoid; see docs/asr10/investigations/idma-implementation-plan.md.
//
// MUST check idma_channel_active() before touching the FDC at all:
// upd765_family_device::enable_transfer() asserts DRQ on every transfer,
// PIO included -- there is no `else` between its PIO/internal_drq branch
// and its unconditional `if(!drq) set_drq(true)`. An earlier version of
// this handler called dma_r() unconditionally on every DRQ assertion and
// broke plain boot-to-FILE1 (regression fell to 1/5, "PLEASE INSERT DISK"
// even with media mounted): it was silently popping bytes out of the
// FDC's FIFO during the boot's own unrelated polled READ DATA transfers,
// desyncing the CPU's own fifo_r() reads of the same data. Caught by the
// regression suite, reverted, root-caused against upd765.cpp before
// retrying -- not guessed around.
//
// tc_w() is NOT called synchronously here -- docs/asr10/investigations/
// tc-reentrancy-probe.md. enable_transfer() (which asserts DRQ, reaching
// this handler) is called from fifo_push(), itself called from the live
// MFM-decode path inside upd765_family_device::live_run()'s own for(;;)
// loop. Calling tc_w() from here would call live_sync(), which can call
// rollback()+live_run() again -- reentrant, while the outer live_run()
// invocation that led to this exact call chain is still on the stack,
// mid-iteration, with cur_live partially updated for the byte just
// delivered. Measured effect: main_phase got stuck at PHASE_EXEC forever
// (MSR read $10 continuously, never $D0/PHASE_RESULT) -- command_end()
// (which sets irq=true and calls check_irq()) never ran, so INTRQ was
// never asserted at all, not just undelivered; firmware's own ~5s
// timeout then fired. dma_r() stays synchronous, unchanged, per
// instruction -- one variable at a time. Only tc_w() moves to a
// zero-delay timer, so it runs on its own call stack outside live_run()
// entirely, the way a real DMA controller's independent TC bus line
// would reach the FDC rather than as a nested function call.
void asr10_boot_state::idma_drq_w(int state)
{
	if (!state || !m_maincpu->idma_channel_active())
		return;

	const u8 data = m_fdc->dma_r();
	if (m_maincpu->idma_transfer_in(data))
		m_idma_tc_timer->adjust(attotime::zero);
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::idma_tc_deliver)
{
	m_fdc->tc_w(true);
	m_fdc->tc_w(false);
}


u16 asr10_boot_state::low_rom_or_lowmem_r(offs_t offset, u16 mem_mask)
{
	if (!m_maincpu->cs0_covers(0))
		return m_lowmem_shadow[offset] & mem_mask;

	const u32 byte_address = offset << 1;
	const u8 *rom = m_rom->base();
	const u32 rom_offset = byte_address & ROM_MASK;
	return (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
}


TIMER_CALLBACK_MEMBER(asr10_boot_state::lrclk_toggle)
{
	m_lrclk_level = !m_lrclk_level;
	m_maincpu->set_pb_input(3, m_lrclk_level);
}

u16 asr10_boot_state::analog_r()
{
	const u8 selector = m_maincpu->pbdat_latch() & 7;
	return m_analog_values[selector] & 0x03ff;
}

void asr10_boot_state::analog_w(offs_t offset, u16 data)
{
	m_analog_values[offset & 7] = data & 0x03ff;
}

// ES5510 host select/commit wrappers. FC3100-FC3101, FC3140-FC3141 and FC3180-FC3181 are
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
	m_es5510_host->host_w(0x80, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xa0);
}

void asr10_boot_state::es5510_host_write_select_gpr_w(offs_t offset, u8 data)
{
	m_es5510_host->host_w(0xa0, data);
}

u8 asr10_boot_state::es5510_host_write_select_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xc0);
}

void asr10_boot_state::es5510_host_write_select_instr_w(offs_t offset, u8 data)
{
	es5510_program_commit();
	m_es5510_host->host_w(0xc0, data);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_instr_r(offs_t offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), 0xe0);
}

void asr10_boot_state::es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data)
{
	es5510_program_commit();
	m_es5510_host->host_w(0xe0, data);
}

void asr10_boot_state::es5510_program_commit()
{
	if (m_pump)
	{
		m_pump->set_esp_halted(true);
		m_pump_release_timer->adjust(attotime::from_msec(10));
	}
}

void asr10_boot_state::lowmem_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_lowmem_shadow[offset]);

	// `$0CE3` is the PC-correlated current-effect operating-mode byte.  Its
	// values 0/1 drive the separately verified ACTV and pitch setup branches;
	// this board-level policy adds only the surviving current-MAME rate relation.
	if (offset == (0x0ce2 / 2) && ACCESSING_BITS_0_7)
	{
		const u8 mode = m_lowmem_shadow[offset] & 0xff;
		if (mode <= 1)
		{
			m_effect_audio_mode = mode;
			apply_effect_audio_rate_policy();
		}
	}
}

void asr10_boot_state::apply_effect_audio_rate_policy()
{
	const u32 clock = m_effect_audio_mode ? AUDIO_RATE_MODE1_CLOCK : AUDIO_RATE_MODE0_CLOCK;
	if (m_es5506_host)
		m_es5506_host->set_unscaled_clock(clock);
}

void asr10_boot_state::es5506_frame_rate_changed(u32 rate)
{
	if (m_pump)
		m_pump->set_unscaled_clock(rate);
}

TIMER_CALLBACK_MEMBER(asr10_boot_state::pump_release)
{
	if (!m_pump || !m_es5510_host)
		return;

	// The known-program gate deliberately rejects all other uploaded images.
	// It is a functional safety hypothesis, not an ASR HALT-pin reconstruction.
	const bool lush = m_es5510_host->_instr(0x00) == 0xffffffff9040ULL
		&& m_es5510_host->_instr(0x3b) == 0x7a81fffff040ULL;
	const bool hall = m_es5510_host->_instr(0x00) == 0xffffffff9040ULL
		&& m_es5510_host->_instr(0x01) == 0xea7dffff9128ULL
		&& m_es5510_host->_instr(0x58) == 0x3662fffff040ULL;
	if (lush || hall)
		m_pump->set_esp_halted(false);
}

void asr10_boot_state::effect_audio_rate_postload()
{
	apply_effect_audio_rate_policy();
}

u16 asr10_boot_state::system_ram_alias_r(offs_t offset, u16 mem_mask)
{
	const u32 address = 0x00200000 + (offset << 1);
	const u32 wrapped = address % SYSTEM_RAM_BYTES;
	if (wrapped < LOWMEM_WORDS * 2)
		return m_lowmem_shadow[wrapped >> 1] & mem_mask;
	return m_sample_ram[(wrapped - LOWMEM_WORDS * 2) >> 1] & mem_mask;
}


void asr10_boot_state::system_ram_alias_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = 0x00200000 + (offset << 1);
	const u32 wrapped = address % SYSTEM_RAM_BYTES;
	if (wrapped < LOWMEM_WORDS * 2)
		COMBINE_DATA(&m_lowmem_shadow[wrapped >> 1]);
	else
		COMBINE_DATA(&m_sample_ram[(wrapped - LOWMEM_WORDS * 2) >> 1]);
}


u16 asr10_boot_state::high_alias_r(offs_t offset, u16 mem_mask)
{
	const u8 *rom = m_rom->base();
	const u32 runtime_address_24 = 0x00f80000 | (offset << 1);
	const u32 rom_offset = runtime_address_24 & ROM_MASK;
	return ((u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK]) & mem_mask;
}


void asr10_boot_state::high_alias_w(offs_t offset, u16 data, u16 mem_mask)
{
}


u16 asr10_boot_state::upd72069_fdc_r(offs_t offset, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);

	u16 raw_data = 0;
	if ((address & 3) == 1)
	{
		raw_data = m_fdc->msr_r();
	}
	else if ((address & 3) == 3)
	{
		raw_data = m_fdc->fifo_r();
	}

	return raw_data & mem_mask;
}


void asr10_boot_state::upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 address = (0x00fc4000 | (offset << 1)) | (ACCESSING_BITS_0_7 ? 1 : 0);

	if ((address & 3) == 1 && ACCESSING_BITS_0_7)
	{
		const u8 aux_command = u8(data);
		m_fdc->auxcmd_w(aux_command);
		if (ASR10_MISSING_FDC_RATE_SOURCE && aux_command == 0x88)
			m_fdc->set_rate(500000);
	}
	else if ((address & 3) == 3 && ACCESSING_BITS_0_7)
	{
		m_fdc->fifo_w(u8(data));
	}
}


u16 asr10_boot_state::duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	u16 raw_data = ACCESSING_BITS_0_7 ? m_duart->read(word) : 0;
	const u16 data = raw_data & mem_mask;
	return data;
}


void asr10_boot_state::duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u32 word = offset & 0x0f;
	if (ACCESSING_BITS_0_7)
		m_duart->write(word, u8(data));
}


u16 asr10_boot_state::scsi_asr_candidate_r(offs_t offset, u16 mem_mask)
{
	return 0;
}


void asr10_boot_state::scsi_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask)
{
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

	// Klockprovenens (keyboard-and-sample-bridge-10.md): GISSNING.
	// Borrows Y1's own value only as a placeholder; not measured or
	// derived, no board citation exists for the FDC's actual clock.
	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder for boot tracing
	m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w));
	// docs/asr10/investigations/ready-line-artifact-probe.md,
	// irq1-handler-chain-probe.md, irq1-storage-completion-probe.md: this
	// pair is meaningful only together, landed together after negative
	// control, positive control, and four clean vector-$51 IACKs.
	//
	// upd765_family_device::run_drive_ready_polling() (upd765.cpp) polls
	// floppy_image_device::ready_r() every 1.024ms against a stored flag
	// and, on a transition, synthesizes an INTRQ + ST0 "ready line changed"
	// result -- real uPD765-family behavior (this is genuine edge-triggered
	// polling, not a MAME-only shortcut). On real hardware, if RDY is
	// physically tied active on this board, that polling loop never sees an
	// edge and no such interrupt is ever generated. set_ready_line_connected
	// (false) models exactly that board policy -- not a workaround for a
	// MAME quirk, a stand-in for a specific, plausible physical wiring
	// choice. [Likely, coverage: 5 regression tests, no in-session disk
	// swap exercised] Empirically safe: 5/5 alone, including nodisk (whose
	// no-disk detection runs through DUART IP0/INDEX, not FDC ready) --
	// multi-disk INSERT DISK prompts during a running session are untested.
	// get_ready() with ready_connected=false falls back to
	// `!external_ready`; nothing in this driver ever calls ready_w(), so
	// external_ready stays false and get_ready() returns permanently true
	// (always-ready), not always-not-ready.
	//
	// Without this, the same INTRQ wiring alone breaks boot itself
	// (ERROR 129 -- a genuine 68000 Address Error, not a firmware-level
	// detection; see the retroactive correction in
	// irq1-storage-completion-probe.md) by delivering a phantom completion
	// into the generic vector-$51 dispatcher outside any context that
	// installed its $0402 continuation. With both lines here, boot survives
	// to FILE 1 and the instrument-load path's real completion chain runs
	// correctly through RECALIBRATE, SEEK 0F 00 01, and READ DATA $46 --
	// each a genuine vector-$51 delivery with $0402 correctly populated by
	// the instrument-load's own RECALIBRATE issuer -- then stops at a
	// legitimate `DISK ERROR - LOST DATA` (uPD765 ST1 overrun) -- MC68302
	// IDMA was not implemented at that point.
	//
	// Now wired to the minimal IDMA in mc68302_device (idma_drq_w() above,
	// docs/asr10/investigations/idma-implementation-plan.md): does not
	// deliver vector $4B (IDMA's own internal-INRQ completion path).
	// Measured: at every observed vector-$51 IACK, IMR is $E480, and bit
	// 11 (IDMA's INRQ source, docs/mc68302/interrupt-source-map.md) is
	// clear -- $E480 = $C080 (ROM's PB9/10/11 unmask) | $2400 (OS's SCC1+
	// SCC2 unmask); bit 10, not bit 11, is what's set in that nibble.
	// IDMA's completion is masked throughout the observed window, and all
	// four completions actually observed for this request (RECALIBRATE,
	// two SEEKs, READ DATA's own result) arrived via the FDC's own INTRQ
	// through this same external-IRQ1/vector-$51 path, not vector $4B --
	// so this driver does not implement the internal IPR/IMR/ISR path or
	// vector $4B delivery, only the transfer itself.
	m_fdc->set_ready_line_connected(false);
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, 1);
	m_fdc->drq_wr_callback().set(FUNC(asr10_boot_state::idma_drq_w));


	// The uPD72069 sees this child connector as drive 0 via the conventional "fdc:0" tag.
	// Mounted HFE media changes Recalibrate/Sense from 68,00 (not ready) to 20,00.
	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);

	// U20, per docs/hardware-identity.md. X1 is derived, not a separate
	// crystal: the board has no 3.6864MHz part (Y1=16MHz, Y2=30.476MHz,
	// Y3=33.8688MHz), and ES5701's own reconstruction (sources/es5701.vhd)
	// divides the 16MHz system clock by two on-chip; docs/asr10/PLAN.md
	// section 3 hypothesizes a further /2 (spare 74HC74/74F74 flip-flops
	// on the board) yields 16/4 = 4.000MHz into X1, matching the 4MHz X1
	// this same SCN2681 runs at on esqkt.cpp/esq5505.cpp. Channel B is
	// wired to an ASR-10-specific esqpanel-derived device. The hand-written
	// taps in duart_panel_asr_candidate_r/w remain for ASR-10 boot/status
	// responses. The IRQ output is a real pin on a real device --
	// docs/asr10/duart-imr.md found it genuinely pending (counter/timer
	// ready) 159/160 of the time and never wired to anything. Wired here
	// the same way esq5505.cpp wires its own SCN2681 (irq_cb() ->
	// set_inputline), replacing the old m_panel_c_isr/imr shadow that
	// could only ever see RX-ready, never counter/timer. Landed together
	// with mc68302_device::irq6_ack_vector() (docs/asr10/PLAN.md fas 3
	// steg 2, minimal slice) -- see docs/asr10/duart-irq6-wiring.md for
	// why the irq_cb wiring alone regresses the boot without it.
	// Klockprovenens (keyboard-and-sample-bridge-10.md): HÄRLEDD,
	// empiriskt bekräftad. Master-klockan (16MHz/4) sätter devicens
	// interna timing; kanal A/B:s egna baud-genererande IP3/IP5-klockor
	// sätts separat via set_clocks() nedan och är de som faktiskt
	// verifierats: IP3=500kHz/16=31250 baud matchar den byte-för-byte
	// korrekt mottagna MIDI-trafiken; IP5=1MHz/16=62500 baud matchar
	// panelkanalens dokumenterade takt.
	SCN2681(config, m_duart, XTAL(16'000'000) / 4);
	m_duart->irq_cb().set_inputline(m_maincpu, 6);
	m_duart->b_tx_cb().set(m_panel, FUNC(asr10panel_device::rx_w));
	// set_clocks() maps to IP3/IP4/IP5/IP6. With CSRA/CSRB selector $E,
	// mc68681.cpp uses IP3/16 for channel A and IP5/16 for channel B.
	m_duart->set_clocks(500'000, 500'000, 1'000'000, 1'000'000);

	// Channel A is MIDI, 31250 baud -- unconnected until this task. Wired
	// per the esq5505.cpp precedent (same SCN2681 family, same channel
	// assignment): a_tx_cb feeds a MIDI Out port, and a MIDI In port's
	// rxd_handler feeds rx_a_w. This is the one machine-config change
	// this task is scoped to.
	m_duart->a_tx_cb().set(m_mdout, FUNC(midi_port_device::write_txd));
	auto &mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(m_duart, FUNC(scn2681_device::rx_a_w));
	midiout_slot(MIDI_PORT(config, "mdout"));

	ASR10PANEL(config, m_panel);
	m_panel->write_tx().set(m_duart, FUNC(scn2681_device::rx_b_w));
	m_panel->write_analog().set(FUNC(asr10_boot_state::analog_w));

	// Phase 1 host-port fingerprint mapping. Not board-proven: see
	// docs/asr10/es5506-chain-verification.md.
	// Klockprovenens (keyboard-and-sample-bridge-10.md): HÄRLEDD
	// (bakåträkning från uppmätt tonhöjd) + FAMILJEPRECEDENS (kortets
	// egen kristallista). En faktor-2-spänning mot databladets "up to
	// 16MHz"-specifikation och esq5505.cpp:s /2-delning av samma
	// namngivna kristall står kvar som en namngiven, oöppnad skuld --
	// se journaldokumentet, ingen mekanism är fastställd.
	// keyboard-and-sample-bridge-9.md: XTAL(16'000'000) was Y1, the MPU
	// crystal, borrowed from esq5505.cpp precedent only because it
	// shared a number, not because it's ES5506's own board crystal.
	// The board's crystal complement is Y1=16MHz (MPU), Y2=30.47618MHz
	// and Y3=33.8688MHz (ES5506/ES5510/AD-DA side, per PLAN.md section 3
	// and es5506-hostport.md's own crystal table). Two independent
	// lines converge on Y2: a backwards calculation from measured $3C
	// pitch (keyboard-and-sample-bridge-7.md, ~30.6MHz) and the board's
	// documented crystal (30.47618MHz), within 0.4%. Measured after
	// this change (autocorrelation, not zero-crossing): $3C lands at
	// ~259-261Hz against a MIDI-nominal 261.6Hz (~0.4-0.6% low,
	// consistent with the sample's own tuning); semitone/whole-tone/
	// fifth/octave interval ratios hold within 0.3% of equal
	// temperament at this clock -- see the investigation doc for the
	// full measurement. Open: MAME's sample_rate divisor
	// (16*(ACT+1)) is identical for es5505_device and es5506_device
	// (es5506.cpp), so no MAME-code-visible divisor bug explains why
	// this clock divided by (16*32) comes out at exactly double the
	// board's documented 29.76kHz mode; esq5505.cpp's own precedent
	// (30.47618MHz_XTAL / 2 fed to ES5505) supports a crystal-network
	// /2 as the more likely explanation, not confirmed further here.
	es5506_device &es5506_host(ES5506(config, m_es5506_host, AUDIO_RATE_MODE0_CLOCK));
	es5506_host.set_addrmap(0, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.set_addrmap(1, &asr10_boot_state::es5506_wavetable_bank1_map);
	es5506_host.set_addrmap(2, &asr10_boot_state::es5506_unpopulated_wavetable_map);
	es5506_host.set_addrmap(3, &asr10_boot_state::es5506_unpopulated_wavetable_map);
	es5506_host.sample_rate_changed().set(FUNC(asr10_boot_state::es5506_frame_rate_changed));

	es5506_host.read_port_cb().set(FUNC(asr10_boot_state::analog_r));

	// The [Hypothesis] adapter needs four exposed pairs to duplicate the
	// observed active lane 0/1 into its three program-read serial ports.
	SPEAKER(config, "speaker", 2).front();
	es5506_host.set_channels(4);

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
	// Klockprovenens (keyboard-and-sample-bridge-10.md/-11.md):
	// FAMILJEPRECEDENS + DATABLAD (delvis). docs/ensoniq/ES5510.pdf's
	// own VDD spec cites "< 100mA @ 10MHz clock" and its ESPR7 timing
	// table covers 8/10/12MHz as three real speed grades -- 10MHz is
	// datasheet-real for this part, not just a borrowed round number.
	// esq5505.cpp also has a second board variant (distinct from the
	// crystal-halving one) that clocks M68000/ES5510/ES5505/DMAC all
	// from a flat, undivided 10_MHz_XTAL with ES5510 disabled -- closer
	// to this driver's own pattern than the halved-crystal precedent.
	// Still no measurement possible while set_disable()'d (scratch-
	// enabling it this task did not hang MAME, but hit the already-
	// documented EFFECT DOWNLOAD FAILED path and was reverted, not
	// landed). Whether real ASR-10 hardware feeds ES5510 from Y2, Y3,
	// or a flat 10MHz stays [OPEN], not established.
	es5510_device &es5510_host(ES5510(config, m_es5510_host, XTAL(10'000'000)));
	es5510_host.set_disable();

	// [HYPOTHESIS] ASR functional frame adapter.  It is deliberately a
	// serial-contract choice, not a physical board-route claim: the known
	// 44LUSH image reads SER0/SER2/SER3 and writes SER1.  HALT remains true
	// except after a verified post-upload ROM-HALL or 44LUSH image.
	ESQ_5505_5510_PUMP(config, m_pump, AUDIO_RATE_MODE0_CLOCK / (16 * 32));
	m_pump->set_esp("es5510_host");
	m_pump->set_serial_route(esq_5505_5510_pump_device::serial_route::ser0_ser2_ser3_to_ser1);
	m_pump->set_esp_halted(true);
	m_pump->add_route(0, "speaker", 1.0, 0);
	m_pump->add_route(1, "speaker", 1.0, 1);

	// Minimal discriminating functional map: one observed active ES5506 pair
	// is supplied identically to each port the 44LUSH image actually reads.
	es5506_host.add_route(0, "pump", 1.0, 0); // SER3 left
	es5506_host.add_route(1, "pump", 1.0, 1); // SER3 right
	es5506_host.add_route(0, "pump", 1.0, 2); // SER0 left
	es5506_host.add_route(1, "pump", 1.0, 3); // SER0 right
	es5506_host.add_route(0, "pump", 1.0, 6); // SER2 left
	es5506_host.add_route(1, "pump", 1.0, 7); // SER2 right
}


ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace


// CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness", MACHINE_NO_SOUND)
CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness", 0)
