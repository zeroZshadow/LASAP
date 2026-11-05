#pragma once
// The options the RomFS holds both ways (DESIGN.md, Options): config.ini's [options] picks one at runtime.
#include <optional>
#include <string_view>

namespace AP::Options {
    // An option flag's value (patcher ap_options.py), asked by events, sheet conditions and NPC layouts through
    // EventFlags_Check: "APOpt.<option>" whether the option is on; "APIf|<option>|<type>|<argument>|<type>|<argument>"
    // the first condition (Conditions_Check's type and argument; type 0 holds unless its argument is "false") while
    // the option is on, else the second. Empty for any other flag.
    std::optional<bool> Flag(std::string_view name);
    // The room hook that leaves out, retypes or moves LEB actors by the options.
    void InstallHooks();
    // A sheet the game makes (Datasheet_Create): the rows the options change. It runs at each make, on rows that may
    // hold an earlier make's edit (scripts/re/UI.md, Datasheets), so an edit must give the same rows when run again.
    void EditSheet(std::string_view name, void* sheet);
}
