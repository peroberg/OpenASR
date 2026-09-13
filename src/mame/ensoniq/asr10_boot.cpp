// license:BSD-3-Clause
// copyright-holders:

/***************************************************************************
 *
 *     Ensoniq ASR-10 functional machine model / boot harness
 *
 *  Functional driver model for the Ensoniq ASR-10 advanced sampling
 *  workstation.
 *
 *  Included subsystems and verified facilities:
 *  - MC68302 Integrated Multiprotocol Processor (CPU, SIB, BAR, Port A/B,
 *    timers, interrupt controller, IDMA)
 *  - System memory (16 MiB canonical backing, ROM overlay, low-memory remap,
 *    aliasing detection support)
 *  - Dynamic voice banking on MC68302 CS1 ($FF7F00-$FF7FFF) translating
 *    ES5506 wavetable access into 16 MiB physical sample memory
 *  - Ensoniq ES5506 (OTTO) sound generator:
 *    - 32-channel wavetable synthesis
 *    - Audio clock switching: Mode 0 (29.8 kHz / Y2/2) and Mode 1 (44.1 kHz / Y3/2)
 *    - Large-WaveSample (>4 MiB) transwave/interrupt-driven continuation
 *    - ES5506 IRQB -> MC68302 PB9 -> Level 4 Vector $47 interrupt path
 *    - Analog parameter acquisition (PAR) via MC68302 Port B PB0-PB2 selector
 *  - Ensoniq ES5510 (ESP) host interface and audio routing:
 *    - Host register window ($FC3000-$FC303F) and select/commit registers
 *      ($FC3101, $FC3141, $FC3181, $FC31C1)
 *    - Hardware HALT control via MC68302 PA4
 *    - Inter-IC serial audio pump routing (BUS1/2/3 -> SER0/2/3 -> SER1)
 *  - Storage subsystem:
 *    - NEC uPD72069 floppy controller with MC68302 IDMA and IRQ1 completion
 *    - Western Digital WD33C93A SCSI controller with IDMA and IRQ1 completion
 *  - Communication & Front Panel:
 *    - SCN2681 Dual UART (DUART):
 *      - Channel A: MIDI In/Out (31,250 baud)
 *      - Channel B: ASR-10 front panel communication (62,500 baud)
 *      - DUART IRQ -> MC68302 Level 6 autovector
 *  - Save-state support
 *
 *  NOTE: Board-level glue logic (including Ensoniq ES5701 SuperGLU ASIC,
 *  exact crystal multiplexers, and discrete bus arbiters) is modeled at the
 *  functional interface boundary; exact physical PCB wiring remains open.
 *
 *  Address space summary:
 *   $000000-$0fffff    System low RAM / boot ROM overlay (controlled by CS0)
 *   $100000-$1fffff    Sample RAM window (mapped to system memory)
 *   $200000-$f7ffff    Expanded system / sample memory (up to 16 MiB)
 *   $f80000-$fbffff    High ROM alias window
 *   $fc2000-$fc207f    Ensoniq ES5506 (OTTO) host registers
 *   $fc3000-$fc31ff    Ensoniq ES5510 (ESP) host register & commit block
 *   $fc4000-$fc4003    uPD72069 Floppy Disk Controller
 *   $fc4800-$fc481f    SCN2681 DUART (MIDI & front panel)
 *   $fc5000-$fc5003    WD33C93A SCSI controller
 *   $fc6000-$fc6fff    MC68302 internal DPRAM and registers (dynamic BAR)
 *   $ff7f00-$ff7fff    MC68302 CS1 voice banking translation table
 *
 ***************************************************************************/

#include "emu.h"
#include "main.h"

#include "bus/midi/midi.h"
#include "cpu/m68000/m68000.h"
#include "imagedev/floppy.h"
#include "machine/mc68302.h"
#include "machine/mc68681.h"
#include "machine/upd765.h"
#include "machine/wd33c9x.h"
#include "machine/nscsi_bus.h"
#include "bus/nscsi/devices.h"
#include "bus/nscsi/cd.h"
#include "bus/nscsi/hd.h"

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
		, m_scsi(*this, "wd33c93a")
	{
	}

	void asr10_boot(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	// Constants
	static constexpr u32 ROM_MASK = 0x0003ffff;
	static constexpr u32 LOWMEM_WORDS = 0x00100000 / 2;
	// Verified functional model: V3.50's unmodified alias probe ($F8A166-$F8A244)
	// detects this distinct 16 MiB backing as its $000000/$F80000 configuration.
	static constexpr u32 SYSTEM_RAM_BYTES = 0x01000000;
	static constexpr u32 SYSTEM_RAM_WORDS = SYSTEM_RAM_BYTES / 2;
	// Spec-consistent ES5506 clock domains:
	// Mode 0: Y2 / 2 = 30.476180 MHz / 2 = 15.238090 MHz -> Fs = 15.238090 / (16 * 32) = 29,761.895 Hz
	// Mode 1: Y3 / 2 = 33.868800 MHz / 2 = 16.934400 MHz -> Fs = 16.934400 / (16 * 24) = 44,100.000 Hz
	static constexpr u32 AUDIO_RATE_MODE0_CLOCK = 30'476'180 / 2;
	static constexpr u32 AUDIO_RATE_MODE1_CLOCK = 33'868'800 / 2;
	static constexpr bool ASR10_MISSING_FDC_RATE_SOURCE = true;

	// Devices (all mandatory in current machine configuration)
	required_device<mc68302_device> m_maincpu;
	required_device<upd72069_device> m_fdc;
	required_device<floppy_connector> m_floppy_connector;
	required_device<scn2681_device> m_duart;
	required_device<asr10panel_device> m_panel;
	required_device<midi_port_device> m_mdout;
	required_memory_region m_rom;
	required_device<es5506_device> m_es5506_host;
	required_device<es5510_device> m_es5510_host;
	required_device<esq_5505_5510_pump_device> m_pump;
	required_device<wd33c93a_device> m_scsi;

	// Core memory / decode
	std::unique_ptr<u16[]> m_system_ram;
	void mem_map(address_map &map) ATTR_COLD;
	void cpu_space_map(address_map &map) ATTR_COLD;
	u16 system_memory_r(u32 address, u16 mem_mask = ~0);
	void system_memory_w(u32 address, u16 data, u16 mem_mask = ~0);
	u16 low_rom_or_lowmem_r(offs_t offset, u16 mem_mask = ~0);
	void lowmem_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 sample_memory_r(offs_t offset, u16 mem_mask = ~0);
	void sample_memory_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 expanded_memory_r(offs_t offset, u16 mem_mask = ~0);
	void expanded_memory_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 high_alias_r(offs_t offset, u16 mem_mask = ~0);
	void high_alias_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	// Sample-memory / CS1 dynamic voice banking
	std::array<std::array<u16, 4>, 32> m_voice_bank{};
	void es5506_wavetable_map(address_map &map) ATTR_COLD;
	u16 voice_bank_r(offs_t offset, u16 mem_mask = ~0);
	void voice_bank_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 es5506_wavetable_r(offs_t offset);

	// Storage (FDC / SCSI / IDMA)
	emu_timer *m_idma_tc_timer = nullptr;
	int m_fdc_irq = 0;
	int m_scsi_irq = 0;
	u16 upd72069_fdc_r(offs_t offset, u16 mem_mask = ~0);
	void upd72069_fdc_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void idma_drq_w(int state);
	void scsi_drq_w(int state);
	TIMER_CALLBACK_MEMBER(idma_tc_deliver);
	static void floppy_drives(device_slot_interface &device);
	static void floppy_formats(format_registration &fr);

	// Interrupts
	u8 maincpu_iack_r(u8 level);
	void fdc_intrq_w(int state);
	void scsi_irq_w(int state);
	void es5506_irq_w(int state);

	// Panel / MIDI / Analog
	std::array<u16, 8> m_analog_values{};
	u16 duart_panel_asr_candidate_r(offs_t offset, u16 mem_mask = ~0);
	void duart_panel_asr_candidate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 analog_r();
	void analog_w(offs_t offset, u16 data);

	// Audio clocks & rate mode
	u8 m_effect_audio_mode = 0;
	void apply_effect_audio_rate_policy();
	void effect_audio_rate_postload();
	void es5506_frame_rate_changed(u32 rate);

	// ES5510 host / lifecycle
	bool m_esp_program_loaded = false;
	bool m_esp_program_received = false;
	void mc68302_pa_w(u16 data);
	u8 es5510_fixed_host_r(u8 host_offset);
	void es5510_fixed_host_w(u8 host_offset, u8 data, bool marks_program_received);
	u8 es5510_host_read_select_r(offs_t offset);
	void es5510_host_read_select_w(offs_t offset, u8 data);
	u8 es5510_host_write_select_gpr_r(offs_t offset);
	void es5510_host_write_select_gpr_w(offs_t offset, u8 data);
	u8 es5510_host_write_select_instr_r(offs_t offset);
	void es5510_host_write_select_instr_w(offs_t offset, u8 data);
	u8 es5510_host_write_select_gpr_instr_r(offs_t offset);
	void es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data);

	// Timers / Board glue
	emu_timer *m_lrclk_timer = nullptr;
	bool m_lrclk_level = false;
	TIMER_CALLBACK_MEMBER(lrclk_toggle);
};

//-------------------------------------------------
//  machine_start - allocate resources and register state
//-------------------------------------------------

void asr10_boot_state::machine_start()
{
	m_lrclk_timer = timer_alloc(FUNC(asr10_boot_state::lrclk_toggle), this);
	m_idma_tc_timer = timer_alloc(FUNC(asr10_boot_state::idma_tc_deliver), this);
	m_system_ram = make_unique_clear<u16[]>(SYSTEM_RAM_WORDS);

	save_pointer(NAME(m_system_ram), SYSTEM_RAM_WORDS);
	save_item(NAME(m_lrclk_level));
	save_item(NAME(m_analog_values));
	save_item(NAME(m_effect_audio_mode));
	save_item(NAME(m_esp_program_loaded));
	save_item(NAME(m_esp_program_received));
	save_item(NAME(m_voice_bank));
	save_item(NAME(m_fdc_irq));
	save_item(NAME(m_scsi_irq));
	machine().save().register_postload(save_prepost_delegate(FUNC(asr10_boot_state::effect_audio_rate_postload), this));
}

//-------------------------------------------------
//  machine_reset - reset board state and hardware devices
//-------------------------------------------------

void asr10_boot_state::machine_reset()
{
	for (auto &vb : m_voice_bank)
		vb.fill(0);
	m_esp_program_loaded = false;
	m_esp_program_received = false;
	m_effect_audio_mode = 0;
	apply_effect_audio_rate_policy();
	m_fdc_irq = 0;
	m_scsi_irq = 0;
	m_analog_values[0] = 0x200; // Pitch wheel, boot-calibrated center
	m_analog_values[1] = 0x200; // ASR-88 conditional source; no ASR-10 label
	m_analog_values[2] = 0x200; // Mod wheel
	m_analog_values[3] = 0x3ff; // Volume
	m_analog_values[4] = 0x200; // Pedal/CV
	m_analog_values[5] = 0x200; // MR. KNOB / Data Entry
	m_analog_values[6] = 0x200; // No producer in the analyzed V3.50 path
	m_analog_values[7] = 0x300; // Stable calibration reference: viewer >= 190

	// [provisional board policy]
	// Board-level LRCLK into PB3 (GPIO input, docs/mc68302/pin-function-map.md):
	// external to the 68302, always running once the machine is up, not a
	// register-driven behavior. The rate is provisionally derived from the
	// board's 33.8688 MHz audio clock as 768 x 44.1 kHz.
	m_lrclk_level = false;
	m_lrclk_timer->adjust(attotime::from_hz(44100), 0, attotime::from_hz(44100));
	std::fill_n(m_system_ram.get(), LOWMEM_WORDS, 0);

	m_es5510_host->set_HALT(true);
	m_pump->set_esp_halted(true);
}

//-------------------------------------------------
//  Address Maps
//-------------------------------------------------

void asr10_boot_state::mem_map(address_map &map)
{
	// Low boot region. Reads normally come from ROM; writes are shadowed for
	// the boot-time remap/low-memory behavior used by the ROM.
	map(0x000000, 0x0fffff).rw(FUNC(asr10_boot_state::low_rom_or_lowmem_r), FUNC(asr10_boot_state::lowmem_w));

	// Sample memory window ($100000-$1fffff)
	map(0x100000, 0x1fffff).rw(FUNC(asr10_boot_state::sample_memory_r), FUNC(asr10_boot_state::sample_memory_w));

	// Expanded system / sample memory ($200000-$f7ffff)
	map(0x200000, 0xf7ffff).rw(FUNC(asr10_boot_state::expanded_memory_r), FUNC(asr10_boot_state::expanded_memory_w));

	// High ROM alias window ($f80000-$fbffff)
	map(0xf80000, 0xfbffff).rw(FUNC(asr10_boot_state::high_alias_r), FUNC(asr10_boot_state::high_alias_w));

	// Ensoniq ES5506 host registers ($fc2000-$fc207f)
	map(0xfc0000, 0xfc1fff).ram();
	map(0xfc2000, 0xfc207f).rw(m_es5506_host, FUNC(es5506_device::read), FUNC(es5506_device::write)).umask16(0x00ff);
	map(0xfc2080, 0xfc2fff).ram();

	// Ensoniq ES5510 host registers and commit ports ($fc3000-$fc31ff)
	map(0xfc3000, 0xfc303f).rw(m_es5510_host, FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);
	map(0xfc3040, 0xfc30ff).ram();
	map(0xfc3100, 0xfc3101).rw(FUNC(asr10_boot_state::es5510_host_read_select_r), FUNC(asr10_boot_state::es5510_host_read_select_w)).umask16(0x00ff);
	map(0xfc3102, 0xfc313f).ram();
	map(0xfc3140, 0xfc3141).rw(FUNC(asr10_boot_state::es5510_host_write_select_gpr_r), FUNC(asr10_boot_state::es5510_host_write_select_gpr_w)).umask16(0x00ff);
	map(0xfc3142, 0xfc317f).ram();
	map(0xfc3180, 0xfc3181).rw(FUNC(asr10_boot_state::es5510_host_write_select_instr_r), FUNC(asr10_boot_state::es5510_host_write_select_instr_w)).umask16(0x00ff);
	map(0xfc3182, 0xfc31bf).ram();
	map(0xfc31c0, 0xfc31c1).rw(FUNC(asr10_boot_state::es5510_host_write_select_gpr_instr_r), FUNC(asr10_boot_state::es5510_host_write_select_gpr_instr_w)).umask16(0x00ff);
	map(0xfc31c2, 0xfc31ff).ram();
	map(0xfc3200, 0xfc3fff).ram();

	// Floppy controller (uPD72069)
	map(0xfc4000, 0xfc4003).rw(FUNC(asr10_boot_state::upd72069_fdc_r), FUNC(asr10_boot_state::upd72069_fdc_w));
	map(0xfc4004, 0xfc47ff).ram();

	// DUART (SCN2681)
	map(0xfc4800, 0xfc481f).rw(FUNC(asr10_boot_state::duart_panel_asr_candidate_r), FUNC(asr10_boot_state::duart_panel_asr_candidate_w));
	map(0xfc4820, 0xfc4fff).ram();

	// SCSI controller (WD33C93A)
	map(0xfc5000, 0xfc5003).rw(m_scsi, FUNC(wd33c93a_device::indir_r), FUNC(wd33c93a_device::indir_w)).umask16(0x00ff);
	map(0xfc5004, 0xfc501f).ram();

	// MC68302 internal DPRAM/registers fallback ($fc5020-$ff7eff)
	map(0xfc5020, 0xff7eff).ram();

	// MC68302 CS1 voice banking ($ff7f00-$ff7fff)
	map(0xff7f00, 0xff7fff).rw(FUNC(asr10_boot_state::voice_bank_r), FUNC(asr10_boot_state::voice_bank_w));
	map(0xff8000, 0xffffff).ram();
}

void asr10_boot_state::cpu_space_map(address_map &map)
{
	// M68000 interrupt acknowledge cycles:
	// Level 1: FDC / SCSI shared external IRQ1
	// Level 4: ES5506 IRQB -> MC68302 PB9
	// Level 6: SCN2681 DUART
	map(0xfffff3, 0xfffff3).lr8(NAME([this]() { return maincpu_iack_r(1); }));
	map(0xfffff5, 0xfffff5).lr8(NAME([this]() { return maincpu_iack_r(2); }));
	map(0xfffff7, 0xfffff7).lr8(NAME([this]() { return maincpu_iack_r(3); }));
	map(0xfffff9, 0xfffff9).lr8(NAME([this]() { return m_maincpu->irq4_ack_vector(); }));
	map(0xfffffb, 0xfffffb).lr8(NAME([this]() { return maincpu_iack_r(5); }));
	map(0xfffffd, 0xfffffd).lr8(NAME([this]() { return maincpu_iack_r(6); }));
	map(0xffffff, 0xffffff).lr8(NAME([this]() { return maincpu_iack_r(7); }));
}

//-------------------------------------------------
//  Core Memory / Decode Handlers
//-------------------------------------------------

u16 asr10_boot_state::system_memory_r(u32 address, u16 mem_mask)
{
	return m_system_ram[(address & (SYSTEM_RAM_BYTES - 1)) >> 1] & mem_mask;
}

void asr10_boot_state::system_memory_w(u32 address, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_system_ram[(address & (SYSTEM_RAM_BYTES - 1)) >> 1]);
}

u16 asr10_boot_state::low_rom_or_lowmem_r(offs_t offset, u16 mem_mask)
{
	if (!m_maincpu->cs0_covers(0))
		return system_memory_r(offset << 1, mem_mask);

	const u32 byte_address = offset << 1;
	const u8 *rom = m_rom->base();
	const u32 rom_offset = byte_address & ROM_MASK;
	return (u16(rom[rom_offset]) << 8) | rom[(rom_offset + 1) & ROM_MASK];
}

void asr10_boot_state::lowmem_w(offs_t offset, u16 data, u16 mem_mask)
{
	system_memory_w(offset << 1, data, mem_mask);

	// `$0CE3` is the PC-correlated current-effect operating-mode byte. Its
	// values 0/1 drive the separately verified ACTV and pitch setup branches;
	// this board-level policy adds only the surviving current-MAME rate relation.
	if (offset == (0x0ce2 / 2) && ACCESSING_BITS_0_7)
	{
		const u8 mode = m_system_ram[offset] & 0xff;
		if (mode <= 1 && mode != m_effect_audio_mode)
		{
			m_effect_audio_mode = mode;
			apply_effect_audio_rate_policy();
		}
	}
}

u16 asr10_boot_state::sample_memory_r(offs_t offset, u16 mem_mask)
{
	return system_memory_r(0x00100000 + (offset << 1), mem_mask);
}

void asr10_boot_state::sample_memory_w(offs_t offset, u16 data, u16 mem_mask)
{
	system_memory_w(0x00100000 + (offset << 1), data, mem_mask);
}

u16 asr10_boot_state::expanded_memory_r(offs_t offset, u16 mem_mask)
{
	return system_memory_r(0x00200000 + (offset << 1), mem_mask);
}

void asr10_boot_state::expanded_memory_w(offs_t offset, u16 data, u16 mem_mask)
{
	system_memory_w(0x00200000 + (offset << 1), data, mem_mask);
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

//-------------------------------------------------
//  Sample-Memory / CS1 Dynamic Voice Banking
//-------------------------------------------------

void asr10_boot_state::es5506_wavetable_map(address_map &map)
{
	map(0x000000, 0x1fffff).r(FUNC(asr10_boot_state::es5506_wavetable_r));
}

// [VERIFIED functional translation]
// External voice banking glue logic on MC68302 CS1 ($FF7F00-$FF7FFF).
// The firmware initializes 32 voice-banking table entries (8 bytes / 4 words each)
// at $F8CD22-$F8CD56: voice 0/1 use entry 0 ($FF7F00), voice 2 uses entry 1 ($FF7F08),
// and voice V uses entry (V - 1) at $FF7F00 + (V - 1) * 8.
// During sample setup at $F8E250-$F8E27C, firmware writes the starting physical
// megabyte index d0 = (a3 >> 20) & 0x0F to word 0, d0+1 to word 1, d0+2 to word 2,
// and d0+3 to word 3.
u16 asr10_boot_state::voice_bank_r(offs_t offset, u16 mem_mask)
{
	const u8 entry = (offset >> 2) & 0x1f;
	const u8 word = offset & 3;
	return m_voice_bank[entry][word] & mem_mask;
}

void asr10_boot_state::voice_bank_w(offs_t offset, u16 data, u16 mem_mask)
{
	const u8 entry = (offset >> 2) & 0x1f;
	const u8 word = offset & 3;
	COMBINE_DATA(&m_voice_bank[entry][word]);
}

// [VERIFIED functional translation]
// In ES5506, the 21-bit word address has bits 20:19 selecting which of four 1 MB
// windows within the 4 MB space is addressed, and bits 18:0 providing the 1 MB
// sub-offset (512K words). Dynamic voice banking translates this into the
// canonical 16 MiB firmware-visible backing store.
// Exact physical glue owner (discrete logic vs ASIC) remains [OPEN physical detail].
u16 asr10_boot_state::es5506_wavetable_r(offs_t offset)
{
	const u32 voice = m_es5506_host->get_voice_index();
	const u32 entry = (voice > 0) ? (voice - 1) : 0;
	const u32 bank_idx = (offset >> 19) & 3;
	const u32 sub_offset = offset & 0x7ffff;
	const u32 megabyte = m_voice_bank[entry][bank_idx];
	const u32 phys_byte_address = (megabyte << 20) | (sub_offset << 1);
	return system_memory_r(phys_byte_address);
}

//-------------------------------------------------
//  Storage (FDC / SCSI / IDMA) Handlers
//-------------------------------------------------

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

// [VERIFIED functional / provisional board glue]
// FDC DRQ feeds MC68302 IDMA.
// Invariants:
// - Only consume FDC DRQ if IDMA channel is actively enabled.
// - Defer FDC TC via zero-delay timer to avoid re-entering upd765 live_run().
//   See docs/asr10/investigations/tc-reentrancy-probe.md.
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

void asr10_boot_state::scsi_drq_w(int state)
{
	if (!state || !m_maincpu->idma_channel_active())
		return;

	const u8 data = m_scsi->dma_r();
	m_maincpu->idma_transfer_in(data);
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

static void asr10_scsi_devices(device_slot_interface &device)
{
	device.option_add("harddisk", NSCSI_HARDDISK);
	device.option_add("cdrom", NSCSI_CDROM).machine_config(
		[](device_t *device)
		{
			downcast<nscsi_cdrom_device &>(*device).set_block_size(512);
		});
}

static INPUT_PORTS_START(asr10_boot)
INPUT_PORTS_END

//-------------------------------------------------
//  Interrupt Handlers
//-------------------------------------------------

u8 asr10_boot_state::maincpu_iack_r(u8 level)
{
	if (level == 6)
		return m_maincpu->irq6_ack_vector();
	if (level == 1)
		return m_maincpu->irq1_ack_vector();

	return m68000_base_device::autovector(level);
}

void asr10_boot_state::fdc_intrq_w(int state)
{
	m_fdc_irq = state;
	m_maincpu->set_input_line(1, (m_fdc_irq || m_scsi_irq) ? ASSERT_LINE : CLEAR_LINE);
}

void asr10_boot_state::scsi_irq_w(int state)
{
	m_scsi_irq = state;
	m_maincpu->set_input_line(1, (m_fdc_irq || m_scsi_irq) ? ASSERT_LINE : CLEAR_LINE);
}

void asr10_boot_state::es5506_irq_w(int state)
{
	m_maincpu->set_pb_input(9, state != 0);
}

//-------------------------------------------------
//  Panel / MIDI / Analog Handlers
//-------------------------------------------------

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

u16 asr10_boot_state::analog_r()
{
	const u8 selector = m_maincpu->pbdat_latch() & 7;
	return m_analog_values[selector] & 0x03ff;
}

void asr10_boot_state::analog_w(offs_t offset, u16 data)
{
	m_analog_values[offset & 7] = data & 0x03ff;
}

//-------------------------------------------------
//  Audio Clocks & Rate Mode
//-------------------------------------------------

// [provisional board policy]
// Applies the input clock corresponding to the active effect operating mode (30 kHz vs 44.1 kHz).
void asr10_boot_state::apply_effect_audio_rate_policy()
{
	const u32 clock = m_effect_audio_mode ? AUDIO_RATE_MODE1_CLOCK : AUDIO_RATE_MODE0_CLOCK;
	m_es5506_host->set_unscaled_clock(clock);
}

void asr10_boot_state::effect_audio_rate_postload()
{
	apply_effect_audio_rate_policy();
	const bool run = BIT(m_maincpu->padat_latch(), 4);
	m_es5510_host->set_HALT(!run);
	m_pump->set_esp_halted(!(run && m_esp_program_loaded));
}

void asr10_boot_state::es5506_frame_rate_changed(u32 rate)
{
	m_pump->set_unscaled_clock(rate);
}

//-------------------------------------------------
//  ES5510 Host / Lifecycle
//-------------------------------------------------

void asr10_boot_state::mc68302_pa_w(u16 data)
{
	// MC68302 Port A PA4 (bit 4 of $FC6823 / PADAT) drives the hardware RUN/HALT line:
	// PA4 = 0: ESP halted during program/GPR upload ($FFF977B0)
	// PA4 = 1: ESP running ($FFF977C6)
	const bool run = BIT(data, 4);
	m_es5510_host->set_HALT(!run);
	if (run)
	{
		if (m_esp_program_received)
			m_esp_program_loaded = true;
	}
	else
	{
		m_esp_program_loaded = false;
	}
	m_pump->set_esp_halted(!(run && m_esp_program_loaded));
}

// ES5510 host select/commit helpers.
// FC3101/FC3141/FC3181/FC31C1 are each single-word address_map ranges, so MAME's
// map-relative offset is always 0. These helpers supply the fixed host offset (0x80/0xa0/0xc0/0xe0).
u8 asr10_boot_state::es5510_fixed_host_r(u8 host_offset)
{
	return m_es5510_host->host_r(m_maincpu->space(AS_PROGRAM), host_offset);
}

void asr10_boot_state::es5510_fixed_host_w(u8 host_offset, u8 data, bool marks_program_received)
{
	if (marks_program_received)
		m_esp_program_received = true;
	m_es5510_host->host_w(host_offset, data);
}

u8 asr10_boot_state::es5510_host_read_select_r(offs_t offset)
{
	return es5510_fixed_host_r(0x80);
}

void asr10_boot_state::es5510_host_read_select_w(offs_t offset, u8 data)
{
	es5510_fixed_host_w(0x80, data, false);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_r(offs_t offset)
{
	return es5510_fixed_host_r(0xa0);
}

void asr10_boot_state::es5510_host_write_select_gpr_w(offs_t offset, u8 data)
{
	es5510_fixed_host_w(0xa0, data, false);
}

u8 asr10_boot_state::es5510_host_write_select_instr_r(offs_t offset)
{
	return es5510_fixed_host_r(0xc0);
}

void asr10_boot_state::es5510_host_write_select_instr_w(offs_t offset, u8 data)
{
	es5510_fixed_host_w(0xc0, data, true);
}

u8 asr10_boot_state::es5510_host_write_select_gpr_instr_r(offs_t offset)
{
	return es5510_fixed_host_r(0xe0);
}

void asr10_boot_state::es5510_host_write_select_gpr_instr_w(offs_t offset, u8 data)
{
	es5510_fixed_host_w(0xe0, data, true);
}

//-------------------------------------------------
//  Timers / Board Glue
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(asr10_boot_state::lrclk_toggle)
{
	m_lrclk_level = !m_lrclk_level;
	m_maincpu->set_pb_input(3, m_lrclk_level);
}

//-------------------------------------------------
//  Machine Configuration
//-------------------------------------------------

void asr10_boot_state::asr10_boot(machine_config &config)
{
	MC68302(config, m_maincpu, XTAL(16'000'000));
	m_maincpu->set_addrmap(AS_PROGRAM, &asr10_boot_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &asr10_boot_state::cpu_space_map);
	m_maincpu->pa_out_cb().set(*this, FUNC(asr10_boot_state::mc68302_pa_w));

	// Floppy subsystem: NEC uPD72069 controller and 3.5" HD drive
	UPD72069(config, m_fdc, XTAL(16'000'000)); // clock unknown; placeholder
	m_fdc->idx_wr_callback().set(m_duart, FUNC(scn2681_device::ip0_w));
	// [provisional board policy]
	// Ready line is tied active (false), preventing spurious ST0 ready polling interrupts.
	// FDC completion IRQ (INTRQ) routes through external IRQ1 / vector $51.
	// See docs/asr10/investigations/ready-line-artifact-probe.md.
	m_fdc->set_ready_line_connected(false);
	m_fdc->intrq_wr_callback().set(FUNC(asr10_boot_state::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(asr10_boot_state::idma_drq_w));

	FLOPPY_CONNECTOR(config, m_floppy_connector, asr10_boot_state::floppy_drives, "35hd", asr10_boot_state::floppy_formats, true);

	// SCSI subsystem: WD33C93A controller and bus
	auto &scsi(NSCSI_BUS(config, "scsibus"));
	NSCSI_CONNECTOR(config, "scsibus:0", asr10_scsi_devices, "harddisk");
	NSCSI_CONNECTOR(config, "scsibus:1", asr10_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsibus:2", asr10_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsibus:3", asr10_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsibus:4", asr10_scsi_devices, "cdrom");
	NSCSI_CONNECTOR(config, "scsibus:5", asr10_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsibus:6", asr10_scsi_devices, nullptr);

	auto &wd33c93(WD33C93A(config, m_scsi, XTAL(10'000'000)));
	wd33c93.irq_cb().set(*this, FUNC(asr10_boot_state::scsi_irq_w));
	wd33c93.drq_cb().set(*this, FUNC(asr10_boot_state::scsi_drq_w));
	scsi.set_external_device(7, wd33c93);

	// DUART (SCN2681):
	// Master clock is 16 MHz / 4 = 4.000 MHz.
	// Channel A: MIDI In/Out at 31,250 baud (IP3 = 500 kHz / 16).
	// Channel B: ASR-10 front panel at 62,500 baud (IP5 = 1 MHz / 16).
	// DUART IRQ asserts MC68302 Level 6 autovector.
	SCN2681(config, m_duart, XTAL(16'000'000) / 4);
	m_duart->irq_cb().set_inputline(m_maincpu, 6);
	m_duart->b_tx_cb().set(m_panel, FUNC(asr10panel_device::rx_w));
	m_duart->set_clocks(500'000, 500'000, 1'000'000, 1'000'000);

	m_duart->a_tx_cb().set(m_mdout, FUNC(midi_port_device::write_txd));
	auto &mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(m_duart, FUNC(scn2681_device::rx_a_w));
	midiout_slot(MIDI_PORT(config, "mdout"));

	ASR10PANEL(config, m_panel);
	m_panel->write_tx().set(m_duart, FUNC(scn2681_device::rx_b_w));
	m_panel->write_analog().set(FUNC(asr10_boot_state::analog_w));

	// Ensoniq ES5506 (OTTO) sound generator:
	// Mode 0 clock: Y2/2 = 15.238090 MHz (32 voices -> Fs = 29,761.90 Hz).
	// Mode 1 clock: Y3/2 = 16.934400 MHz (24 voices -> Fs = 44,100.00 Hz).
	es5506_device &es5506_host(ES5506(config, m_es5506_host, AUDIO_RATE_MODE0_CLOCK));
	es5506_host.set_addrmap(0, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.set_addrmap(1, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.set_addrmap(2, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.set_addrmap(3, &asr10_boot_state::es5506_wavetable_map);
	es5506_host.sample_rate_changed().set(FUNC(asr10_boot_state::es5506_frame_rate_changed));
	es5506_host.read_port_cb().set(FUNC(asr10_boot_state::analog_r));
	es5506_host.irq_cb().set(*this, FUNC(asr10_boot_state::es5506_irq_w));
	es5506_host.set_channels(6);

	SPEAKER(config, "speaker", 2).front();

	// Ensoniq ES5510 (ESP) effects processor:
	// Kept out of scheduler via set_disable(); host interface register bank is active.
	es5510_device &es5510_host(ES5510(config, m_es5510_host, XTAL(10'000'000)));
	es5510_host.set_disable();

	// [provisional board policy] ASR functional frame adapter & audio pump:
	// 44LUSH reads SER0/SER2/SER3 and writes SER1.
	ESQ_5505_5510_PUMP(config, m_pump, AUDIO_RATE_MODE0_CLOCK / (16 * 32));
	m_pump->set_esp("es5510_host");
	m_pump->set_serial_route(esq_5505_5510_pump_device::serial_route::ser0_ser2_ser3_to_ser1);
	m_pump->set_esp_halted(true);
	m_pump->add_route(0, "speaker", 1.0, 0);
	m_pump->add_route(1, "speaker", 1.0, 1);

	// Authentic ASR-10 bus-to-ESP serial topology:
	// BUS1 (Pair 0, outs 0/1) -> SER0 (pump inputs 2/3)
	// BUS2 (Pair 1, outs 2/3) -> SER2 (pump inputs 6/7)
	// BUS3 (Pair 2, outs 4/5) -> SER3 (pump inputs 0/1)
	es5506_host.add_route(0, "pump", 1.0, 2); // BUS1 -> SER0 L
	es5506_host.add_route(1, "pump", 1.0, 3); // BUS1 -> SER0 R
	es5506_host.add_route(2, "pump", 1.0, 6); // BUS2 -> SER2 L
	es5506_host.add_route(3, "pump", 1.0, 7); // BUS2 -> SER2 R
	es5506_host.add_route(4, "pump", 1.0, 0); // BUS3 -> SER3 L
	es5506_host.add_route(5, "pump", 1.0, 1); // BUS3 -> SER3 R
}

ROM_START(asr10booth)
	ROM_REGION(0x040000, "maincpu", 0)
	ROM_LOAD16_BYTE("asr-648c-lo-1.5b.bin", 0x000000, 0x020000, CRC(8e437843) SHA1(418f042acbc5323f5b59cbbd71fdc8b2d851f7d0))
	ROM_LOAD16_BYTE("asr-65e0-hi-1.5b.bin", 0x000001, 0x020000, CRC(b37cd3b6) SHA1(c4371848428a628b5e5a50e99be602d7abfc7904))
ROM_END

} // anonymous namespace

CONS(1992, asr10booth, 0, 0, asr10_boot, asr10_boot, asr10_boot_state, empty_init, "Ensoniq", "ASR-10 boot harness", MACHINE_SUPPORTS_SAVE)
