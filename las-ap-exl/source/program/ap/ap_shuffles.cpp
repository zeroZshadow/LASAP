// The player's shuffles (DESIGN.md, Options), LASR's Music, Randomize Environments and Randomize Text with a RomFS
// that is the same for every seed: the seed file's shuffles say what each swaps (patcher ap_shuffles.py), and
// the module swaps the names as the game looks them up. Music: the game's own requests (music.cpp), which the
// Jukebox's chosen track replaces after. Lighting: LevelEnvironment_Find, by which zones and events pick their
// environment (scripts/re/FRAMEWORK.md). Dialogue: MessageManager_Find ("File:Label").
#include "ap/ap_shuffles.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_text.hpp"
#include "config/seed_file.hpp"
#include "lib.hpp"
#include "loggers.hpp"
#include "test_only.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct Swap {
        std::string_view from, to;  // in the seed file
        char fix = '=';            // messages: the input wait at the end added (+), taken off (-) or kept (=)
    };
    std::vector<Swap> g_Tracks, g_Environments, g_Messages;
    // The messages shown in another's place whose input wait at the end changes, by (message file, index) as the game
    // found them: the fix and the fixed text. The game still reads a fetched text after fetching others (ap_text.cpp,
    // HandOut), so each keeps its own, made once from the text it was fetched as; the map's nodes never move.
    struct Fixed {
        char fix;
        const char* from = nullptr;  // the game's text the fixed one was made from
        s64 fromLength = 0;
        std::string text;
    };
    std::map<std::pair<void*, s32>, Fixed> g_Fixed;
    TEST_ONLY(std::string g_LastEnvironment = "-";)  // the last environment swapped, "<asked>-><found>", for DBG shuffles
    TEST_ONLY(std::string g_LastMessage = "-";)  // the last message looked up, "<asked>[-><donor>]" with the donor found

    const Swap* Find(const std::vector<Swap>& swaps, std::string_view from) {
        for (const Swap& swap : swaps) {
            if (swap.from == from) return &swap;
        }
        return nullptr;
    }

    HOOK_DEFINE_TRAMPOLINE(EnvironmentFind) {
        static void* Callback(void* map, const char* name, u64 length) {
            const Swap* swap = name != nullptr ? Find(g_Environments, std::string_view(name, length)) : nullptr;
            if (swap == nullptr) return Orig(map, name, length);
            void* found = Orig(map, swap->to.data(), swap->to.size());
            TEST_ONLY(g_LastEnvironment = std::string(swap->from) + "->" + std::string(found != nullptr ? swap->to : swap->from);)
            return found != nullptr ? found : Orig(map, name, length);  // a level without that environment
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MessageFind) {
        static void Callback(Game::MessageRef* out, u8* manager, const Game::StrView* label) {
            const Swap* swap = label != nullptr ? Find(g_Messages, label->View()) : nullptr;
            TEST_ONLY(if (label != nullptr) g_LastMessage = std::string(label->View());)
            char fix = '=';
            if (swap != nullptr) {
                Game::StrView donor(swap->to);
                Orig(out, manager, &donor);
                if (out->msbt != nullptr && out->index >= 0) {
                    TEST_ONLY(g_LastMessage += "->" + std::string(swap->to);)
                    fix = swap->fix;
                } else {
                    Orig(out, manager, label);
                }
            } else {
                Orig(out, manager, label);
            }
            // Message files are reloaded on language change: another message in a place keeps no earlier fix.
            std::pair<void*, s32> at{out->msbt, out->index};
            if (fix == '=') {
                if (!g_Fixed.empty()) g_Fixed.erase(at);
                return;
            }
            auto [it, made] = g_Fixed.try_emplace(at, Fixed{fix});
            if (!made && it->second.fix != fix) it->second = Fixed{fix};
        }
    };

    // The seed file's shuffles: "M <track> <track or ->" the music a track plays as (- none), "E <environment>
    // <environment>" a zone's lighting, "T <File:Label> <File:Label> <fix>" the message a message shows, its end's
    // input wait added (+), taken off (-) or kept (=) (patcher ap_shuffles.py)
    void Load() {
        SeedFile::ForEach("shuffles", [](Fields& f) {
            std::string_view kind = f.Str(), from = f.Str(), to = f.Str(), fix = f.Str();
            if (to.empty()) return;
            Swap swap{from, to == "-" ? std::string_view() : to};
            if (kind == "M") g_Tracks.push_back(swap);
            else if (kind == "E") g_Environments.push_back(swap);
            else if (kind == "T" && !fix.empty()) g_Messages.push_back({from, to, fix[0]});
        });
        Logging.Log("[AP] shuffles: %zu tracks, %zu environments, %zu messages", g_Tracks.size(),
                    g_Environments.size(), g_Messages.size());
    }
}

namespace AP::Shuffles {
    void Install() {
        Load();
        if (!g_Environments.empty()) EnvironmentFind::InstallAtFuncPtr(Game::LevelEnvironment_Find.Ptr());
        if (!g_Messages.empty()) MessageFind::InstallAtFuncPtr(Game::MessageManager_Find.Ptr());
    }

    std::optional<std::string_view> Track(std::string_view track) {
        const Swap* swap = Find(g_Tracks, track);
        return swap != nullptr ? std::optional(swap->to) : std::nullopt;
    }

#ifdef LASAP_TEST
    std::string State() {
        return "SHUFFLES tracks=" + std::to_string(g_Tracks.size()) + " environments=" +
               std::to_string(g_Environments.size()) + " messages=" + std::to_string(g_Messages.size()) +
               " environment=" + g_LastEnvironment + " message=" + g_LastMessage;
    }
#endif

    void FixText(Game::MsbtText* out, void* msbt, s32 index) {
        if (out->text == nullptr || g_Fixed.empty()) return;
        auto it = g_Fixed.find({msbt, index});
        if (it == g_Fixed.end()) return;
        Fixed& fixed = it->second;
        if (fixed.from != out->text || fixed.fromLength != out->length) {
            std::string_view text(out->text, static_cast<size_t>(out->length));
            bool waits = text.ends_with(AP::WAIT_INPUT);
            fixed.from = out->text;
            fixed.fromLength = out->length;
            if ((fixed.fix == '+') == waits) {
                fixed.text.clear();  // it has its fix already
            } else if (fixed.fix == '+') {
                fixed.text = std::string(text).append(AP::WAIT_INPUT);
            } else {
                fixed.text = std::string(text.substr(0, text.size() - AP::WAIT_INPUT.size()));
            }
        }
        if (fixed.text.empty()) return;
        out->text = fixed.text.c_str();
        out->length = static_cast<s64>(fixed.text.size());
    }
}
