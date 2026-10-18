-- @description Demiurge: replace the selected MIDI item's notes with Demiurge's
-- @about Press TO REAPER in Demiurge first. The notes inside the exported range (a cell or a
--        whole part, at their song positions) are deleted from the selected MIDI item and
--        replaced. The item grows if it is too short. With no item selected, a new MIDI item
--        is created on the selected track. One undo step.

local dir = ({reaper.get_action_context()})[2]:match("^(.*[/\\])")
local common = dofile(dir .. "Demiurge - common.lua")

local f = io.open(common.exchangeDir() .. "to_reaper.txt", "r")
if not f then
  reaper.MB("Nothing to import yet. Press TO REAPER in Demiurge first.", "Demiurge", 0)
  return
end

local rangeStart, rangeEnd = nil, nil
local notes = {}
for line in f:lines() do
  local a, b = line:match("^range%s+(%S+)%s+(%S+)")
  if a then rangeStart, rangeEnd = tonumber(a), tonumber(b) end
  local qn, len, pitch, vel, chan = line:match("^note%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)")
  if qn then
    notes[#notes + 1] = { qn = tonumber(qn), len = tonumber(len), pitch = tonumber(pitch),
                          vel = tonumber(vel), chan = tonumber(chan) }
  end
end
f:close()
if not rangeStart then
  reaper.MB("The Demiurge export is empty or damaged. Press TO REAPER again.", "Demiurge", 0)
  return
end

reaper.Undo_BeginBlock()
local item, take = common.selectedMidiTake()
if item and not take then
  reaper.Undo_EndBlock("Demiurge: replace MIDI", -1)
  reaper.MB("The selected item is not a MIDI item.", "Demiurge", 0)
  return
end

if not item then
  local track = reaper.GetSelectedTrack(0, 0)
  if not track then
    reaper.Undo_EndBlock("Demiurge: replace MIDI", -1)
    reaper.MB("Select a MIDI item to replace, or a track for a new item.", "Demiurge", 0)
    return
  end
  item = reaper.CreateNewMIDIItemInProj(track, reaper.TimeMap2_QNToTime(0, rangeStart),
                                        reaper.TimeMap2_QNToTime(0, rangeEnd), false)
  take = reaper.GetActiveTake(item)
else
  -- Grow the item to cover the exported range.
  local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
  local len = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
  local itemStart = reaper.TimeMap2_timeToQN(0, pos)
  local itemEnd = reaper.TimeMap2_timeToQN(0, pos + len)
  if rangeStart < itemStart - 1e-6 or rangeEnd > itemEnd + 1e-6 then
    reaper.MIDI_SetItemExtents(item, math.min(itemStart, rangeStart), math.max(itemEnd, rangeEnd))
  end
end

-- Delete the notes that start inside the range...
local _, noteCount = reaper.MIDI_CountEvts(take)
for i = noteCount - 1, 0, -1 do
  local ok, _, _, startPpq = reaper.MIDI_GetNote(take, i)
  if ok then
    local qn = reaper.MIDI_GetProjQNFromPPQPos(take, startPpq)
    if qn >= rangeStart - 1e-6 and qn < rangeEnd - 1e-6 then
      reaper.MIDI_DeleteNote(take, i)
    end
  end
end

-- ...and write Demiurge's.
for _, n in ipairs(notes) do
  local s = reaper.MIDI_GetPPQPosFromProjQN(take, n.qn)
  local e = reaper.MIDI_GetPPQPosFromProjQN(take, n.qn + n.len)
  reaper.MIDI_InsertNote(take, false, false, s, e, math.max(0, n.chan - 1), n.pitch, n.vel, true)
end
reaper.MIDI_Sort(take)
reaper.UpdateArrange()
reaper.Undo_EndBlock("Demiurge: replace MIDI", -1)
