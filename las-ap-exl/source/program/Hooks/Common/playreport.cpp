#include "Hooks/hooks.hpp"
#include "Game/addr.hpp"

// Play reports (telemetry) are not sent: their add and save functions do nothing.
namespace {
    constexpr uintptr_t kPlayReportAdd = 0x1432450, kPlayReportSave = 0x1432460, kPlayReportAddDouble = 0x1432470;

    HOOK_DEFINE_REPLACE(PlayReport_Add) {
        static void Callback(long, long) {}
    };

    HOOK_DEFINE_REPLACE(PlayReport_Save) {
        static void Callback(long) {}
    };

    HOOK_DEFINE_REPLACE(PlayReport_AddDouble) {
        static void Callback(long, double) {}
    };
}

namespace PlayReport {
    void Install() {
        PlayReport_Add::InstallAtOffset(Game::Offset(kPlayReportAdd));
        PlayReport_Save::InstallAtOffset(Game::Offset(kPlayReportSave));
        PlayReport_AddDouble::InstallAtOffset(Game::Offset(kPlayReportAddDouble));
    }
}
