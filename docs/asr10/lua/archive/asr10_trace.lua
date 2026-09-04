-- asr10_trace.lua -- generell transaktionslogg for ett adressfonster.
--
-- Ersatter de handskrivna log_*/dump_*-funktionerna i asr10_boot.cpp for
-- allt som bara OBSERVERAR dataaccesser. Kan inte ersatta nagot som driver
-- en avbrottslinje, och ser inte instruktionshamtningar (opcode-vagen gar
-- forbi passthrough-taps).
--
--   SDL_VIDEODRIVER=dummy ASR10_TRACE=duart \
--   ./mess asr10booth -flop1 floppies/asr10booth/V350.img \
--     -video none -sound none -nothrottle -seconds_to_run 30 \
--     -autoboot_script docs/asr10/lua/asr10_trace.lua -autoboot_delay 0 \
--     2>&1 | grep '^TRACE' > trace.txt
--
-- Tva korningar diffas radvis. Forsta raden dar de skiljer sig ar svaret.
-- INGEN tid loggas -- klockor skiljer sig mellan revisioner och ger falska
-- diffar. Bara ordning, adress, riktning, varde.

local WINDOWS = {
  duart   = { 0x00FC4800, 0x00FC481F, "SCN2681" },
  es5506  = { 0x00FC2000, 0x00FC207F, "ES5506 host" },
  es5510  = { 0x00FC3000, 0x00FC31FF, "ES5510 host" },
  sib     = { 0x00FC6800, 0x00FC68FF, "MC68302 SIM" },
  dpram   = { 0x00FC6000, 0x00FC67FF, "MC68302 DPRAM" },
  fdc     = { 0x00FC4000, 0x00FC401F, "uPD72069" },
  lowmem  = { 0x00000000, 0x00000FFF, "lagminne" },
  sched   = { 0x000023F6, 0x0000247B, "schemalaggartabellen" },
}

local MAX = 400000

local sel = "duart"
pcall(function() sel = os.getenv("ASR10_TRACE") or sel end)
local win = WINDOWS[sel]
if not win then
  print("TRACE OKANT FONSTER: " .. tostring(sel))
  local names = {}
  for k in pairs(WINDOWS) do names[#names+1] = k end
  table.sort(names)
  print("TRACE giltiga: " .. table.concat(names, " "))
  return
end

local LO, HI, LABEL = win[1], win[2] | 1, win[3]
_t = { n = 0, r = 0, w = 0, hist = {} }

local function emit(dir, offset, data, mask)
  _t.n = _t.n + 1
  local k = string.format("%06X", offset)
  _t.hist[k] = (_t.hist[k] or 0) + 1
  if _t.n > MAX then return end
  local v = ((mask & 0x00FF) ~= 0) and (data & 0xFF) or ((data >> 8) & 0xFF)
  print(string.format("TRACE %7d %s %06X %02X %s",
    _t.n, dir, offset, v, ((mask & 0x00FF) ~= 0) and "lo" or "hi"))
end

local function tap(prog, kind, name, dir)
  local ok, res = pcall(function()
    if kind == "r" then
      return prog:install_read_tap(LO, HI, name,
        function(o, d, m) _t.r = _t.r + 1; emit(dir, o, d, m); return nil end)
    end
    return prog:install_write_tap(LO, HI, name,
      function(o, d, m) _t.w = _t.w + 1; emit(dir, o, d, m); return nil end)
  end)
  if not ok then print("TRACE TAP MISSLYCKADES: " .. tostring(res)) end
  return res
end

local prog = manager.machine.devices[":maincpu"].spaces["program"]
_t.rt = tap(prog, "r", "asr10_trace_r", "R")
_t.wt = tap(prog, "w", "asr10_trace_w", "W")
print(string.format("TRACE aktiv: %s  %06X-%06X  (%s)", sel, LO, HI, LABEL))

_t.stop = emu.add_machine_stop_notifier(function()
  print(string.format("TRACE slut: %d accesser (%d las, %d skriv)%s",
    _t.n, _t.r, _t.w, _t.n > MAX and "  -- TAK NATT" or ""))
  if _t.n == 0 then
    print("TRACE ROKTEST MISSLYCKAD: noll accesser. Jamfor inte.")
    return
  end
  local ks = {}
  for k in pairs(_t.hist) do ks[#ks+1] = k end
  table.sort(ks, function(a, b) return _t.hist[a] > _t.hist[b] end)
  local ps = {}
  for i = 1, math.min(#ks, 24) do ps[#ps+1] = ks[i] .. "x" .. _t.hist[ks[i]] end
  print("TRACE histogram: " .. table.concat(ps, "  "))
end)
