-- Deterministic replay oracle for the captured V3.50 TEMPO stream.
--
-- `current` mirrors the relevant behavior of the current
-- esq1x22_device::write_char(): $62/$72 only reset the active attribute and
-- $63 is ignored. `field_aware` adds only the measured $62/$63 contract. This
-- does not call the C++ device and is therefore not yet a display regression;
-- the fixture is intended to become one when the ASR decoder is implemented.

local fixture = dofile("docs/asr10/lua/fixtures/display_tempo_v350.lua")

local function new_state(field_aware)
  local state = {
    field_aware = field_aware,
    cursor = 0,
    selected_column = nil,
    attr = 0,
    pending_attr = false,
    preserve_attr = false,
    chars = {},
    attrs = {},
  }
  for column = 0, 39 do
    state.chars[column] = " "
    state.attrs[column] = 0
  end
  return state
end

local function consume(state, byte)
  if state.pending_attr then
    state.attr = ((byte & 0x02) ~= 0) and 1 or 0
    state.pending_attr = false
  elseif byte <= 0x1f then
    state.cursor = byte
  elseif byte >= 0x20 and byte <= 0x5f then
    state.chars[state.cursor] = string.char(byte)
    if not state.preserve_attr then state.attrs[state.cursor] = state.attr end
    state.cursor = math.min(state.cursor + 1, 23)
  elseif byte == 0x60 then
    state.pending_attr = true
  elseif byte == 0x62 then
    state.attr = 0
    if state.field_aware then state.selected_column = state.cursor end
  elseif byte == 0x63 then
    if state.field_aware then
      assert(state.selected_column, "$63 without a preceding selected-field anchor")
      state.cursor = state.selected_column
      state.preserve_attr = true
    end
  elseif byte == 0x66 then
    state.cursor, state.selected_column, state.attr = 0, nil, 0
    state.pending_attr, state.preserve_attr = false, false
    for column = 0, 39 do
      state.chars[column] = " "
      state.attrs[column] = 0
    end
  elseif byte == 0x72 then
    state.attr = 0
    state.preserve_attr = false
  end
end

local function feed(state, bytes)
  for _, byte in ipairs(bytes) do consume(state, byte) end
end

local function text(state)
  local result = {}
  for column = 0, 21 do result[#result + 1] = state.chars[column] end
  return table.concat(result)
end

local function underline(state)
  local result = {}
  for column = 0, 21 do result[#result + 1] = state.attrs[column] ~= 0 and "1" or "0" end
  return table.concat(result)
end

local current = new_state(false)
local field_aware = new_state(true)
feed(current, fixture.enter_tempo)
feed(field_aware, fixture.enter_tempo)
assert(text(current) == fixture.expected.initial)
assert(text(field_aware) == fixture.expected.initial)
assert(underline(current) == fixture.expected.underline)
assert(underline(field_aware) == fixture.expected.underline)

feed(current, fixture.up)
feed(field_aware, fixture.up)
assert(text(current) == fixture.expected.current_after_up)
assert(text(field_aware) == fixture.expected.protocol_after_up)
assert(field_aware.selected_column == fixture.expected.selected_column)
assert(underline(field_aware) == fixture.expected.underline)

feed(current, fixture.down)
feed(field_aware, fixture.down)
assert(text(current) == fixture.expected.current_after_down)
assert(text(field_aware) == fixture.expected.protocol_after_down)
assert(underline(field_aware) == fixture.expected.underline)

print("PASS display_protocol_stream_replay current_bug_reproduced=true field_contract=true")
manager.machine:exit()
