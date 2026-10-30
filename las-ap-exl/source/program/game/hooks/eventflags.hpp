#pragma once
// The game's event flags by name (eventflags.cpp). The map tracker answers its own flags (AP::MapTracker::Flag), the
// bridge the egg's seashell flag (AP::GoalFlag), and ShadowClear set is the goal (AP::OnGoal).
#include <string>
#include <string_view>

namespace EventFlags {
    // An item's getting flag (its Items.gsheet row's)
    bool CheckGettingFlag(int itemId);
    void SetGettingFlag(int itemId);
    bool CheckFlag(std::string_view name);
    // Sets the flag when it differs.
    void SetFlag(const std::string& name, bool value);
}
