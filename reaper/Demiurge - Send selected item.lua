-- @description Demiurge: send the selected MIDI item to the plugin
-- @about Writes the notes of the selected MIDI item for Demiurge. Then press FROM REAPER in
--        Demiurge: the notes go into the selected part, split over the song's sections, as new takes.

local dir = ({reaper.get_action_context()})[2]:match("^(.*[/\\])")
local common = dofile(dir .. "Demiurge - common.lua")

local item, take = common.selectedMidiTake()
if not take then
  reaper.MB("Select a MIDI item first.", "Demiurge", 0)
  return
end

local folder = common.exchangeDir()
reaper.RecursiveCreateDirectory(folder, 0)
local f = io.open(folder .. "from_reaper.txt", "w")
if not f then
  reaper.MB("Could not write to " .. folder, "Demiurge", 0)
  return
end

local _, noteCount = reaper.MIDI_CountEvts(take)
f:write("demiurge 1\n")
f:write("name " .. reaper.GetTakeName(take) .. "\n")
local sent = 0
for i = 0, noteCount - 1 do
  local ok, _, muted, startPpq, endPpq, chan, pitch, vel = reaper.MIDI_GetNote(take, i)
  if ok and not muted then
    local qs = reaper.MIDI_GetProjQNFromPPQPos(take, startPpq)
    local qe = reaper.MIDI_GetProjQNFromPPQPos(take, endPpq)
    f:write(string.format("note %.6f %.6f %d %d %d\n", qs, qe - qs, pitch, vel, chan + 1))
    sent = sent + 1
  end
end
f:close()
reaper.ShowConsoleMsg("Demiurge: sent " .. sent .. " notes. Press FROM REAPER in the plugin.\n")
