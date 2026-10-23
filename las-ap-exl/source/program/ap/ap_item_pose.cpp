// The pose the item-get holds an item up in (DESIGN.md, Showing received items): the game picks it by item id
// (ItemPose_ItemGet), and ids it has no pose for get a default one (scale 1, tipped 20 degrees). The placeholder's
// variants and LASR's Dampe rewards have such ids but show another item's model; they take that item's pose
// (POSES_PATH), so a heart piece found at an AP location shows at the heart piece's size (1.7).
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/sym.hpp"
#include <array>
#include <sstream>

namespace AP::Bridge {
namespace {
    // Item id -> the id whose pose it takes; its own id unless POSES_PATH names another
    std::array<u8, 256> g_PoseIds{};

    HOOK_DEFINE_TRAMPOLINE(ItemPoseItemGet) {
        static const Game::ItemPose* Callback(u8 itemId, bool sideView) {
            return Orig(g_PoseIds[itemId], sideView);
        }
    };

    // "<item id> <pose item id>" lines (patcher las_ap_patch.write_poses)
    void LoadItemPoses() {
        for (size_t i = 0; i < g_PoseIds.size(); i++) g_PoseIds[i] = static_cast<u8>(i);
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::POSES_PATH, text))) return;
        std::stringstream lines(text);
        std::string line;
        int count = 0;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            int itemId, poseId;
            if (!(ss >> itemId >> poseId) || itemId < 0 || itemId > 255 || poseId < 0 || poseId > 255) continue;
            g_PoseIds[itemId] = static_cast<u8>(poseId);
            count++;
        }
        Logging.Log("[AP] item poses: %d", count);
    }
}

    void InstallItemPoseHooks() {
        LoadItemPoses();
        ItemPoseItemGet::InstallAtFuncPtr(Game::ItemPose_ItemGet.Ptr());
    }
}
