// license:BSD-3-Clause
// copyright-holders:R. Belmont
/*
    Ensoniq Vacuum Fluorescent Displays (VFDs)
    Emulation by R. Belmont
*/

#include "emu.h"
#include "esqvfd.h"

#include "esq1by22.lh"
#include "esq2by40.lh"

#define LOG_DISPLAY_COMMANDS (1U << 1)
#define LOGDC(...) LOGMASKED(LOG_DISPLAY_COMMANDS, __VA_ARGS__)

// #define VERBOSE LOG_DISPLAY_COMMANDS
#define LOG_VFD_TEXT 0

#include "logmacro.h"

DEFINE_DEVICE_TYPE(ESQ1X22,     esq1x22_device,     "esq1x22",     "Ensoniq 1x22 VFD")
DEFINE_DEVICE_TYPE(ESQ2X40,     esq2x40_device,     "esq2x40",     "Ensoniq 2x40 VFD")
DEFINE_DEVICE_TYPE(ESQ2X40_SQ1, esq2x40_sq1_device, "esq2x40_sq1", "Ensoniq 2x40 VFD (SQ-1 variant)")
DEFINE_DEVICE_TYPE(ESQ2X40_VFX, esq2x40_vfx_device, "esq2x40_vfx", "Ensoniq 2x40 VFD (VFX Family variant)")

// adapted from bfm_bd1, rearranged to work with ASCII data used by the Ensoniq h/w
static const uint16_t font[] = {
			// FEDC BA98 7654 3210
	0x0000, // 0000 0000 0000 0000 (space)
	0x32b7, // 0011 0010 1011 0111 0. (Ensoniq VFD 0x21)
	0x0009, // 0000 0000 0000 1001 ".
	0x1408, // 0001 0100 0000 1000 1. (Ensoniq VFD 0x23)
	0xc62d, // 1100 0110 0010 1101 $.
	0xf206, // 1111 0010 0000 0110 2. (Ensoniq VFD 0x25)
	0x0000, // 0000 0000 0000 0000 & (not defined)
	0x0040, // 0000 0000 1000 0000 '.
	0x5226, // 0101 0010 0010 0110 3. (Ensoniq VFD 0x28)
	0xd023, // 1101 0000 0010 0011 4. (Ensoniq VFD 0x29)
	0xccd8, // 1100 1100 1101 1000 *.
	0xc408, // 1100 0100 0000 1000 +.
	0x0000, // 0000 0000 0000 0000 , (not defined)
	0xc000, // 1100 0000 0000 0000 -.
	0x1000, // 0001 0000 0000 0000 .
	0x0090, // 0000 0000 1001 0000 /
	0x22b7, // 0010 0010 1011 0111 0.
	0x0408, // 0000 0100 0000 1000 1.
	0xe206, // 1110 0010 0000 0110 2.
	0x4226, // 0100 0010 0010 0110 3.
	0xc023, // 1100 0000 0010 0011 4.
	0xc225, // 1100 0010 0010 0101 5.
	0xe225, // 1110 0010 0010 0101 6.
	0x0026, // 0000 0000 0010 0110 7.
	0xe227, // 1110 0010 0010 0111 8.
	0xc227, // 1100 0010 0010 0111 9.
	0xd225, // 1101 0010 0010 0101 5. (Ensoniq VFD 0x3A)
	0xf225, // 1111 0010 0010 0101 6. (Ensoniq VFD 0x3B)
	0x0290, // 0000 0010 1001 0000 <.
	0xc200, // 1100 0010 0000 0000 =.
	0x0a40, // 0000 1010 0100 0000 >.
	0x0000, // 0000 0000 0000 0000 ? (not defined)
	0xa626, // 1010 0110 0010 0110 @.
	0xe027, // 1110 0000 0010 0111 A.
	0x462e, // 0100 0110 0010 1110 B.
	0x2205, // 0010 0010 0000 0101 C.
	0x062e, // 0000 0110 0010 1110 D.
	0xa205, // 1010 0010 0000 0101 E.
	0xa005, // 1010 0000 0000 0101 F.
	0x6225, // 0110 0010 0010 0101 G.
	0xe023, // 1110 0000 0010 0011 H.
	0x060c, // 0000 0110 0000 1100 I.
	0x2222, // 0010 0010 0010 0010 J.
	0xa881, // 1010 1000 1000 0001 K.
	0x2201, // 0010 0010 0000 0001 L.
	0x20e3, // 0010 0000 1110 0011 M.
	0x2863, // 0010 1000 0110 0011 N.
	0x2227, // 0010 0010 0010 0111 O.
	0xe007, // 1110 0000 0000 0111 P.
	0x2a27, // 0010 1010 0010 0111 Q.
	0xe807, // 1110 1000 0000 0111 R.
	0xc225, // 1100 0010 0010 0101 S.
	0x040c, // 0000 0100 0000 1100 T.
	0x2223, // 0010 0010 0010 0011 U.
	0x2091, // 0010 0000 1001 0001 V.
	0x2833, // 0010 1000 0011 0011 W.
	0x08d0, // 0000 1000 1101 0000 X.
	0x04c0, // 0000 0100 1100 0000 Y.
	0x0294, // 0000 0010 1001 0100 Z.
	0x1026, // 0001 0000 0010 0110 7. (Ensoniq VFD 0x5B)
	0xf227, // 1111 0010 0010 0111 8. (Ensoniq VFD 0x5C)
	0xd227, // 1101 0010 0010 0111 9. (Ensoniq VFD 0x5D)
	0x0810, // 0000 1000 0001 0000 ^.
	0x0200, // 0000 0010 0000 0000 _
	0x0040, // 0000 0000 0100 0000 `
	0xe027, // 1110 0000 0010 0111 A.
	0x462e, // 0100 0110 0010 1110 B.
	0x2205, // 0010 0010 0000 0101 C.
	0x062e, // 0000 0110 0010 1110 D.
	0xa205, // 1010 0010 0000 0101 E.
	0xa005, // 1010 0000 0000 0101 F.
	0x6225, // 0110 0010 0010 0101 G.
	0xe023, // 1110 0000 0010 0011 H.
	0x060c, // 0000 0110 0000 1100 I.
	0x2222, // 0010 0010 0010 0010 J.
	0xa881, // 1010 1000 1000 0001 K.
	0x2201, // 0010 0010 0000 0001 L.
	0x20e3, // 0010 0000 1110 0011 M.
	0x2863, // 0010 1000 0110 0011 N.
	0x2227, // 0010 0010 0010 0111 O.
	0xe007, // 1110 0000 0000 0111 P.
	0x2a27, // 0010 1010 0010 0111 Q.
	0xe807, // 1110 1000 0000 0111 R.
	0xc225, // 1100 0010 0010 0101 S.
	0x040c, // 0000 0100 0000 1100 T.
	0x2223, // 0010 0010 0010 0011 U.
	0x2091, // 0010 0000 1001 0001 V.
	0x2833, // 0010 1000 0011 0011 W.
	0x08d0, // 0000 1000 1101 0000 X.
	0x04c0, // 0000 0100 1100 0000 Y.
	0x0294, // 0000 0010 1001 0100 Z.
	0x2205, // 0010 0010 0000 0101 [.
	0x0408, // 0000 0100 0000 1000 |
	0x0226, // 0000 0010 0010 0110 ].
	0x0810, // 0000 1000 0001 0000 ~.
	0x0000, // 0000 0000 0000 0000 (DEL)
};

esqvfd_device::esqvfd_device(
	const machine_config &mconfig,
	device_type type,
	const char *tag,
	device_t *owner,
	uint32_t clock,
	int rows,
	int cols) : device_t(mconfig, type, tag, owner, clock),
	            m_vfds(owner ? *owner : *this, "vfd%u", 0U),
	            m_rows(rows),
	            m_cols(cols) {
}

void esqvfd_device::device_start()
{
	save_item(NAME(m_cursx));
	save_item(NAME(m_cursy));
	save_item(NAME(m_savedx));
	save_item(NAME(m_savedy));
	save_item(NAME(m_curattr));
	save_item(NAME(m_chars));
	save_item(NAME(m_attrs));
	save_item(NAME(m_dirty));
	save_item(NAME(m_lastchar));
	save_item(NAME(m_blink_on));
}

void esqvfd_device::device_reset()
{
	m_cursx = m_cursy = 0;
	m_savedx = m_savedy = 0;
	m_curattr = AT_NORMAL;
	m_lastchar = 0;
	m_blink_on = false;
	memset(m_chars, 0, sizeof(m_chars));
	memset(m_attrs, 0, sizeof(m_attrs));
	memset(m_dirty, 1, sizeof(m_dirty));
}

// generic display update; can override from child classes if not good enough
void esqvfd_device::update_display()
{
	for (int row = 0; row < m_rows; row++) {
		for (int col = 0; col < m_cols; col++) {
			if (m_dirty[row][col]) {
				uint32_t segdata = conv_segments(font[m_chars[row][col]]);

				// digits:
				m_vfds[(row * m_cols) + col] = segdata;

				// underlines:
				m_vfds[(row * m_cols) + col + (m_rows * m_cols)] = (m_attrs[row][col] & AT_UNDERLINE) ? 1 : 0;

				m_dirty[row][col] = 0;
			}
		}
	}
}

inline void esqvfd_device::cursor_left()
{
	m_cursx--;
	if (m_cursx < 0) {
		m_cursx += m_cols;
		m_cursy--;
		if (m_cursy < 0)
			m_cursy += m_rows;
	}
}

inline void esqvfd_device::cursor_right()
{
	m_cursx++;
	if (m_cursx >= m_cols) {
		m_cursx -= m_cols;
		m_cursy++;
		if (m_cursy >= m_rows)
			m_cursy -= m_rows;
	}
}

void esqvfd_device::set_blink_on(bool blink_on)
{
	m_blink_on = blink_on;

	for (int row = 0; row < m_rows; row++) {
		for (int col = 0; col < m_cols; col++) {
			m_dirty[row][col] |= m_attrs[row][col] & AT_BLINK;
		}
	}
	update_display();
}

void esqvfd_device::clear()
{
	m_cursx = m_cursy = m_curattr = 0;
	memset(m_chars, 0, sizeof(m_chars));
	memset(m_attrs, 0, sizeof(m_attrs));
	memset(m_dirty, 1, sizeof(m_dirty));

	update_display();
}


/* 2x40 VFD display used in the ESQ-1, VFX-SD, SD-1, and others */

void esq2x40_device::device_add_mconfig(machine_config &config)
{
	config.set_default_layout(layout_esq2by40);
}

void esq2x40_device::write_char(uint8_t data)
{
	LOGDC("display command %02X ", data);
	if (m_lastchar == 0xfa) {
		// ESQ-1 sends (cursor move) 0xfa 0xYY to mark YY characters as underlined at the current cursor location
		for (uint8_t j = 0; j < m_rows; j++) {
			for (uint8_t i = 0; i < m_cols; i++) {
				if (m_cursy == j && i >= m_cursx && i < m_cursx + data)
					m_attrs[j][i] |= AT_UNDERLINE;
				else
					m_attrs[j][i] &= ~AT_UNDERLINE;

				m_dirty[j][i] = 1;
			}
		}

		m_lastchar = 0;
		update_display();
		LOGDC("ESQ1 %d chars underlined from (%d,%d)\n", data, m_cursy, m_cursx);
		return;
	} else if (m_lastchar == 0xff) {
		// 0xff light commands are followed by a byte indicating the light and
		// its requested status. Ignore this.
		m_lastchar = 0;
		LOGDC("set light %d status %d (ignoring)\n", data & 0x3f, data >> 6);
		return;
	}

	m_lastchar = data;

	if ((data >= 0x80) && (data < 0xd0)) {
		m_cursy = ((data & 0x7f) >= 40) ? 1 : 0;
		m_cursx = (data & 0x7f) % 40;
		LOGDC("cursor move to (%d,%d)\n", m_cursy, m_cursx);
	} else if (data >= 0xd0) {
		switch (data) {
			case 0xd0:  // blink start
				m_curattr |= AT_BLINK;
				LOGDC("start blink\n");
				break;

			case 0xd1:  // blink stop (cancel all attribs on VFX+)
				m_curattr = 0; //&= ~AT_BLINK;
				LOGDC("attrs off D1\n");
				break;

			case 0xd2:  // blinking underline on VFX
				m_curattr |= AT_BLINK | AT_UNDERLINE;
				LOGDC("start blinking underline\n");
				break;

			case 0xd3:  // start underline
				m_curattr |= AT_UNDERLINE;
				LOGDC("start underline\n");
				break;

			case 0xd4:  // move curser one step right
				cursor_right();
				LOGDC("cursor right to (%d,%d)\n", m_cursy, m_cursx);
				break;

			case 0xd5:  // move curser one step left
				cursor_left();
				LOGDC("cursor left to (%d,%d)\n", m_cursy, m_cursx);
				break;

			case 0xd6:  // clear screen
				clear();
				LOGDC("clear screen D6\n");
				break;

			case 0xd9:  // underline current character
				m_attrs[m_cursy][m_cursx] |= AT_UNDERLINE;
				m_dirty[m_cursy][m_cursx] = 1;
				LOGDC("set underline at (%d,%d)\n", m_cursy, m_cursx);
				break;

			case 0xdb:  // de-underline current character
				m_attrs[m_cursy][m_cursx] &= ~AT_UNDERLINE;
				m_dirty[m_cursy][m_cursx] = 1;
				LOGDC("clear underline at (%d,%d)\n", m_cursy, m_cursx);
				break;

			case 0xe8:  // also cancel attributes
				m_curattr = 0;
				LOGDC("attr off E8\n");
				break;

			case 0xf5:  // save cursor position
				m_savedx = m_cursx;
				m_savedy = m_cursy;
				m_curattr = 0;
				LOGDC("save cursor position (%d,%d)\n", m_cursy, m_cursx);
				break;

			case 0xf6:  // restore cursor position
				m_cursx = m_savedx;
				m_cursy = m_savedy;
				m_curattr = m_attrs[m_cursy][m_cursx];
				LOGDC("restore cursor position (%d,%d) attr %x\n", m_cursy, m_cursx, m_curattr);
				break;

			case 0xfd: // also clear screen?
				clear();
				LOGDC("clear screen FD\n");
				break;

			case 0xff: // light status; ignore. Next byte will also be ignored.
				LOGDC("set light status (ignoring)\n");
				break;

			default:
				LOGDC("unhandled %02X\n", data);
				break;
		}
	} else if ((data >= 0x20) && (data <= 0x5f)) {
		m_chars[m_cursy][m_cursx] = data - ' ';
		m_attrs[m_cursy][m_cursx] = m_curattr;
		m_dirty[m_cursy][m_cursx] = 1;

		cursor_right();
		LOGDC("char '%c', cursor now (%d,%d)\n", data, m_cursy, m_cursx);
	} else {
		LOGDC("unhandled %02X\n", data);
	}

	update_display();
}

bool esq2x40_device::write_contents(std::ostream &o)
{
	o.put((char) 0xd6); // clear screen

	uint8_t attrs = 0;
	for (int row = 0; row < 2; row++) {
		o.put((char) (0x80 + (40 * row))); // move to first column this row

		for (int col = 0; col < 40; col++) {
			if (m_attrs[row][col] != attrs) {
				attrs = m_attrs[row][col];

				o.put((char) 0xd1); // all attributes off

				if (attrs & AT_BLINK) {
					o.put((char) 0xd0); // blink on
				}

				if (attrs & AT_UNDERLINE) {
					o.put((char) 0xd3); // underline
				}
			}

			o.put((char) (m_chars[row][col] + ' '));
		}
	}

	// move the cursor to the saved position
	o.put((char) 0x80 | (m_cols * m_savedy + m_savedx));
	// and save the position
	o.put((char) 0xf5);

	// move the cursor to the current cursor position
	o.put((char) 0x80 | (m_cols * m_cursy + m_cursx));

	return true;
}

ROM_START( esq2x40_vfx_device )
	ROM_REGION16_BE( 192, "font", 0 )
	ROM_LOAD( "esqvfd_font_vfx.bin", 0, 192, CRC(58dc335b) SHA1(097fc3e1930a49ab61f73ea7a6191c892004f823) )
ROM_END

const tiny_rom_entry *esq2x40_vfx_device::device_rom_region() const
{
	return ROM_NAME( esq2x40_vfx_device );
}

esq2x40_vfx_device::esq2x40_vfx_device(
	const machine_config &mconfig,
	const char *tag,
	device_t *owner,
	uint32_t clock) :
	esq2x40_device(mconfig, ESQ2X40_VFX, tag, owner, clock),
	m_font(*this, "font")
{
}

void esq2x40_vfx_device::device_add_mconfig(machine_config &config)
{
	// Do not set a default layout. This display must be used
	// within a layout that includes the VFD elements, such as
	// vfx.lay, vfxsd.lay or sd1.lay.
}

// Handles blinking of underline and of entire character,
void esq2x40_vfx_device::update_display()
{
#if LOG_VFD_TEXT
	if (!machine().side_effects_disabled())
	{
		char line0[41]{};
		char line1[41]{};

		for (int col = 0; col < 40; col++)
		{
			line0[col] = char(m_chars[0][col] + ' ');
			line1[col] = char(m_chars[1][col] + ' ');
		}

		logerror("VFD0: [%s]\n", line0);
		logerror("VFD1: [%s]\n", line1);
	}
#endif

	for (int row = 0; row < m_rows; row++)
	{
		for (int col = 0; col < m_cols; col++)
		{
			if (m_dirty[row][col])
			{
				uint8_t c = m_chars[row][col];

				uint16_t char_segments = m_font[c < 96 ? c : 0];
				auto attr = m_attrs[row][col];
				uint16_t segments;

				if ((attr & AT_BLINK) && !m_blink_on)
				{
					if (attr & AT_UNDERLINE)
						segments = char_segments;
					else
						segments = 0;
				}
				else
				{
					if (attr & AT_UNDERLINE)
						segments = char_segments | 0x8000;
					else
						segments = char_segments;
				}

				m_vfds[(row * m_cols) + col] = segments;

				m_dirty[row][col] = 0;
			}
		}
	}
}

/* 1x22 display from the VFX (not right, but it'll do for now) */

void esq1x22_device::device_add_mconfig(machine_config &config)
{
	config.set_default_layout(layout_esq1by22);
}


void esq1x22_device::write_char(uint8_t data)
{
	// ASR-10 field-attribute opcode: 0x60 <attr> sets the attribute for
	// the next run of printable characters -- attr bit 0x02 marks the
	// field as underlined (the manual's "cursor (underline) beneath the
	// field", moved by the Left/Right Arrow buttons on most screens).
	// Two-byte opcode+operand, same m_lastchar lookback technique
	// esq2x40_device already uses for its own 0xfa/0xff pairs. Derived
	// from the byte stream, not guessed --
	// docs/asr10/investigations/display-protocol-inventory.md.
	if (m_lastchar == 0x60) {
		m_curattr = (data & 0x02) ? AT_UNDERLINE : AT_NORMAL;
		m_lastchar = 0;
		return;
	}

	// $74/$75/$76 each consume one operand on the observed physical stream,
	// but their semantics remain OPEN (operands are not nibble-only: $74 $40
	// is observed). Consume the operand so it cannot be misread as a cursor
	// column -- both occur in the same low byte range.
	if (m_lastchar == 0x74 || m_lastchar == 0x75 || m_lastchar == 0x76) {
		m_lastchar = 0;
		return;
	}

	m_lastchar = data;

	// Cursor-position opcode: a standalone byte $00-$1F sets the write
	// column directly. Measured live, not inferred
	// (docs/asr10/investigations/partial-update-position-probe.md):
	// $14 (=20 decimal) precedes the two value digits of "VOLUME=99"
	// each time Up/Down changes it, an exact match to that field's
	// column; $00 (=column 0) precedes REC SRC's own field-switch
	// redraw. A full redraw (after a $66 clear) never needs this, since
	// sequential placement from column 0 already lands correctly --
	// this is specifically what a *partial* update (no clear) needs to
	// avoid writing at whatever column a previous, unrelated write left
	// the cursor at (previously the missing mechanism: a changed value
	// was appended after the old one instead of overwriting it).
	if (data <= 0x1f) {
		m_cursx = data;
		return;
	}

	if (data >= 0x60) {
		switch (data) {
			case 'f':   // clear screen
				m_cursx = m_cursy = 0;
				memset(m_chars, 0, sizeof(m_chars));
				memset(m_attrs, 0, sizeof(m_attrs));
				memset(m_dirty, 1, sizeof(m_dirty));
				m_curattr = AT_NORMAL;
				break;

			case 0x60:  // field-attribute opcode; operand handled above
				break;

			case 0x62:  // observed field marker; this legacy raw-byte path
				// only resets attribute until the following 0x60
				m_curattr = AT_NORMAL;
				break;

			case 0x72:  // end of attributed field
				m_curattr = AT_NORMAL;
				break;

			default:
				// docs/asr10/investigations/display-protocol-inventory.md
				// Del 2: the aggregated, first-occurrence-per-code alarm
				// for this lives in asr10panel_device::send_to_display(),
				// which sees every byte before it reaches here -- no
				// second, redundant alarm mechanism needed in this
				// shared class.
				break;
		}
	} else {
		if ((data >= 0x20) && (data <= 0x5f)) {
			m_chars[0][m_cursx] = data - ' ';
			m_attrs[0][m_cursx] = m_curattr;
			m_dirty[0][m_cursx] = 1;
			m_cursx++;

			if (m_cursx >= 23) {
				m_cursx = 23;
			}
		}
	}

	update_display();
}

void esq1x22_device::render_character(uint8_t column, uint8_t data, bool underline)
{
	if (column >= std::size(m_chars[0]) || data < 0x20 || data > 0x5f)
		return;

	m_chars[0][column] = data - ' ';
	m_attrs[0][column] = underline ? AT_UNDERLINE : AT_NORMAL;
	m_dirty[0][column] = 1;
	update_display();
}

void esq1x22_device::set_underline(uint8_t column, bool underline)
{
	if (column >= std::size(m_chars[0]))
		return;

	uint8_t const old_attr = m_attrs[0][column];
	uint8_t const new_attr = underline ? (old_attr | AT_UNDERLINE) : (old_attr & ~AT_UNDERLINE);
	if (old_attr != new_attr)
	{
		m_attrs[0][column] = new_attr;
		m_dirty[0][column] = 1;
		update_display();
	}
}

esq1x22_device::esq1x22_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqvfd_device(mconfig, ESQ1X22, tag, owner, clock, 1, 22)
{
}

/* SQ-1 display, I think it's really an LCD but we'll deal with it for now */
void esq2x40_sq1_device::device_add_mconfig(machine_config &config)
{
	config.set_default_layout(layout_esq2by40);  // we use the normal 2x40 layout
}

void esq2x40_sq1_device::write_char(uint8_t data)
{
	if (data == 0x09) {
		// musical note
		data = '^'; // approximate for now
	}

	if (m_wait87shift) {
		m_cursy = (data >> 4) & 0xf;
		m_cursx = data & 0xf;
		m_wait87shift = false;
	} else if (m_wait88shift) {
		m_wait88shift = false;
	} else if ((data >= 0x20) && (data <= 0x7f)) {
		m_chars[m_cursy][m_cursx] = data - ' ';
		m_attrs[m_cursy][m_cursx] = m_curattr;
		m_dirty[m_cursy][m_cursx] = 1;
		m_cursx++;

		if (m_cursx >= 39) {
			m_cursx = 39;
		}

		update_display();
	} else if (data == 0x83) {
		m_cursx = m_cursy = 0;
		memset(m_chars, 0, sizeof(m_chars));
		memset(m_attrs, 0, sizeof(m_attrs));
		memset(m_dirty, 1, sizeof(m_dirty));
	} else if (data == 0x87) {
		m_wait87shift = true;
	} else if (data == 0x88) {
		m_wait88shift = true;
	} else {
//        printf("SQ-1 unhandled display char %02x\n", data);
	}
}

esq2x40_sq1_device::esq2x40_sq1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqvfd_device(mconfig, ESQ2X40_SQ1, tag, owner, clock, 2, 40)
{
	m_wait87shift = false;
	m_wait88shift = false;
}

esq2x40_device::esq2x40_device(
	const machine_config &mconfig,
	device_type type,
	const char *tag,
	device_t *owner,
	uint32_t clock) :
	esqvfd_device(mconfig, type, tag, owner, clock, 2, 40)
{
}

esq2x40_device::esq2x40_device(
	const machine_config &mconfig,
	const char *tag,
	device_t *owner,
	uint32_t clock) :
	esq2x40_device(mconfig, ESQ2X40, tag, owner, clock)
{
}
