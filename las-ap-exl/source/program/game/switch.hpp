#pragma once
// An actor's LEB switches (ACTORS.md, Switches): its record's four slots, each a switch type and index. Each actor
// class gives its slots meanings of its own, which it names in a scoped enum of slots; these take that enum.
#include "game/actor.hpp"
#include "game/sym.hpp"
#include <type_traits>
#include <utility>

namespace Game {
    template<typename E>
    concept SwitchSlot = std::is_scoped_enum_v<E>;

    // The actor's switch in the slot is on; false without a record
    template<SwitchSlot E>
    bool SwitchOn(const Actor& actor, E slot) { return ActorSwitch_IsOn(actor.record, std::to_underlying(slot)); }

    template<SwitchSlot E>
    void SetSwitch(const Actor& actor, E slot, bool on) { ActorSwitch_Set(actor.record, std::to_underlying(slot), on); }
}
