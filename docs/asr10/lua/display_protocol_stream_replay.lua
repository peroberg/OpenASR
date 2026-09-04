-- Deterministic contract test for the captured V3.50 TEMPO stream and the
-- verified ASR-10 display transaction classes. The real firmware/device path
-- is covered separately by display_field_rewrite.lua.

local fixture = dofile("docs/asr10/lua/fixtures/display_tempo_v350.lua")

local function new_state()
  local state = {
    cursor = 0, selected_column = nil, attr = 0, selected_attr = 0,
    pending_attr = false, defining_field = false,
    pending_annunciator = nil, pending_open = false,
    chars = {}, attrs = {}, annunciators = {},
  }
  for column = 0, 39 do state.chars[column], state.attrs[column] = " ", "0" end
  for register = 0, 4 do state.annunciators[register] = 0 end
  return state
end

local function consume(state, byte)
  if state.pending_annunciator then
    state.annunciators[state.pending_annunciator] = byte
    state.pending_annunciator = nil
  elseif byte >= 0x77 and byte <= 0x7b then
    state.pending_annunciator = byte - 0x77
  elseif state.pending_open then
    state.pending_open = false
  elseif state.pending_attr then
    state.attr = ((byte & 0x02) ~= 0) and "1" or "0"
    if state.defining_field then state.selected_attr = state.attr end
    state.pending_attr = false
  elseif byte >= 0x74 and byte <= 0x76 then
    state.pending_open = true
  elseif byte <= 0x1f then
    state.cursor = byte
  elseif byte >= 0x20 and byte <= 0x5f then
    state.chars[state.cursor], state.attrs[state.cursor] = string.char(byte), state.attr
    state.cursor = math.min(state.cursor + 1, 23)
  elseif byte == 0x60 then
    state.pending_attr = true
  elseif byte == 0x62 then
    state.selected_column, state.attr = state.cursor, "0"
    state.defining_field = true
  elseif byte == 0x63 then
    assert(state.selected_column, "$63 without a preceding selected-field anchor")
    state.cursor, state.attr = state.selected_column, state.selected_attr
  elseif byte == 0x66 then
    state.cursor, state.selected_column, state.attr = 0, nil, "0"
    state.defining_field = false
    for column = 0, 39 do state.chars[column], state.attrs[column] = " ", "0" end
  elseif byte == 0x72 then
    state.attr, state.defining_field = "0", false
  end
end

local function feed(state, bytes)
  for _, byte in ipairs(bytes) do consume(state, byte) end
end

local function visible(state, values)
  local result = {}
  for column = 0, 21 do result[#result + 1] = values[column] end
  return table.concat(result)
end

local tempo = new_state()
feed(tempo, fixture.enter_tempo)
assert(visible(tempo, tempo.chars) == fixture.expected.initial)
assert(visible(tempo, tempo.attrs) == fixture.expected.underline)
assert(tempo.selected_column == fixture.expected.selected_column)
feed(tempo, fixture.up)
assert(visible(tempo, tempo.chars) == fixture.expected.after_up)
assert(visible(tempo, tempo.attrs) == fixture.expected.underline)
assert(tempo.cursor == fixture.expected.cursor_after_rewrite)
feed(tempo, fixture.down)
assert(visible(tempo, tempo.chars) == fixture.expected.after_down)
assert(tempo.cursor == fixture.expected.cursor_after_rewrite)

local absolute = new_state()
feed(absolute, { 0x66 })
for byte in ("                    99"):gmatch(".") do consume(absolute, byte:byte()) end
feed(absolute, { 0x14, 0x39, 0x38 })
assert(visible(absolute, absolute.chars):sub(21, 22) == "98")

local before = visible(tempo, tempo.chars) .. visible(tempo, tempo.attrs)
feed(tempo, { 0x77, 0x02 })
assert(visible(tempo, tempo.chars) .. visible(tempo, tempo.attrs) == before)
feed(tempo, { 0x74, 0x40 })
assert(visible(tempo, tempo.chars) .. visible(tempo, tempo.attrs) == before)
feed(tempo, { 0x66 })
assert(tempo.annunciators[0] == 0x02)

print("PASS display_protocol_stream_replay tempo=90->91->90 absolute=true underline=true annunciator_retained=true open_pairs_ignored=true")
manager.machine:exit()
