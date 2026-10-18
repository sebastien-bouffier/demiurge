-- Shared by the Demiurge ReaScripts: where the plugin and REAPER exchange notes.
-- Same folder as the plugin's: <app data>/Bidoo/Demiurge/Exchange
local M = {}

function M.exchangeDir()
  local os_name = reaper.GetOS()
  if os_name:match("Win") then
    return os.getenv("APPDATA") .. "\\Bidoo\\Demiurge\\Exchange\\"
  elseif os_name:match("OSX") or os_name:match("macOS") then
    return os.getenv("HOME") .. "/Library/Application Support/Bidoo/Demiurge/Exchange/"
  else
    return os.getenv("HOME") .. "/.config/Bidoo/Demiurge/Exchange/"
  end
end

function M.selectedMidiTake()
  local item = reaper.GetSelectedMediaItem(0, 0)
  if not item then return nil, nil end
  local take = reaper.GetActiveTake(item)
  if not take or not reaper.TakeIsMIDI(take) then return item, nil end
  return item, take
end

return M
