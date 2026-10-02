// license:BSD-3-Clause
// copyright-holders:R. Belmont
#ifndef MAME_ENSONIQ_ESQPANEL_H
#define MAME_ENSONIQ_ESQPANEL_H

#pragma once

#include "esqvfd.h"
#include "esqlcd.h"

#include "diserial.h"

#include <array>
#include <iostream>
#include <set>
#include <vector>


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> esqpanel_device

namespace esqpanel {
	class external_panel_server;
}

class esqpanel_device : public device_t, public device_serial_interface
{
public:
	auto write_tx() { return m_write_tx.bind(); }
	auto write_analog() { return m_write_analog.bind(); }

	void xmit_char(uint8_t data);
	virtual void set_analog_value(offs_t offset, uint16_t value);
	virtual void set_button(uint8_t button, bool pressed);
	virtual void key_down(uint8_t key, uint8_t velocity);
	virtual void key_pressure(uint8_t key, uint8_t pressure);
	virtual void key_up(uint8_t key);
	virtual void set_floppy_active(bool floppy_active) { }

protected:
	// construction/destruction
	esqpanel_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;

	// serial overrides
	virtual void rcv_complete() override;    // Rx completed receiving byte
	virtual void tra_complete() override;    // Tx completed sending byte
	virtual void tra_callback() override;    // Tx send bit

	virtual void send_to_display(uint8_t data) = 0;
	virtual void debug_rx_complete(uint8_t data) { }
	virtual void debug_send_to_display(uint8_t data) { }
	virtual void debug_xmit_char(uint8_t data) { }
	virtual void debug_tra_complete() { }

	std::set<int> m_pressed_buttons;
	TIMER_CALLBACK_MEMBER(check_external_panel_server);

	virtual const std::string get_front_panel_html_file() const { return ""; }
	virtual const std::string get_front_panel_js_file() const { return ""; }
	virtual bool write_contents(std::ostream &o) { return false; }

	std::vector<uint8_t> m_light_states;

	bool m_eps_mode = false;
	bool m_expect_calibration_second_byte = false;
	bool m_expect_light_second_byte = false;

	esqpanel::external_panel_server *m_external_panel_server;

private:
	static const int XMIT_RING_SIZE = 16;

	devcb_write_line m_write_tx;
	devcb_write16 m_write_analog;
	uint8_t m_xmitring[XMIT_RING_SIZE];
	int m_xmit_read, m_xmit_write = 0;
	bool m_tx_busy = false;
	// xmit_char() had no overflow check at all -- confirmed to actually
	// drop bytes under fast play, not just theoretically
	// (docs/asr10/investigations/keyboard-and-sample-bridge.md: a
	// 13-key stress press lost 32 of 52 expected bytes). Counted and
	// logged loudly now instead of silently overwritten, same principle
	// as the rest of the mc68302 consolidation.
	unsigned m_xmit_overflow_count = 0;

	emu_timer *m_external_timer = nullptr;
};

class esqpanel1x22_device : public esqpanel_device {
public:
	esqpanel1x22_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	virtual void send_to_display(uint8_t data) override { m_vfd->write_char(data); }

	required_device<esq1x22_device> m_vfd;
};

class asr10panel_device : public esqpanel_device {
public:
	asr10panel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	std::string annunciator_summary() const;
	std::string unhandled_code_summary() const;

	DECLARE_INPUT_CHANGED_MEMBER(button_change);
	DECLARE_INPUT_CHANGED_MEMBER(analog_value_change);

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override;
	virtual void rcv_complete() override;
	virtual void send_to_display(uint8_t data) override;

	required_device<esq1x22_device> m_vfd;
	output_finder<8> m_instrument_lamps;
	output_finder<8> m_loaded_lamps;
	output_finder<40> m_annunciator_bits;

private:
	std::array<uint8_t, 16> m_ann_left{};
	std::array<uint8_t, 16> m_ann_right{};
	std::array<uint8_t, 8> m_instrument_lamp_state{};
	std::array<uint8_t, 8> m_loaded_lamp_state{};
	bool m_blink_state = false;
	emu_timer *m_ann_blink_timer = nullptr;

	void update_annunciator_outputs();
	void update_indicator_outputs();
	TIMER_CALLBACK_MEMBER(update_annunciator_blink);
	// Authoritative ASR display-protocol state. Character/attribute cells stay
	// in the generic renderer and are updated at explicit logical columns.
	// Level 2: Multi-byte Panel & Annunciator Controls
	uint8_t m_pending_annunciator_command = 0;
	uint8_t m_pending_indicator_cmd = 0;
	uint8_t m_pending_bargraph = 0;
	uint8_t m_vfd_brightness = 0;

	// Level 3: Authoritative ASR display-protocol state
	uint8_t m_display_cursor = 0;
	uint8_t m_selected_field_anchor = 0;
	bool m_pending_field_attr = false;
	bool m_current_underline = false;
	bool m_selected_field_underline = false;
	bool m_selected_field_valid = false;
	bool m_defining_selected_field = false;
	bool m_disable_eps_echo = false;

	// Del 2: an unrecognized display control code says so, once per
	// distinct code, instead of staying silent or flooding output --
	// docs/asr10/investigations/display-protocol-inventory.md Del 2.
	std::array<uint8_t, 256> m_seen_unhandled_display_code{};
	void report_unhandled_display_code(uint8_t data);

};

class esqpanel2x40_device : public esqpanel_device {
public:
	esqpanel2x40_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	virtual void send_to_display(uint8_t data) override { m_vfd->write_char(data); }

	required_device<esq2x40_device> m_vfd;
};

class esqpanel2x40_vfx_device : public esqpanel_device {
public:
	esqpanel2x40_vfx_device(const machine_config &mconfig, const char *tag, device_t *owner, int panel_type = UNKNOWN, uint32_t clock = 0);

	DECLARE_INPUT_CHANGED_MEMBER(button_change);
	DECLARE_INPUT_CHANGED_MEMBER(patch_select_change);
	DECLARE_INPUT_CHANGED_MEMBER(analog_value_change);
	DECLARE_INPUT_CHANGED_MEMBER(key_change);
	void set_floppy_active(bool floppy_active) override;

	void set_family_member(int family_member);

	enum panel_types : int {
		UNKNOWN = 0,
		VFX,
		VFX_SD,
		SD_1,
		SD_1_32
	};

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override;
	virtual void send_to_display(uint8_t data) override { m_vfd->write_char(data); }

	virtual const std::string get_front_panel_html_file() const override { return "/esqpanel/vfx/FrontPanel.html"; }
	virtual const std::string get_front_panel_js_file() const override { return "/esqpanel/vfx/FrontPanel.js"; }
	virtual bool write_contents(std::ostream &o) override;

	static constexpr uint8_t AT_NORMAL      = 0x00;
	static constexpr uint8_t AT_UNDERLINE   = 0x01;
	static constexpr uint8_t AT_BLINK       = 0x02;

private:
	int m_panel_type;

	emu_timer *m_blink_timer = nullptr;
	uint8_t m_blink_phase;

	required_device<esq2x40_vfx_device> m_vfd;

	output_finder<> m_lights;

	required_ioport m_buttons_0;
	required_ioport m_buttons_32;
	required_ioport m_analog_data_entry;
	required_ioport m_analog_volume;

	bool m_floppy_active = false;

	TIMER_CALLBACK_MEMBER(update_blink);
	void update_lights();

	ioport_value get_adjuster_value(required_ioport &ioport);
	void set_adjuster_value(required_ioport &ioport, const ioport_value & value);
};

class esqpanel2x40_sq1_device : public esqpanel_device {
public:
	esqpanel2x40_sq1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	virtual void send_to_display(uint8_t data) override { m_vfd->write_char(data); }

	required_device<esq2x40_sq1_device> m_vfd;
};

// --- SQ1 - Parduz --------------------------------------------------------------------------------------------------------------------------
class esqpanel2x16_sq1_device : public esqpanel_device {
public:
	esqpanel2x16_sq1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	virtual void send_to_display(uint8_t data) override { m_vfd->write_char(data); }

	required_device<esq2x16_sq1_device> m_vfd;
};

DECLARE_DEVICE_TYPE(ESQPANEL1X22,     esqpanel1x22_device)
DECLARE_DEVICE_TYPE(ASR10PANEL,       asr10panel_device)
DECLARE_DEVICE_TYPE(ESQPANEL2X40,     esqpanel2x40_device)
DECLARE_DEVICE_TYPE(ESQPANEL2X40_VFX, esqpanel2x40_vfx_device)
DECLARE_DEVICE_TYPE(ESQPANEL2X40_SQ1, esqpanel2x40_sq1_device)
DECLARE_DEVICE_TYPE(ESQPANEL2X16_SQ1, esqpanel2x16_sq1_device)

#endif // MAME_ENSONIQ_ESQPANEL_H
