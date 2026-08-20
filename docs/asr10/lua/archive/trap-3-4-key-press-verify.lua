-- Verifies whether the already-existing key_down() encoding (0x80|key,
-- nonzero velocity) reaches the TRAP #3/#4 dispatch path found in
-- $FFB43E's disassembly (docs/asr10/investigations/
-- keyboard-and-sample-bridge-2.md Del 2), using the same proven
-- handler-address-plus-PC-correlation technique from
-- mc68302-consolidation-2.md's Del 1 (masked to 24 bits, calibrated
-- method).

local reg = dofile("docs/asr10/lua/lib/asr10_regression.lua")
local display = reg.display
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local pc_state = cpu.state["PC"] or cpu.state["CURPC"] or cpu.state["GENPC"]
local taps = {}
local function now() return emu.time() end
local function pc() return pc_state and (pc_state.value & 0x00ffffff) or 0xffffffff end

-- 1. Trap vector handlers (32+3=35=$23, 32+4=36=$24), masked to 24 bits.
-- Read too early (right after FILE 1, before FILE LOADED), this gave
-- $FFFFFF -- a sentinel/uninitialized pattern, not a real address, and
-- installing a tap on it crashed the run outright (MAME's own address-
-- mask error). Deferred to a function, called just before the key press
-- instead, with a sanity check against exactly that failure mode.
local trap3_hits, trap4_hits = {}, {}
local function install_trap_taps()
  local trap3_addr = prog:read_u32(35 * 4) & 0x00ffffff
  local trap4_addr = prog:read_u32(36 * 4) & 0x00ffffff
  print(string.format("T34_HANDLERS trap3=%06X trap4=%06X", trap3_addr, trap4_addr))
  if trap3_addr >= 0x00fffffe or trap3_addr == 0 then
    print("T34_TRAP3_UNAVAILABLE sentinel_or_zero_address")
  else
    taps[#taps + 1] = prog:install_read_tap(trap3_addr, trap3_addr + 1, "t34_trap3", function(offset, data, mask)
      trap3_hits[#trap3_hits + 1] = { t = now(), pc = pc() }
      return nil
    end)
  end
  if trap4_addr >= 0x00fffffe or trap4_addr == 0 then
    print("T34_TRAP4_UNAVAILABLE sentinel_or_zero_address")
  else
    taps[#taps + 1] = prog:install_read_tap(trap4_addr, trap4_addr + 1, "t34_trap4", function(offset, data, mask)
      trap4_hits[#trap4_hits + 1] = { t = now(), pc = pc() }
      return nil
    end)
  end
end

-- 2. Also directly tap $FFB43E itself (the code we disassembled), and
-- $FFB258 (the D1!=0 branch target inside $FFB20A) as intermediate
-- checkpoints, so we can see exactly how far a real key press gets even
-- if it stops before actually executing a TRAP.
local checkpoint_hits = {}
local function tap_checkpoint(name, addr)
  taps[#taps + 1] = prog:install_read_tap(addr, addr + 1, "t34_cp_" .. name, function(offset, data, mask)
    checkpoint_hits[name] = checkpoint_hits[name] or {}
    table.insert(checkpoint_hits[name], { t = now(), pc = pc() })
    return nil
  end)
end
tap_checkpoint("B20A", 0x00ffb20a)
tap_checkpoint("B258", 0x00ffb258)
tap_checkpoint("B2DC", 0x00ffb2dc)
tap_checkpoint("B43E", 0x00ffb43e)

-- 3. ES5506 voice register decoder (established technique).
local acc_bytes, current_page, voices, voice_write_log = {}, 0, {}, {}
local function finalize_register(register_index, value32)
  if register_index == 15 then current_page = value32 & 0x7f; return end
  local voice_n = current_page & 0x1f
  local group = (current_page < 0x20) and "low" or ((current_page < 0x40) and "high" or "test")
  voices[voice_n] = voices[voice_n] or {}
  local v = voices[voice_n]
  if group == "high" then
    if register_index == 0 then v.cr = value32 & 0xffff; voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR"}
    elseif register_index == 1 then v.start = value32 & 0xfffff800; voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="START"}
    elseif register_index == 2 then v["end"] = value32 & 0xffffff80; voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="END"}
    elseif register_index == 3 then v.accum = value32; voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="ACCUM"}
    end
  elseif group == "low" and register_index == 0 then
    v.cr = value32 & 0xffff; voice_write_log[#voice_write_log+1] = {t=now(), voice=voice_n, field="CR(low)"}
  end
end
taps[#taps + 1] = prog:install_write_tap(0x00fc2000, 0x00fc207f, "t34_es5506_w", function(offset, data, mask)
  local v = data & 0xff
  local word_offset = (offset - 0xfc2000) // 2
  local register_index = word_offset // 4
  local byte_in_reg = word_offset % 4
  acc_bytes[byte_in_reg + 1] = v
  if byte_in_reg == 3 then
    local b0,b1,b2,b3 = acc_bytes[1] or 0, acc_bytes[2] or 0, acc_bytes[3] or 0, acc_bytes[4] or 0
    finalize_register(register_index, (b0<<24)|(b1<<16)|(b2<<8)|b3)
    acc_bytes = {}
  end
  return nil
end)

local function press(port_name, mask, hold_ms)
  local port = manager.machine.ioport.ports[port_name]
  local field = port:field(mask)
  field:set_value(1)
  emu.wait(emu.attotime.from_msec(hold_ms or 80))
  field:clear_value()
  emu.wait(emu.attotime.from_msec(250))
end

local ok, text = reg.wait_for_text("FILE 1  TUT0RIAL BNK  ", 45)
if not ok then
  reg.fail("trap_3_4_key_press_verify", string.format("boot_timeout final_display=\"%s\"", text))
  return
end

press(":panel:buttons_0", 1 << 0x0a)
press(":panel:buttons_32", 1 << 0x03)
press(":panel:buttons_0", 1 << 0x02)

ok, text = reg.wait_for_text("FILE L0ADED           ", 20)
if not ok then
  reg.fail("trap_3_4_key_press_verify", string.format("no_file_loaded final_display=\"%s\"", text))
  return
end
emu.wait(emu.attotime.from_msec(500))

install_trap_taps()
local before_voice_log = #voice_write_log
print(string.format("T34_KEY_PRESS t=%.6f", now()))
press(":panel:keys_0", 0x00000001, 150) -- KEY_C, velocity=100 (already nonzero)
emu.wait(emu.attotime.from_msec(1000))

print(string.format("T34_RESULT trap3_hits=%u trap4_hits=%u voice_writes=%u",
  #trap3_hits, #trap4_hits, #voice_write_log - before_voice_log))
for name, hits in pairs(checkpoint_hits) do
  print(string.format("T34_CHECKPOINT name=%s hits=%u", name, #hits))
  for i = 1, math.min(3, #hits) do
    print(string.format("T34_CHECKPOINT_HIT name=%s t=%.6f pc=%06X", name, hits[i].t, hits[i].pc))
  end
end
for i = before_voice_log + 1, #voice_write_log do
  local e = voice_write_log[i]
  print(string.format("T34_VOICE_WRITE t=%.6f voice=%d field=%s", e.t, e.voice, e.field))
end

print(string.format("T34_SUMMARY final_display=\"%s\" t=%.6f", display.read_raw(), now()))
reg.pass("trap_3_4_key_press_verify", string.format("trap3=%u trap4=%u", #trap3_hits, #trap4_hits))
