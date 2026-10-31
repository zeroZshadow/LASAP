// The pose the item-get holds an item up in (DESIGN.md, Item model: remote items; scripts/re/ITEMS.md, Item-get
// poses): the placeholder's variants and LASR's Dampe rewards, which get the default pose, take the pose of the item
// whose model they show (POSES_PATH).
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
