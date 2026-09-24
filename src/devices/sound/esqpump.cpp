// license:BSD-3-Clause
// copyright-holders:Christian Brunschen
/***************************************************************************

  Ensoniq 5505/5506 to 5510 interface.

  Modeled specifically after the routing of and for use with
  the VFX family of keyboards.

  By Christian Brunschen

***************************************************************************/

#include "emu.h"
#include "esqpump.h"

DEFINE_DEVICE_TYPE(ESQ_5505_5510_PUMP, esq_5505_5510_pump_device, "esq_5505_5510_pump", "Ensoniq 5505/5506 to 5510 interface")

esq_5505_5510_pump_device::esq_5505_5510_pump_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ESQ_5505_5510_PUMP, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_esp(*this, finder_base::DUMMY_TAG)
	, m_esp_halted(true)
	, m_serial_route(serial_route::vfx)
	, ticks_spent_processing(0)
	, samples_processed(0)
{
}

void esq_5505_5510_pump_device::device_start()
{
	// The VFX only has a single pair of stereo outputs, 'Main'; these will be channels 0 and 1,
	// and will be routed to the 'speaker' output device.
	// VFX-SD and later have a separate 'Aux' stereo output that bypasses ESP effect processing;
	// these will be channels 2 and 3 and can be routed to a separate 'aux' output device.
	// On the VFX, those will simply remain silent.
	m_stream = stream_alloc(8, 4, clock(), STREAM_SYNCHRONOUS);

	save_item(NAME(m_esp_halted));
	save_item(NAME(m_serial_route));

#if PUMP_DETECT_SILENCE
	silent_for = 500;
	was_silence = 1;
#endif
#if !PUMP_FAKE_ESP_PROCESSING
	ticks_spent_processing = 0;
	samples_processed = 0;
#endif
#if PUMP_TRACK_SAMPLES
	last_samples = 0;
	last_ticks = osd_ticks();
	next_report_ticks = last_ticks + osd_ticks_per_second();
#endif

#if !PUMP_FAKE_ESP_PROCESSING && PUMP_REPLACE_ESP_PROGRAM
	e.resize(0x4000);
	ei = 0;
#endif
}

void esq_5505_5510_pump_device::device_clock_changed()
{
	m_stream->set_sample_rate(clock());
}

void esq_5505_5510_pump_device::sound_stream_update(sound_stream &stream)
{
	constexpr sound_stream::sample_t input_scale = 32767.0;
	constexpr sound_stream::sample_t output_scale = 1.0 / input_scale;

	auto to_esp_sample = [](sound_stream::sample_t sample) -> int16_t {
		return int16_t(std::clamp<s32>(s32(sample * input_scale), -32768, 32767));
	};

	const bool esp_halted = m_esp_halted || m_esp->get_HALT();

	if (m_serial_route == serial_route::vfx)
	{
		// Push the 'Aux' output samples directly into the output stream
		stream.put(2, 0, stream.get(0, 0));
		stream.put(3, 0, stream.get(1, 0));

		// Push the 'FX1', 'FX2' and 'DRY' samples into the ESP
		if (!esp_halted)
		{
			m_esp->ser_w(0, to_esp_sample(stream.get(2, 0)));
			m_esp->ser_w(1, to_esp_sample(stream.get(3, 0)));
			m_esp->ser_w(2, to_esp_sample(stream.get(4, 0)));
			m_esp->ser_w(3, to_esp_sample(stream.get(5, 0)));
			m_esp->ser_w(4, to_esp_sample(stream.get(6, 0)));
			m_esp->ser_w(5, to_esp_sample(stream.get(7, 0)));
		}
	}
	else
	{
		// Functional serial contract: inputs 2/3, 6/7 and 0/1 respectively
		// feed SER0, SER2 and SER3; SER1 is the processed stereo result.
		stream.put(2, 0, 0.0);
		stream.put(3, 0, 0.0);
		if (!esp_halted)
		{
			m_esp->ser_w(0, to_esp_sample(stream.get(2, 0)));
			m_esp->ser_w(1, to_esp_sample(stream.get(3, 0)));
			m_esp->ser_w(4, to_esp_sample(stream.get(6, 0)));
			m_esp->ser_w(5, to_esp_sample(stream.get(7, 0)));
			m_esp->ser_w(6, to_esp_sample(stream.get(0, 0)));
			m_esp->ser_w(7, to_esp_sample(stream.get(1, 0)));
		}
	}

#if PUMP_FAKE_ESP_PROCESSING
	m_esp->ser_w(6, m_esp->ser_r(0) + m_esp->ser_r(2) + m_esp->ser_r(4));
	m_esp->ser_w(7, m_esp->ser_r(1) + m_esp->ser_r(3) + m_esp->ser_r(5));
#else
	if (!esp_halted) {
#if PUMP_TRACK_SAMPLES
		osd_ticks_t a = osd_ticks();
		m_esp->run_once();
		osd_ticks_t b = osd_ticks();
		ticks_spent_processing += (b - a);
		samples_processed++;
#else
		m_esp->run_once();
#endif
	}
#endif

	// Read the configured processed result from the ESP.  The functional
	// multi-input route falls back to its dry source while the caller keeps an
	// unknown/incomplete image halted; once released, SER1 is its only main
	// output.
	sound_stream::sample_t l;
	sound_stream::sample_t r;
	if (m_serial_route == serial_route::ser0_ser2_ser3_to_ser1 && esp_halted)
	{
		l = stream.get(0, 0);
		r = stream.get(1, 0);
	}
	else
	{
		const int output_port = (m_serial_route == serial_route::vfx) ? 6 : 2;
		l = std::clamp<sound_stream::sample_t>(sound_stream::sample_t(m_esp->ser_r(output_port + 0)) * output_scale, -1.0, 1.0);
		r = std::clamp<sound_stream::sample_t>(sound_stream::sample_t(m_esp->ser_r(output_port + 1)) * output_scale, -1.0, 1.0);
	}

#if !PUMP_FAKE_ESP_PROCESSING && PUMP_REPLACE_ESP_PROGRAM
	// if we're processing the fake program through the ESP, the result should just be that of adding the inputs
	sound_stream::sample_t el = (stream.get(2, 0)) + (stream.get(4, 0)) + (stream.get(6, 0));
	sound_stream::sample_t er = (stream.get(3, 0)) + (stream.get(5, 0)) + (stream.get(7, 0));
	sound_stream::sample_t e_next = el + er;
	e[(ei + 0x1d0f) % 0x4000] = e_next;

	if (fabs(l - e[ei]) > 1e-5) {
		util::stream_format(std::cerr, "expected (%d) but have (%d)\n", e[ei], l);
	}
	ei = (ei + 1) % 0x4000;
#endif

	// Write the Processed samples to the output
	stream.put(0, 0, l);
	stream.put(1, 0, r);

#if PUMP_DETECT_SILENCE
	if (left.get(0) == 0 && right.get(0) == 0) {
		silent_for++;
	} else {
		silent_for = 0;
	}
	bool silence = silent_for >= 500;
	if (was_silence != silence) {
		if (!silence) {
			util::stream_format(std::cerr, ".-*\n");
		} else {
			util::stream_format(std::cerr, "*-.\n");
		}
		was_silence = silence;
	}
#endif

#if PUMP_TRACK_SAMPLES
	last_samples += samples;
	osd_ticks_t now = osd_ticks();
	if (now >= next_report_ticks)
	{
		osd_ticks_t elapsed = now - last_ticks;
		osd_ticks_t tps = osd_ticks_per_second();
		util::stream_format(std::cerr, "Pump: %d samples in %d ticks for %f Hz\n", last_samples, elapsed, last_samples * (double)tps / (double)elapsed);
		last_ticks = now;
		while (next_report_ticks <= now) {
			next_report_ticks += tps;
		}
		last_samples = 0;

#if !PUMP_FAKE_ESP_PROCESSING
		util::stream_format(std::cerr, "  ESP spent %d ticks on %d samples, %f ticks per sample\n", ticks_spent_processing, samples_processed, (double)ticks_spent_processing / (double)samples_processed);
		ticks_spent_processing = 0;
		samples_processed = 0;
#endif
	}
#endif
}
