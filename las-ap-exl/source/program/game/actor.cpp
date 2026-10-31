#include "game/actor.hpp"
#include "game/sym.hpp"

namespace Game {
    ScopedActorParams::~ScopedActorParams() {
        ReleaseWeakOwner(params.unread.ctrl);
        ReleaseOwner(params.lebRoom.ctrl);
        ReleaseOwner(params.flowchart.ctrl);
        ActorParams_Dtor0(params.drsb);
        ActorParams_Dtor1(params.bfres);
        ActorParams_Dtor2(params.lebParams);
    }
}
