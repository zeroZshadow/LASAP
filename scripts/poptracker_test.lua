-- Test hook of scripts/poptracker_test.py, loaded by the sandbox PopTracker from its user-override folder after the
-- pack's own scripts; it is not part of the pack. It prints the tracker's state as LASAPTEST lines (stdout, the log):
--   LASAPTEST <tag> I <code> <increment> <active> <stage> <count> <badge>   an item, found by one of its codes
--   LASAPTEST <tag> L <id> <accessibility>                             a location
--   LASAPTEST <tag> S <code> <available> <chests> <accessibility> <highlight>   a location section, its highlight by
--                                                                        name (Highlight's: None, Priority, ...)
--   LASAPTEST <tag> G <name> <value>                                   a global
--   LASAPTEST <tag> END
-- fields separated by tabs. Commands come from scripts/lasap_cmd.lua in the override folder, read every 0.1 s:
-- LASAP_CMD = {seq = <n>, "<command>", ...}, each run once, then "LASAPTEST DONE <n>". Commands:
--   dump <tag> | set <code> Active|CurrentStage|AcquiredCount <value> | mark <section code> <available> |
--   call <tag> <global function> [<argument> ...]   (prints "LASAPTEST <tag> CALL <result>"; the sandbox has no
--   load; the arguments are strings, as a pack rule's $function|argument passes them)
-- With Archipelago, a data storage write of lasap_test_barrier dumps with its value as the tag: the server sends it
-- after everything the test did before, so the dump shows the tracker after those events.
ScriptHost:LoadScript("scripts/lasap_test_codes.lua")

local function field(obj, prop)
    local ok, value = pcall(function() return obj[prop] end)
    if ok then
        return tostring(value)
    end
    return "-"
end

local HIGHLIGHT_NAMES = {}
if Highlight then
    for name, value in pairs(Highlight) do
        HIGHLIGHT_NAMES[value] = name
    end
end

local function out(tag, kind, ...)
    print("LASAPTEST " .. table.concat({tag, kind, ...}, "\t"))
end

local function dump(tag)
    for _, code in ipairs(LASAP_TEST_ITEMS) do
        local obj = Tracker:FindObjectForCode(code)
        if obj then
            out(tag, "I", code, field(obj, "Increment"), field(obj, "Active"), field(obj, "CurrentStage"),
                field(obj, "AcquiredCount"), field(obj, "BadgeText"))
        else
            out(tag, "I", code, "missing")
        end
    end
    for _, id in ipairs(LASAP_TEST_LOCATIONS) do
        local obj = Tracker:FindObjectForCode("@" .. id)
        out(tag, "L", id, obj and field(obj, "AccessibilityLevel") or "missing")
    end
    for _, code in ipairs(LASAP_TEST_SECTIONS) do
        local obj = Tracker:FindObjectForCode(code)
        if obj then
            local ok, highlight = pcall(function() return obj.Highlight end)
            out(tag, "S", code, field(obj, "AvailableChestCount"), field(obj, "ChestCount"),
                field(obj, "AccessibilityLevel"), ok and HIGHLIGHT_NAMES[highlight] or "-")
        else
            out(tag, "S", code, "missing")
        end
    end
    out(tag, "G", "INSTRUMENTS_HIDDEN", tostring(INSTRUMENTS_HIDDEN))
    out(tag, "END")
end

local function run(command)
    local verb, rest = command:match("^(%S+)%s*(.*)$")
    if verb == "dump" then
        dump(rest)
    elseif verb == "set" then
        local code, prop, value = rest:match("^(.-)%s+(%S+)%s+(%S+)$")
        local obj = Tracker:FindObjectForCode(code)
        if prop == "Active" then
            obj.Active = value == "true"
        else
            obj[prop] = tonumber(value)
        end
    elseif verb == "mark" then
        local code, value = rest:match("^(.-)%s+(%S+)$")
        Tracker:FindObjectForCode(code).AvailableChestCount = tonumber(value)
    elseif verb == "call" then
        local tag, name, list = rest:match("^(%S+)%s+(%S+)%s*(.*)$")
        local args = {}
        for arg in list:gmatch("%S+") do
            args[#args + 1] = arg
        end
        out(tag, "CALL", tostring(_G[name](table.unpack(args))))
    end
end

local done = 0
local since = 0
ScriptHost:AddOnFrameHandler("lasap test commands", function(elapsed)
    since = since + elapsed
    if since < 0.1 then
        return
    end
    since = 0
    LASAP_CMD = nil
    ScriptHost:LoadScript("scripts/lasap_cmd.lua")
    if LASAP_CMD and LASAP_CMD.seq > done then
        done = LASAP_CMD.seq
        for _, command in ipairs(LASAP_CMD) do
            local ok, err = pcall(run, command)
            if not ok then
                print("LASAPTEST ERROR " .. command .. ": " .. tostring(err))
            end
        end
        print("LASAPTEST DONE " .. done)
    end
end)

if Archipelago then
    Archipelago:AddClearHandler("lasap test", function(slot_data)
        Archipelago:SetNotify({"lasap_test_barrier"})
        Archipelago:Get({"lasap_test_barrier"})
    end)
    Archipelago:AddRetrievedHandler("lasap test", function(key, value)
        if key == "lasap_test_barrier" then
            print("LASAPTEST READY " .. Archipelago.PlayerNumber)
        end
    end)
    Archipelago:AddItemHandler("lasap test", function(index, item_id, item_name)
        print("LASAPTEST ITEM " .. index .. " " .. item_id)
    end)
    Archipelago:AddSetReplyHandler("lasap test", function(key, value)
        if key == "lasap_test_barrier" then
            dump(tostring(value))
        end
    end)
end
print("LASAPTEST LOADED")
