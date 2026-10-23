#include "lib.hpp"
#include "patches.hpp"
#include "nn/err.hpp"
#include "Config/config.hpp"
#include "Config/file_handler.hpp"
#include "Hooks/hooks.hpp"
#include "AP/ap.hpp"
#include <string>

namespace {
    // The module's patches and hooks, in the order they install; AP::Init adds the Archipelago bridge's.
    void Install() {
        const PatchConfig& config = global_config;
        Patches::Run();
        EventFlags::Install();
        Input::Install();
        Inventory::Install();
        LevelSystem::Install();
        PlayReport::Install();
        if (config.randomizer.enabled) {
            Fishing::Install();
            Seashells::Install();
            EventPatcher::Install();
        }
        if (config.randomizer.enemies || config.randomizer.enemy_sizes) Enemies::Install();
        Controls::Install();
        Fixes::Install();
        Tweaks::Install();
        AP::Init(config.archipelago.seed_hash, config.archipelago.port);
    }
}

HOOK_DEFINE_TRAMPOLINE(nnMain) {
    static void Callback() {
        R_ABORT_UNLESS(FileHandler::MountSD());
        std::string config_str;
        if (R_FAILED(FileHandler::ReadFile("sd:/config/lasr-exl/config.ini", config_str))) {
            nn::err::ApplicationErrorArg err(
                nn::err::MakeErrorCode(nn::err::ErrorCodeCategoryType::unk1, 0x420), "Failed to read config!",
                "Please ensure 'sd:/config/lasr-exl/config.ini' exists",
                nn::settings::LanguageCode::Make(nn::settings::Language::Language_English)
            );
            nn::err::ShowApplicationError(err);
            EXL_ABORT("Failed to read config.");
        }
        global_config.parse(config_str);
        Install();
        Orig();
    }
};

extern "C" void exl_main(void* x0, void* x1) {
    /* Setup hooking environment. */
    exl::hook::Initialize();
    nnMain::InstallAtOffset(0x1bbf0);
}

extern "C" NORETURN void exl_exception_entry() {
    /* Note: this is only applicable in the context of applets/sysmodules. */
    EXL_ABORT("Default exception handler called!");
}
