// license:BSD-3-Clause
// copyright-holders:R. Belmont, Parduz
/*
    Ensoniq panel/display device
*/
#include "emu.h"
#include "esqpanel.h"
#include "http.h"
#include "ioport.h"
#include "main.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>


#define VERBOSE 0
#include "logmacro.h"


#include "asr10_panel.lh"
#include "esq2by40_vfx.lh"
#include "sd1.lh"
#include "sd132.lh"
#include "vfx.lh"
#include "vfxsd.lh"


//**************************************************************************
// External panel support
//**************************************************************************


namespace esqpanel {

	class external_panel;

	using external_panel_ptr = std::shared_ptr<external_panel>;
	typedef std::map<http_manager::websocket_connection_ptr, external_panel_ptr, std::owner_less<http_manager::websocket_connection_ptr>> connection_to_panel_map;

	enum message_type {
		UNKNOWN = 0,
		ANALOG = 1 << 0,
		BUTTON = 1 << 1,
		CONTROL = 1 << 2,
		DISPLAY = 1 << 3,
		INFO = 1 << 4
	};

	class external_panel
	{
	public:
		static int get_message_type(const char c)
		{
			switch(c)
			{
			case 'A':
				return message_type::ANALOG;
			case 'B':
				return message_type::BUTTON;
			case 'C':
				return message_type::CONTROL;
			case 'D':
				return message_type::DISPLAY;
			case 'I':
				return message_type::INFO;
			default:
				return message_type::UNKNOWN;
			}
		}

		external_panel() : m_send_message_types(0)
		{
			// printf("session: constructed\n");
		}

		int handle_control_message(const std::string &command)
		{
			int old = m_send_message_types;
			std::istringstream is(command);
			if (get_message_type(is.get()) != message_type::CONTROL)
			{
				return 0;
			}

			int c;
			while ((c = is.get()) != EOF)
			{
				int message_type = external_panel::get_message_type(char(uint8_t(unsigned(c))));
				int n;
				is >> n;
				if (n != 0)
				{
					m_send_message_types |= message_type;
				}
				else
				{
					m_send_message_types &= ~message_type;
				}
			}

			return m_send_message_types ^ old;
		}

		int send_message_types()
		{
			return m_send_message_types;
		}

		bool send_display_data()
		{
			return m_send_message_types & message_type::DISPLAY;
		}

		bool send_analog_values()
		{
			return m_send_message_types & message_type::ANALOG;
		}

		bool send_buttons()
		{
			return m_send_message_types & message_type::BUTTON;
		}

	private:
		int m_send_message_types;
	};

	class external_panel_server
	{
	public:
		enum websocket_opcode {
			text = 1,
			binary = 2
		};
		external_panel_server(http_manager *webserver) :
			m_server(webserver),
			m_keyboard("unknown"),
			m_version("1")
		{
			using namespace std::placeholders;
			if (m_server->is_active())
			{
				m_server->add_endpoint("/esqpanel/socket",
						std::bind(&external_panel_server::on_open, this, _1),
						std::bind(&external_panel_server::on_message, this, _1, _2, _3),
						std::bind(&external_panel_server::on_close, this, _1, _2, _3),
						std::bind(&external_panel_server::on_error, this, _1, _2)
						);
			}
		}

		virtual ~external_panel_server()
		{
			if (m_server->is_active())
			{
				m_server->remove_endpoint("/esqpanel/socket");
			}
		}

		void send_to_all(char c)
		{
			// printf("server: send_to_all(%02x)\n", ((unsigned int) c) & 0xff);
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			// printf("server: sending '%02x' to all\n", ((unsigned int) c) & 0xff);
			m_to_send.str("");
			m_to_send.put('D');
			m_to_send.put(c);
			const std::string &s = m_to_send.str();

			for (const auto &iter: m_panels)
			{
				external_panel_ptr panel = iter.second;
				if (panel->send_display_data())
				{
					send(iter.first, s);
				}
			}
		}

		void on_open(http_manager::websocket_connection_ptr connection)
		{
			using namespace std::placeholders;

			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_panels[connection] = std::make_shared<external_panel>();
		}

		void on_message(http_manager::websocket_connection_ptr connection, const std::string &payload, int opcode)
		{
			external_panel_ptr panel = external_panel_for_connection(connection);
			const std::string &command = payload;

			int t = external_panel::get_message_type(command.front());

			if (t == message_type::CONTROL)
			{
				int changed = panel->handle_control_message(command);
				// printf("server: control message, changed = '%x'\n", changed);
				if ((changed & message_type::DISPLAY) && panel->send_display_data())
				{
					// printf("server: control message, sending contents\n");
					send_contents(connection);
				}

				if ((changed & message_type::ANALOG) && panel->send_analog_values())
				{
					// printf("server: control message, sending analog values\n");
					send_analog_values(connection);
				}

				if ((changed & message_type::BUTTON) && panel->send_buttons())
				{
					// printf("server: control message, sending button states\n");
					send_button_states(connection);
				}
			}
			else if (t == message_type::INFO)
			{
				std::ostringstream o;
				o << "I" << get_keyboard() << "," << get_version();
				send(connection, o.str());
			}
			else
			{
				{
					std::lock_guard<std::recursive_mutex> lock(m_mutex);
					m_commands.emplace_back(command);
				}

				// Echo the non-command message to any other connected panels that want it
				for (const auto &iter: m_panels)
				{
					external_panel_ptr other_panel = iter.second;
					if (other_panel != panel && (t & other_panel->send_message_types()) != 0)
					{
						send(iter.first, command);
					}
				}
			}
		}

		void on_close(http_manager::websocket_connection_ptr connection, int status, const std::string& reason)
		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_panels.erase(connection);
		}

		void on_error(http_manager::websocket_connection_ptr connection, const std::error_code& error_code)
		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_panels.erase(connection);
		}

		void on_document_request(http_manager::http_request_ptr request, http_manager::http_response_ptr response, const std::string &filename)
		{
			m_server->serve_document(request, response, filename);
		}

		void on_template_request(http_manager::http_request_ptr request, http_manager::http_response_ptr response, const std::string &filename)
		{
			using namespace std::placeholders;
			m_server->serve_template(request, response, filename, std::bind(&external_panel_server::get_template_value, this, _1), '$', '$');
		}

		external_panel_ptr external_panel_for_connection(http_manager::websocket_connection_ptr connection)
		{
			auto it = m_panels.find(connection);

			if (it == m_panels.end())
			{
				// this connection is not in the list. This really shouldn't happen
				// and probably means something else is wrong.
				throw std::invalid_argument("No panel avaliable for connection");
			}

			return it->second;
		}

		bool has_commands()
		{
			// printf("server: has_commands()\n");
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			return !m_commands.empty();
		}

		std::string get_next_command()
		{
			// printf("server: get_next_command()\n");
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			std::string command = std::move(m_commands.front());
			m_commands.pop_front();
			return command;
		}

		void set_index(const std::string &index)
		{
			m_index = index;
		}

		void add_http_document(const std::string &path, const std::string &filename)
		{
			m_server->remove_http_handler(path);
			if (filename != "")
			{
				using namespace std::placeholders;
				m_server->add_http_handler(path, std::bind(&external_panel_server::on_document_request, this, _1, _2, filename));
			}
		}

		void add_http_template(const std::string &path, const std::string &filename)
		{
			m_server->remove_http_handler(path);
			if (filename != "")
			{
				using namespace std::placeholders;
				m_server->add_http_handler(path, std::bind(&external_panel_server::on_template_request, this, _1, _2, filename));
			}
		}

		void set_content_provider(std::function<bool(std::ostream&)> provider)
		{
			m_content_provider = provider;
		}

		void set_keyboard(const std::string &keyboard)
		{
			m_keyboard = keyboard;
		}

		const std::string &get_keyboard() const
		{
			return m_keyboard;
		}

		const std::string &get_version() const
		{
			return m_version;
		}

		bool get_template_value(std::string &s)
		{
			if (s == "keyboard")
			{
				s = m_keyboard;
				return true;
			}
			else if (s == "version")
			{
				s = m_version;
				return true;
			}
			else
			{
				return false;
			}
		}

	private:
		void send(http_manager::websocket_connection_ptr connection, const std::string &s)
		{
			connection->send_message(s, websocket_opcode::binary);
		}

		void send_contents(http_manager::websocket_connection_ptr connection)
		{
			if (m_content_provider)
			{
				m_to_send.str("");
				m_to_send.put('D');
				if (m_content_provider(m_to_send))
				{
					send(connection, m_to_send.str());
				}
			}
		}

		void send_analog_values(http_manager::websocket_connection_ptr connection)
		{
			// TODO(cbrunschen): get the current analog values and send them
		}

		void send_button_states(http_manager::websocket_connection_ptr connection)
		{
			// TODO(cbrunschen): track current button states and send them
		}

		http_manager *m_server;
		std::recursive_mutex m_mutex;

		connection_to_panel_map m_panels;
		std::list<std::string> m_commands;
		std::thread m_working_thread;
		std::ostringstream m_to_send;

		std::string m_index;
		std::string m_keyboard;
		std::string m_version;
		std::function<bool(std::ostream&)> m_content_provider;
		std::map<const std::string, const std::string> m_template_values;
	};

}  // namespace esqpanel

//**************************************************************************
//  MACROS / CONSTANTS
//**************************************************************************

//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

DEFINE_DEVICE_TYPE(ESQPANEL1X22,     esqpanel1x22_device,     "esqpanel122",     "Ensoniq front panel with 1x22 VFD")
DEFINE_DEVICE_TYPE(ASR10PANEL,       asr10panel_device,       "asr10panel",      "Ensoniq ASR-10 front panel with 1x22 VFD and annunciators")
DEFINE_DEVICE_TYPE(ESQPANEL2X40,     esqpanel2x40_device,     "esqpanel240",     "Ensoniq front panel with 2x40 VFD")
DEFINE_DEVICE_TYPE(ESQPANEL2X40_VFX, esqpanel2x40_vfx_device, "esqpanel240_vfx", "Ensoniq front panel with 2x40 VFD for VFX family")
DEFINE_DEVICE_TYPE(ESQPANEL2X16_SQ1, esqpanel2x16_sq1_device, "esqpanel216_sq1", "Ensoniq front panel with 2x16 LCD")

//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

//-------------------------------------------------
//  esqpanel_device - constructor
//-------------------------------------------------

esqpanel_device::esqpanel_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, type, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	m_light_states(0x40), // maximum number of lights
	m_write_tx(*this),
	m_write_analog(*this)
{
	std::fill(std::begin(m_xmitring), std::end(m_xmitring), 0);
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void esqpanel_device::device_start()
{
	m_external_panel_server = new esqpanel::external_panel_server(machine().manager().http());
	if (machine().manager().http()->is_active())
	{
		m_external_panel_server->set_keyboard(owner()->shortname());
		m_external_panel_server->set_index("/esqpanel/FrontPanel.html");
		m_external_panel_server->add_http_template("/esqpanel/FrontPanel.html", get_front_panel_html_file());
		m_external_panel_server->add_http_document("/esqpanel/FrontPanel.js", get_front_panel_js_file());
		m_external_panel_server->set_content_provider([this](std::ostream& o)
		{
			return write_contents(o);
		});

		m_external_timer = timer_alloc(FUNC(esqpanel_device::check_external_panel_server), this);
		m_external_timer->enable(false);
	}
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void esqpanel_device::device_reset()
{
	device_t::device_reset();

	// panel comms is at 62500 baud (double the MIDI rate), 8N2
	set_data_frame(1, 8, PARITY_NONE, STOP_BITS_2);
	set_rcv_rate(62500);
	set_tra_rate(62500);

	m_tx_busy = false;
	m_xmit_read = m_xmit_write = 0;
	m_expect_calibration_second_byte = false;
	m_expect_light_second_byte = false;

	attotime sample_time(0, ATTOSECONDS_PER_MILLISECOND);
	attotime initial_delay(0, ATTOSECONDS_PER_MILLISECOND);

	if (m_external_timer)
	{
		m_external_timer->adjust(initial_delay, 0, sample_time);
		m_external_timer->enable(true);
	}
}

//-------------------------------------------------
//  device_stop - device-specific stop
//-------------------------------------------------

void esqpanel_device::device_stop()
{
	device_t::device_stop();

	delete m_external_panel_server;
	m_external_panel_server = nullptr;
}

void esqpanel_device::rcv_complete()    // Rx completed receiving byte
{
	receive_register_extract();
	uint8_t data = get_received_char();
	debug_rx_complete(data);

//  if (data >= 0xe0) LOG("Got %02x from motherboard (second %s)\n", data, m_expect_calibration_second_byte ? "yes" : "no");

	// Set this to `true` to prevent this byte to be sent to the display.
	// This lets us avoid sending keyboard calibration and light related
	// commands that the display just has to ignore anyway.
	bool skip_display = false;

	m_external_panel_server->send_to_all(data);

	if (m_expect_calibration_second_byte)
	{
		skip_display = true;
//      LOG("second byte is %02x\n", data);
		if (data == 0xfd)   // calibration request
		{
//          LOG("let's send reply!\n");
			xmit_char(0xff);   // this is the correct response for "calibration OK"
		}
		m_expect_calibration_second_byte = false;
	}
	else if (m_expect_light_second_byte)
	{
		skip_display = true;

		// Lights on the Buttons, on the VFX-SD:
		// Number   Button
		// 0        1-6
		// 1        8
		// 2        6
		// 3        4
		// 4        2
		// 5        Compare
		// 6        1
		// 7        Presets
		// 8        7-12
		// 9        9
		// a        7
		// b        5
		// c        3
		// d        Sounds
		// e        0
		// f        Cart
		int light_number = data & 0x3f;

		// Light states:
		// 0 = Off
		// 2 = On
		// 3 = Blinking
		m_light_states[light_number] = (data & 0xc0) >> 6;
		m_expect_light_second_byte = false;
	}
	else if (data == 0xfb)   // request calibration
	{
		skip_display = true;
		m_expect_calibration_second_byte = true;
	}
	else if (data == 0xff)  // button light state command
	{
		skip_display = true;
		m_expect_light_second_byte = true;
	}
	else
	{
		// EPS wants a throwaway reply byte for each byte sent to the KPC
		// VFX-SD and SD-1 definitely don't :)
		if (m_eps_mode)
		{
			if (data == 0xe7)
			{
				xmit_char(0x00);   // actual value of response is never checked
			}
			else if (data == 0x71)
			{
				xmit_char(0x00);   // actual value of response is never checked
			}
			else
			{
				xmit_char(data);   // actual value of response is never checked
			}
		}
	}

	// If this was not inhibited, send this to the display as well.
	if (!skip_display)
	{
		debug_send_to_display(data);
		send_to_display(data);
	}
}

void esqpanel_device::tra_complete()    // Tx completed sending byte
{
//  LOG("panel Tx complete\n");
	debug_tra_complete();
	// is there more waiting to send?
	if (m_xmit_read != m_xmit_write)
	{
		transmit_register_setup(m_xmitring[m_xmit_read++]);
		if (m_xmit_read >= XMIT_RING_SIZE)
		{
			m_xmit_read = 0;
		}
	}
	else
	{
		m_tx_busy = false;
	}
}

void esqpanel_device::tra_callback()    // Tx send bit
{
	m_write_tx(transmit_register_get_data_bit());
}

void esqpanel_device::xmit_char(uint8_t data)
{
//  LOG("Panel: xmit %02x\n", data);
	debug_xmit_char(data);

	// if tx is busy it'll pick this up automatically when it completes
	if (!m_tx_busy)
	{
		m_tx_busy = true;
		transmit_register_setup(data);
	}
	else
	{
		// tx is busy, it'll pick this up next time -- unless the ring is
		// already full, in which case the byte about to be pushed would
		// silently overwrite one tra_complete() hasn't drained yet.
		// Measured actually happening under fast play, not just
		// theoretically possible: docs/asr10/investigations/
		// keyboard-and-sample-bridge.md's 13-key stress test lost 32 of
		// 52 expected bytes with no prior check at all. Dropped and
		// counted loudly here instead.
		int next_write = m_xmit_write + 1;
		if (next_write >= XMIT_RING_SIZE)
			next_write = 0;
		if (next_write == m_xmit_read)
		{
			// logerror() alone is not loud enough for this project's own
			// headless testing constraints: its callback is only
			// registered when -log is passed (src/emu/machine.cpp:289,
			// checked directly), and -log is forbidden here. osd_printf_
			// error() always prints, -log or not -- confirmed against
			// this project's own captured run output already showing
			// unconditional osd-level messages (e.g. MAME's own
			// install_read_tap range errors) with no -log in play.
			m_xmit_overflow_count++;
			logerror("esqpanel: XMIT_RING_SIZE overflow (count=%u), dropping byte %02x\n", m_xmit_overflow_count, data);
			osd_printf_error("esqpanel: XMIT_RING_SIZE overflow (count=%u), dropping byte %02x\n", m_xmit_overflow_count, data);
			return;
		}
		m_xmitring[m_xmit_write++] = data;
		if (m_xmit_write >= XMIT_RING_SIZE)
		{
			m_xmit_write = 0;
		}
	}
}

TIMER_CALLBACK_MEMBER(esqpanel_device::check_external_panel_server)
{
	while (m_external_panel_server->has_commands())
	{
		std::string command = m_external_panel_server->get_next_command();
		int l = command.length();
		if (l > 0)
		{
			std::istringstream is(command);
			char c;
			is >> c;
			if (c == 'B')
			{
				// button
				char ud;
				is >> ud;
				int button;
				is >> button;
				bool down = ud == 'D';
				uint8_t sendme = (down ? 0x80 : 0) | (button & 0xff);
				// printf("button %d %s : sending char to mainboard: %02x\n", button, down ? "down" : "up", sendme);
				xmit_char(sendme);
				xmit_char(0x00);
			}
			else if (c == 'A')
			{
				// analog value from ES5505 OTIS: 10 bits, left-aligned within 16 bits.
				int channel, value;
				is >> channel;
				is >> value;
				uint16_t analog_value = (value << 6);
				// printf("analog: channel %d, value %d = %04x\n", channel, value, analog_value);
				set_analog_value(channel, analog_value);
			}
		}
	}
}

void esqpanel_device::set_analog_value(offs_t offset, uint16_t value)
{
	m_write_analog(offset, value);
}

void esqpanel_device::set_button(uint8_t button, bool pressed)
{
	// LOG("set_button(%d, %d)\r\n", button, pressed);
	bool current = m_pressed_buttons.find(button) != m_pressed_buttons.end();
	if (pressed == current)
	{
		// LOG("- button %d already %d, skipping\r\n", button, pressed);
		return;
	}

	uint8_t sendme = (pressed ? 0x80 : 0) | (button & 0xff);
	// LOG("button %d %s : sending char to mainboard: %02x\n", button, pressed ? "down" : "up", sendme);
	xmit_char(sendme);
	xmit_char(0x00);
	if (pressed)
	{
		m_pressed_buttons.insert(button);
	}
	else
	{
		m_pressed_buttons.erase(button);
	}
}

void esqpanel_device::key_down(uint8_t key, uint8_t velocity)
{
	velocity = std::clamp<uint8_t>(velocity, 1, 127);

	xmit_char(0x80 | (key & 0x3f));
	xmit_char(velocity);
}

void esqpanel_device::key_pressure(uint8_t key, uint8_t pressure)
{
	pressure = std::min<uint8_t>(pressure, 127);

	xmit_char(0x40 | (key & 0x3f));
	xmit_char(pressure);
}

void esqpanel_device::key_up(uint8_t key)
{
	xmit_char(key & 0x3f);
	xmit_char(0x40);
}

/* panel with 1x22 VFD display used in the EPS-16 and EPS-16 Plus */

void esqpanel1x22_device::device_add_mconfig(machine_config &config)
{
	ESQ1X22(config, m_vfd, 60);
}


esqpanel1x22_device::esqpanel1x22_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqpanel_device(mconfig, ESQPANEL1X22, tag, owner, clock),
	m_vfd(*this, "vfd")
{
	m_eps_mode = true;
}

void asr10panel_device::device_add_mconfig(machine_config &config)
{
	ESQ1X22(config, m_vfd, 60);
	config.set_default_layout(layout_asr10_panel);
}

void asr10panel_device::device_start()
{
	esqpanel_device::device_start();

	save_item(NAME(m_annunciator_state));
	save_item(NAME(m_instrument_lamp_state));
	save_item(NAME(m_text_chars));
	save_item(NAME(m_text_position));
	save_item(NAME(m_pending_annunciator_command));
	save_item(NAME(m_pending_field_attr));
	save_item(NAME(m_seen_unhandled_display_code));
}

void asr10panel_device::device_reset()
{
	esqpanel_device::device_reset();

	// ASR-10 channel B measured cadence: host writes are paced by one 62500
	// baud character time each way (176 us TX + 176 us $ff reply). No vendor
	// source in docs/asr10/sources/ currently identifies a different panel
	// clock, so keep the ASR-specific rate explicit here rather than relying
	// on the EPS base-class default.
	set_rcv_rate(62500);
	set_tra_rate(62500);

	m_annunciator_state.fill(0);
	m_instrument_lamp_state.fill(0);
	m_text_chars.fill(' ');
	m_text_position = 0;
	m_pending_annunciator_command = 0;
	m_pending_field_attr = false;
	m_seen_unhandled_display_code.fill(0);
	m_disable_eps_echo = std::getenv("ASR10_PANEL_DISABLE_ECHO") != nullptr;

	for (u32 index = 0; index != m_annunciator_state.size(); index++)
		m_annunciator_regs[index] = 0;
	for (u32 index = 0; index != m_instrument_lamp_state.size(); index++)
		m_instrument_lamps[index] = 0;
	for (u32 index = 0; index != m_annunciator_bits.size(); index++)
		m_annunciator_bits[index] = 0;
}

void asr10panel_device::rcv_complete()
{
	receive_register_extract();
	const uint8_t data = get_received_char();
	debug_rx_complete(data);

	// ASR-10 uses the EPS-family two-byte panel protocol for keys, but boot
	// stalls in LOADING SYSTEM if display/scan traffic is echoed. The observed
	// idle response on the working ASR path is $ff.
	if (!m_disable_eps_echo)
		xmit_char(0xff);

	debug_send_to_display(data);
	send_to_display(data);
}


void asr10panel_device::send_to_display(uint8_t data)
{
	if (m_pending_annunciator_command)
	{
		const u32 reg_index = m_pending_annunciator_command - 0x77;
		m_annunciator_state[reg_index] = data;
		m_annunciator_regs[reg_index] = data;

		// Bit-level fanout: only bit 0 of $77 has a confirmed meaning
		// (docs/asr10/investigations/annunciator-bit-probe.md -- BTN_02
		// from idle FILE LOADED sets it, pressing BTN_02 again on the
		// already-selected instrument clears it, both directions
		// verified), mirrored into the pre-existing instrument-lamp
		// output. The other 39 bits are wired raw, unlabeled, until
		// correlated against more known-lit states.
		for (u32 bit = 0; bit != 8; bit++)
			m_annunciator_bits[reg_index * 8 + bit] = (data >> bit) & 1;
		if (reg_index == 0)
			m_instrument_lamps[0] = data & 1;

		m_pending_annunciator_command = 0;
		return;
	}

	if (data >= 0x77 && data <= 0x7b)
	{
		m_pending_annunciator_command = data;
		return;
	}

	if (m_pending_field_attr)
	{
		// Operand of 0x60 (see below) -- understood, not alarm-worthy.
		// The attribute itself is applied in esq1x22_device::write_char().
		m_pending_field_attr = false;
		m_vfd->write_char(data);
		return;
	}

	if (data == 0x66)
	{
		m_text_chars.fill(' ');
		m_text_position = 0;
	}
	else if (data >= 0x20 && data <= 0x5f)
	{
		// Printable range is recognized regardless of our own 22-char
		// mirror buffer's bounds -- a byte that overflows m_text_chars
		// is still known text (esq1x22_device has its own, separate
		// bounds check), not an unhandled control code.
		if (m_text_position < m_text_chars.size())
			m_text_chars[m_text_position++] = data;
	}
	else if (data == 0x60)
	{
		// Del 3: field-attribute opcode, expects one operand byte next
		// (docs/asr10/investigations/display-protocol-inventory.md).
		m_pending_field_attr = true;
	}
	else if (data != 0x62 && data != 0x72)
	{
		// Del 3: 0x62 ("next field") and 0x72 ("end of field") are
		// understood structurally -- they reset the current text
		// attribute in esq1x22_device::write_char() -- but carry no
		// operand and need no state here. Anything else reaching this
		// branch is genuinely unrecognized.
		report_unhandled_display_code(data);
	}

	m_vfd->write_char(data);
}

void asr10panel_device::report_unhandled_display_code(uint8_t data)
{
	if (m_seen_unhandled_display_code[data])
		return;
	m_seen_unhandled_display_code[data] = 1;
	osd_printf_error("asr10panel: unhandled display control code $%02X (first occurrence)\n", data);
}

std::string asr10panel_device::unhandled_code_summary() const
{
	std::string result;
	for (u32 code = 0; code != m_seen_unhandled_display_code.size(); code++)
	{
		if (m_seen_unhandled_display_code[code])
		{
			if (!result.empty())
				result += ' ';
			result += util::string_format("%02x", code);
		}
	}
	return result;
}

std::string asr10panel_device::current_text() const
{
	return std::string(m_text_chars.begin(), m_text_chars.end());
}

std::string asr10panel_device::annunciator_summary() const
{
	std::string result;
	for (u32 index = 0; index != m_annunciator_state.size(); index++)
	{
		if (index)
			result += ' ';
		result += util::string_format("%02x:%02x", 0x77 + index, m_annunciator_state[index]);
	}
	return result;
}

static INPUT_PORTS_START(asr10panel_device)
#define ASR10_PANEL_BUTTON(mask, name, param) \
	PORT_BIT(mask, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME(name) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), param)

	PORT_START("buttons_0")
	ASR10_PANEL_BUTTON(0x00000001, "BTN_00", 0x00)
	ASR10_PANEL_BUTTON(0x00000002, "BTN_01", 0x01)
	// BTN_02: Instrument/Sequence Track 1, verified dynamically.
	PORT_BIT(0x00000004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_02") PORT_CODE(KEYCODE_1) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x02)
	ASR10_PANEL_BUTTON(0x00000008, "BTN_03", 0x03)
	ASR10_PANEL_BUTTON(0x00000010, "BTN_04", 0x04)
	ASR10_PANEL_BUTTON(0x00000020, "BTN_05", 0x05)
	ASR10_PANEL_BUTTON(0x00000040, "BTN_06", 0x06)
	ASR10_PANEL_BUTTON(0x00000080, "BTN_07", 0x07)
	ASR10_PANEL_BUTTON(0x00000100, "BTN_08", 0x08)
	ASR10_PANEL_BUTTON(0x00000200, "BTN_09", 0x09)
	// BTN_0A/BTN_0B: KEYCODE_UP/DOWN, swapped 2026-08-24
	// (partial-update-position-probe.md Del 5). The pilot keymap had
	// $0A=KEYCODE_DOWN/$0B=KEYCODE_UP; measured against effect, not
	// label, on the VOLUME=99 screen: $0A held the value at its ceiling
	// (no visible change across two presses -- consistent with already
	// being at the top, i.e. genuinely Up), and $0B moved it down by one
	// digit (99->98, genuinely Down). REC SRC Field 2 cycling direction
	// is unaffected by this swap (still $0A/$0B, just the keyboard
	// shortcut now points at the code that actually behaves like Up).
	PORT_BIT(0x00000400, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_0A") PORT_CODE(KEYCODE_UP) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x0a)
	PORT_BIT(0x00000800, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_0B") PORT_CODE(KEYCODE_DOWN) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x0b)
	// BTN_0C/BTN_0D: KEYCODE_LEFT/RIGHT removed, 2026-08-24
	// (panel-button-and-transport-map.md Del 2). Measured directly: from
	// the REC SRC screen these navigate to an unrelated top-level menu
	// (COPY/ERASE/FILTER/SHIFT AUDIO TRACK on repeated presses), not a
	// cursor move -- the pilot keymap's Left/Right label was wrong, not
	// just unverified. Real hardware's Left/Right Arrow raw codes remain
	// unidentified; keeping the wrong keyboard shortcut bound here would
	// actively mislead rather than just be unverified, so these two
	// revert to click-only like the other 60 unidentified buttons.
	ASR10_PANEL_BUTTON(0x00001000, "BTN_0C", 0x0c)
	ASR10_PANEL_BUTTON(0x00002000, "BTN_0D", 0x0d)
	ASR10_PANEL_BUTTON(0x00004000, "BTN_0E", 0x0e)
	ASR10_PANEL_BUTTON(0x00008000, "BTN_0F", 0x0f)
	// BTN_10/BTN_11: KEYCODE_LEFT/RIGHT, measured 2026-08-24
	// (partial-update-position-probe.md Del 5) using the display's own
	// underline output as ground truth, not display text alone: from
	// REC SRC with Field 2 underlined, $10 moves the underline to
	// Field 1 ("INPUTDRY", columns 8-15); $11 from there moves it back
	// to Field 2, staying on the same screen -- exactly the manual's
	// "Left/Right Arrow moves to the next parameter" description, both
	// directions confirmed round-trip.
	PORT_BIT(0x00010000, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_10") PORT_CODE(KEYCODE_LEFT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x10)
	PORT_BIT(0x00020000, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_11") PORT_CODE(KEYCODE_RIGHT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x11)
	ASR10_PANEL_BUTTON(0x00040000, "BTN_12", 0x12)
	ASR10_PANEL_BUTTON(0x00080000, "BTN_13", 0x13)
	ASR10_PANEL_BUTTON(0x00100000, "BTN_14", 0x14)
	// BTN_15: KEYCODE_Q ("Sequence"), added 2026-08-24. Measured: $15
	// reliably lands on a genuine sequence-file listing ("FILE 9
	// TUT0RIAL 5EQ") when a sequence exists on the loaded disk -- the
	// Seq*Song category button. Only assigned a mnemonic because the
	// function is measured, not guessed at.
	PORT_BIT(0x00200000, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_15") PORT_CODE(KEYCODE_Q) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x15)
	ASR10_PANEL_BUTTON(0x00400000, "BTN_16", 0x16)
	ASR10_PANEL_BUTTON(0x00800000, "BTN_17", 0x17)
	ASR10_PANEL_BUTTON(0x01000000, "BTN_18", 0x18)
	ASR10_PANEL_BUTTON(0x02000000, "BTN_19", 0x19)
	ASR10_PANEL_BUTTON(0x04000000, "BTN_1A", 0x1a)
	ASR10_PANEL_BUTTON(0x08000000, "BTN_1B", 0x1b)
	ASR10_PANEL_BUTTON(0x10000000, "BTN_1C", 0x1c)
	ASR10_PANEL_BUTTON(0x20000000, "BTN_1D", 0x1d)
	ASR10_PANEL_BUTTON(0x40000000, "BTN_1E", 0x1e)
	ASR10_PANEL_BUTTON(0x80000000, "BTN_1F", 0x1f)

	PORT_START("buttons_32")
	// BTN_20: KEYCODE_S ("Sample"), added 2026-08-24. Measured (prior
	// task): $20 is Sample*Source Select. Note: KEYCODE_S collides with
	// KEY_Cs (C-sharp) on the note-typing keyboard below -- pressing 'S'
	// fires both. Documented rather than silently avoided, since the
	// mnemonic scheme is keyed to measured function, not collision-free
	// key layout; a future task can pick a different key if this proves
	// disruptive in practice.
	PORT_BIT(0x00000001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_20") PORT_CODE(KEYCODE_S) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x20)
	ASR10_PANEL_BUTTON(0x00000002, "BTN_21", 0x21)
	ASR10_PANEL_BUTTON(0x00000004, "BTN_22", 0x22)
	// BTN_23: Enter*Yes, verified dynamically.
	PORT_BIT(0x00000008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BTN_23") PORT_CODE(KEYCODE_ENTER) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::button_change), 0x23)
	ASR10_PANEL_BUTTON(0x00000010, "BTN_24", 0x24)
	ASR10_PANEL_BUTTON(0x00000020, "BTN_25", 0x25)
	ASR10_PANEL_BUTTON(0x00000040, "BTN_26", 0x26)
	ASR10_PANEL_BUTTON(0x00000080, "BTN_27", 0x27)
	ASR10_PANEL_BUTTON(0x00000100, "BTN_28", 0x28)
	ASR10_PANEL_BUTTON(0x00000200, "BTN_29", 0x29)
	ASR10_PANEL_BUTTON(0x00000400, "BTN_2A", 0x2a)
	ASR10_PANEL_BUTTON(0x00000800, "BTN_2B", 0x2b)
	ASR10_PANEL_BUTTON(0x00001000, "BTN_2C", 0x2c)
	ASR10_PANEL_BUTTON(0x00002000, "BTN_2D", 0x2d)
	ASR10_PANEL_BUTTON(0x00004000, "BTN_2E", 0x2e)
	ASR10_PANEL_BUTTON(0x00008000, "BTN_2F", 0x2f)
	ASR10_PANEL_BUTTON(0x00010000, "BTN_30", 0x30)
	ASR10_PANEL_BUTTON(0x00020000, "BTN_31", 0x31)
	ASR10_PANEL_BUTTON(0x00040000, "BTN_32", 0x32)
	ASR10_PANEL_BUTTON(0x00080000, "BTN_33", 0x33)
	ASR10_PANEL_BUTTON(0x00100000, "BTN_34", 0x34)
	ASR10_PANEL_BUTTON(0x00200000, "BTN_35", 0x35)
	ASR10_PANEL_BUTTON(0x00400000, "BTN_36", 0x36)
	ASR10_PANEL_BUTTON(0x00800000, "BTN_37", 0x37)
	ASR10_PANEL_BUTTON(0x01000000, "BTN_38", 0x38)
	ASR10_PANEL_BUTTON(0x02000000, "BTN_39", 0x39)
	ASR10_PANEL_BUTTON(0x04000000, "BTN_3A", 0x3a)
	ASR10_PANEL_BUTTON(0x08000000, "BTN_3B", 0x3b)
	ASR10_PANEL_BUTTON(0x10000000, "BTN_3C", 0x3c)
	ASR10_PANEL_BUTTON(0x20000000, "BTN_3D", 0x3d)
	ASR10_PANEL_BUTTON(0x40000000, "BTN_3E", 0x3e)
	ASR10_PANEL_BUTTON(0x80000000, "BTN_3F", 0x3f)

#undef ASR10_PANEL_BUTTON

	// 61-key keyboard stimulus, computer-keyboard-driven (esqpanel.h's
	// own comment). One octave, common "music typing" QWERTY layout
	// (Z=C .. M=B, comma=C of the next octave), plus octave shift on
	// minus/equals. Key numbers are computed in key_change() from
	// (m_octave*12 + offset), not hardcoded here -- offset (param) is
	// 0-12 within the octave.
#define ASR10_PANEL_KEY(mask, name, keycode, note_offset) \
	PORT_BIT(mask, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME(name) PORT_CODE(keycode) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::key_change), note_offset)

	PORT_START("keys_0")
	ASR10_PANEL_KEY(0x00000001, "KEY_C",  KEYCODE_Z,      0)
	ASR10_PANEL_KEY(0x00000002, "KEY_Cs", KEYCODE_S,      1)
	ASR10_PANEL_KEY(0x00000004, "KEY_D",  KEYCODE_X,      2)
	ASR10_PANEL_KEY(0x00000008, "KEY_Ds", KEYCODE_D,      3)
	ASR10_PANEL_KEY(0x00000010, "KEY_E",  KEYCODE_C,      4)
	ASR10_PANEL_KEY(0x00000020, "KEY_F",  KEYCODE_V,      5)
	ASR10_PANEL_KEY(0x00000040, "KEY_Fs", KEYCODE_G,      6)
	ASR10_PANEL_KEY(0x00000080, "KEY_G",  KEYCODE_B,      7)
	ASR10_PANEL_KEY(0x00000100, "KEY_Gs", KEYCODE_H,      8)
	ASR10_PANEL_KEY(0x00000200, "KEY_A",  KEYCODE_N,      9)
	ASR10_PANEL_KEY(0x00000400, "KEY_As", KEYCODE_J,     10)
	ASR10_PANEL_KEY(0x00000800, "KEY_B",  KEYCODE_M,     11)
	ASR10_PANEL_KEY(0x00001000, "KEY_C2", KEYCODE_COMMA, 12)
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("OCTAVE_DOWN") PORT_CODE(KEYCODE_MINUS) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::octave_change), 0)
	PORT_BIT(0x00004000, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("OCTAVE_UP") PORT_CODE(KEYCODE_EQUALS) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::octave_change), 1)
#undef ASR10_PANEL_KEY

	PORT_START("analog_data_entry")
	configurer.field_alloc(IPT_ADJUSTER, 0x200, 0x3ff, "Data Entry");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::analog_value_change), 3)

	PORT_START("analog_input_level")
	configurer.field_alloc(IPT_ADJUSTER, 0x200, 0x3ff, "Input Level");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::analog_value_change), 4)

	PORT_START("analog_volume")
	configurer.field_alloc(IPT_ADJUSTER, 0x3ff, 0x3ff, "Volume");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(asr10panel_device::analog_value_change), 5)
INPUT_PORTS_END

ioport_constructor asr10panel_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(asr10panel_device);
}

INPUT_CHANGED_MEMBER(asr10panel_device::button_change)
{
	esqpanel_device::set_button(param, newval != 0);
}

INPUT_CHANGED_MEMBER(asr10panel_device::analog_value_change)
{
	const int channel = param;
	const int clamped = std::clamp(int(newval), 0, 1023);
	set_analog_value(channel, u16(clamped << 6));
}

INPUT_CHANGED_MEMBER(asr10panel_device::key_change)
{
	// Fixed velocity: a plain computer keyboard has no velocity/pressure
	// input at all -- explicitly a simplification (esqpanel.h's own
	// comment), not a modeled MIDI velocity curve.
	static constexpr u8 KEY_VELOCITY = 100;
	const u8 note_offset = u8(param) & 0x0f;
	if (newval)
	{
		const u8 key = std::min<u8>(u8(m_octave * 12 + note_offset), 60);
		m_key_number_for_offset[note_offset] = key;
		key_down(key, KEY_VELOCITY);
	}
	else
	{
		key_up(m_key_number_for_offset[note_offset]);
	}
}

INPUT_CHANGED_MEMBER(asr10panel_device::octave_change)
{
	if (!newval) // act on press only, not release
		return;
	if (param)
		m_octave = std::min(m_octave + 1, 4);
	else
		m_octave = std::max(m_octave - 1, 0);
}

asr10panel_device::asr10panel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqpanel_device(mconfig, ASR10PANEL, tag, owner, clock),
	m_vfd(*this, "vfd"),
	m_annunciator_regs(*this, "asr10_annreg%u", 0U),
	m_instrument_lamps(*this, "asr10_instlamp%u", 0U),
	m_annunciator_bits(*this, "asr10_annbit%u", 0U)
{
	m_eps_mode = true;
}

/* panel with 2x40 VFD display used in the ESQ-1, SQ-80 */

void esqpanel2x40_device::device_add_mconfig(machine_config &config)
{
	ESQ2X40(config, m_vfd, 60);
}


esqpanel2x40_device::esqpanel2x40_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqpanel_device(mconfig, ESQPANEL2X40, tag, owner, clock),
	m_vfd(*this, "vfd")
{
	m_eps_mode = false;
}

/* panel with 2x40 VFD display used in the VFX, VFX-SD, SD-1 series */

void esqpanel2x40_vfx_device::device_add_mconfig(machine_config &config)
{
	ESQ2X40_VFX(config, m_vfd, 60);

	if (m_panel_type == VFX)
		config.set_default_layout(layout_vfx);
	else if (m_panel_type == VFX_SD)
		config.set_default_layout(layout_vfxsd);
	else if (m_panel_type == SD_1)
		config.set_default_layout(layout_sd1);
	else if (m_panel_type == SD_1_32)
		config.set_default_layout(layout_sd132);
	else // lowest common demonimator as the default: just the VFD.
		config.set_default_layout(layout_esq2by40_vfx);
}

esqpanel2x40_vfx_device::esqpanel2x40_vfx_device(const machine_config &mconfig, const char *tag, device_t *owner, int panel_type, uint32_t clock) :
	esqpanel_device(mconfig, ESQPANEL2X40_VFX, tag, owner, clock),
	m_panel_type(panel_type),
	m_vfd(*this, "vfd"),
	m_lights(*this, "lights"),
	m_buttons_0(*this, "buttons_0"),
	m_buttons_32(*this, "buttons_32"),
	m_analog_data_entry(*this, "analog_data_entry"),
	m_analog_volume(*this, "analog_volume")
{
	m_eps_mode = false;
	// The VFX family have 16 lights on the panel.
	m_light_states.resize(16);
}

bool esqpanel2x40_vfx_device::write_contents(std::ostream &o)
{
	m_vfd->write_contents(o);
	for (int i = 0; i < m_light_states.size(); i++)
	{
		o.put((char)(0xff));
		o.put((char)(m_light_states[i] << 6) | i);
	}
	return true;
}

void esqpanel2x40_vfx_device::update_lights()
{
	// set the lights according to their status and blink phase.
	int32_t lights = 0;
	int32_t bit = 1;
	for (int i = 0; i < 16; i++)
	{
		if (m_light_states[i] == 2 || (m_light_states[i] == 3 && ((m_blink_phase & 1) == 0)))
		{
			lights |= bit;
		}
		bit <<= 1;
	}
	// We use the next bit, 16, for the floppy LED
	if (m_floppy_active)
		lights |= 1 << 16;
	m_lights = lights;
}

TIMER_CALLBACK_MEMBER(esqpanel2x40_vfx_device::update_blink)
{
	m_blink_phase = (m_blink_phase + 1) & 3;
	m_vfd->set_blink_on(m_blink_phase & 2);
	update_lights();
}

void esqpanel2x40_vfx_device::device_start()
{
	esqpanel_device::device_start();

	m_blink_timer = timer_alloc(FUNC(esqpanel2x40_vfx_device::update_blink), this);
	m_blink_timer->enable(false);
}

void esqpanel2x40_vfx_device::device_reset()
{
	esqpanel_device::device_reset();

	if (m_blink_timer)
	{
		attotime sample_time(0, 250 * ATTOSECONDS_PER_MILLISECOND);
		attotime initial_delay(0, 250 * ATTOSECONDS_PER_MILLISECOND);

		m_blink_timer->adjust(initial_delay, 0, sample_time);
		m_blink_timer->enable(true);
	}
}

static INPUT_PORTS_START(esqpanel2x40_vfx_device)
	PORT_START("buttons_0")
	for (int i = 0; i < 32; i++)
	{
		PORT_BIT((1 << i), IP_ACTIVE_HIGH, IPT_KEYPAD);
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::button_change), i)
	}

	PORT_START("buttons_32")
	for (int i = 0; i < 32; i++)
	{
		PORT_BIT((1 << i), IP_ACTIVE_HIGH, IPT_KEYPAD);
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::button_change), 32 + i)
	}

	PORT_START("patch_select")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::patch_select_change), 1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::patch_select_change), 1)

	PORT_START("analog_pitch_bend")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Pitch Bend") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(30) PORT_KEYDELTA(15) PORT_CENTERDELTA(128)
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::analog_value_change), 0)

	PORT_START("analog_mod_wheel")
	// An adjuster, but with range 0 .. 1023, to match the 10 bit resolution of the OTIS ADC
	configurer.field_alloc(IPT_ADJUSTER, 0x3ff, 0x3ff, "Modulation");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::analog_value_change), 2)

	PORT_START("analog_data_entry")
	// An adjuster, but with range 0 .. 1023, to match the 10 bit resolution of the OTIS ADC
	configurer.field_alloc(IPT_ADJUSTER, 0x200, 0x3ff, "Data Entry");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::analog_value_change), 3)

	PORT_START("analog_volume")
	// An adjuster, but with range 0 .. 1023, to match the 10 bit resolution of the OTIS ADC
	configurer.field_alloc(IPT_ADJUSTER, 0x3ff, 0x3ff, "Volume");
	configurer.field_set_min_max(0, 0x3ff);
	PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::analog_value_change), 5)

	for (int i = 0; i < 61; i++)
	{
		std::string port_name = util::string_format("key_%d", i);
		PORT_START(port_name.c_str());
		PORT_BIT(0x3fff, 0x0, IPT_PADDLE)
		PORT_GM_NOTE(36 + i)

		// the following must be set ot MAME complains, but we don't use them:
		// we always pass the values through explicitly, overriding anything else.
		PORT_SENSITIVITY(1) PORT_KEYDELTA(1) PORT_CENTERDELTA(1)

		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(esqpanel2x40_vfx_device::key_change), i)
	}

INPUT_PORTS_END

ioport_constructor esqpanel2x40_vfx_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(esqpanel2x40_vfx_device);
}

// A button is pressed on the internal panel
INPUT_CHANGED_MEMBER(esqpanel2x40_vfx_device::button_change)
{
	// Update the internal state
	esqpanel_device::set_button(param, newval != 0);
}

// A Patch Select button is pressed on the internal panel
INPUT_CHANGED_MEMBER(esqpanel2x40_vfx_device::patch_select_change)
{
	// Update the internal state from the full value of the port: presented as an analog value!
	int value = (field.port().read() & 0x03) * 250;
	set_analog_value(1, value << 6);
}

// An anlog value was changed on the internal panel
INPUT_CHANGED_MEMBER(esqpanel2x40_vfx_device::analog_value_change)
{
	int channel = param;
	int clamped = std::clamp((int)newval, 0, 1023);
	int value = clamped << 6;
	set_analog_value(channel, value);
}

// An key changed on the internal panel
INPUT_CHANGED_MEMBER(esqpanel2x40_vfx_device::key_change)
{
	uint8_t key = param & 0x3f;
	uint8_t velocity = newval & 0x7f;

	if (velocity == 0)
	{
		uint8_t old_pressure = (oldval >> 7) & 0x7f;
		if (old_pressure != 0)
		{
			// there was pressure before; reset the pressure to zero before the key-up event.
			key_pressure(key, 0);
		}
		key_up(key);
	}
	else
	{
		uint8_t old_velocity = oldval & 0x7f;
		uint8_t pressure = (newval >> 7) & 0x7f;

		if (old_velocity == 0)
		{
			// this is a key down event. Might also include an ensuing pressure event.
			key_down(key, velocity);
		}

		if (pressure != 0)
		{
			// if we have pressure, then it is (also) a pressure event.
			key_pressure(key, pressure);
		}
	}
}

void esqpanel2x40_vfx_device::set_floppy_active(bool floppy_active)
{
	m_floppy_active = floppy_active;
	update_lights();
}

ioport_value esqpanel2x40_vfx_device::get_adjuster_value(required_ioport &ioport)
{
	auto field = ioport->fields().first();
	ioport_field::user_settings user_settings;
	field->get_user_settings(user_settings);
	return user_settings.value;
}

void esqpanel2x40_vfx_device::set_adjuster_value(required_ioport &ioport, const ioport_value & value)
{
	auto field = ioport->fields().first();
	ioport_field::user_settings user_settings;
	field->get_user_settings(user_settings);
	user_settings.value = value;
	field->set_user_settings(user_settings);
}

// --- SQ1 - Parduz --------------------------------------------------------------------------------------------------------------------------
void esqpanel2x16_sq1_device::device_add_mconfig(machine_config &config)
{
	ESQ2X16_SQ1(config, m_vfd, 60);
}

// --- SQ1 - Parduz --------------------------------------------------------------------------------------------------------------------------
esqpanel2x16_sq1_device::esqpanel2x16_sq1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	esqpanel_device(mconfig, ESQPANEL2X16_SQ1, tag, owner, clock),
	m_vfd(*this, "vfd")
{
	m_eps_mode = false;
}
