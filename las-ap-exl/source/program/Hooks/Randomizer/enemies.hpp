#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Config/config.hpp"
#include "Game/Data/inventory.hpp"
#include "Hooks/Common/level.hpp"
#include <string>
#include <iterator>
#include <vector>
#include <deque>
#include <algorithm>
#include <random>

// we want to group actors by types
// actors not included are too problematic, whether they just block paths or straight up crash the game without additonal work
u16 land_ids[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x8, 0x9, 0xa, 0x12, 0x13, 0x14, 0x15, 0x16,
                    0x17, 0x1b, 0x1c, 0x1d, 0x22, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2e, 0x2f,
                    0x30, 0x31, 0x32, 0x33, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
                    0x3f, 0x40, 0x41, 0x42, 0x44, 0x4a, 0x4d, 0xb7};
u16 air_ids[] = {0x7, 0x10, 0x26, 0x34, 0x3e, 0x48};
u16 tree_ids[] = {0xc, 0x23};
u16 water_shallow_ids[] = {0xd, 0x43};
u16 water_ids[] = {0x1a};
u16 water_2d_ids[] = {0x45, 0x47};

u64 actor_exclusions[] = {
    7638153348067089846ULL, 7638164343183367606ULL, 11602973586154444641ULL, 11601313323595920104ULL, 11601306726526153448ULL,
    11601308925549409000ULL, 2234171345317354235ULL, 2234164748247587579ULL, 2234163648735959803ULL, 2234177942387120891ULL,
    2234652931410320809ULL, 2234654030921948585ULL, 2234539681712659881ULL, 2234540781224287657ULL, 2234541880735915433ULL,
    2234542980247543209ULL, 2234602353875443113ULL, 2234603453387070889ULL, 2234604552898698665ULL, 2234605652410326441ULL,
    8502845576057960233ULL, 2234482507108015529ULL, 2234262604782460328ULL, 2234263704294088104ULL, 2234397844712676777ULL,
    2234398944224304553ULL, 2234400043735932329ULL, 2234298888666181890ULL, 2234299988177809666ULL, 2234243913084793086ULL,
    2234314281828970756ULL, 2234315381340598532ULL, 2234353864247570698ULL, 2234340670108037383ULL, 2234331874015015175ULL,
    2233803008922054894ULL, 2233795312340660462ULL, 2234076787317371124ULL, 2234077886828998900ULL, 2234078986340626676ULL,
    2234046000991793394ULL, 2234054797084815602ULL, 2234038304410398962ULL, 2234357162782096126ULL, 2234364859363490558ULL,
    2234363759851862782ULL, 2234368157898373886ULL, 2234323077921635067ULL, 2234330774503029499ULL, 2234317580363496187ULL,
    2234324177433262843ULL, 2234256007712340729ULL, 2234258206735596281ULL, 2234257107223968505ULL, 2234133961921657590ULL,
    2234141658503052022ULL, 2234124066317007606ULL, 2234132862410029814ULL, 2234122966805379830ULL, 2235320334968578115ULL,
    2235321434480205891ULL, 2235322533991833667ULL, 2235323633503461443ULL, 2235324733015089219ULL, 2235297245224394819ULL,
    2235298344736022595ULL, 2235299444247650371ULL, 2235300543759278147ULL, 2235192791619756099ULL, 2235193891131383875ULL,
    2235194990643011651ULL, 2235135617015111747ULL, 2235136716526739523ULL, 2235137816038367299ULL, 2235171900898828355ULL,
    2235173000410456131ULL, 2235174099922083907ULL, 2235175199433711683ULL, 2235176298945339459ULL, 2234088881944918772ULL,
    2234097678037940980ULL, 2234181240921651959ULL, 2234162549223979767ULL, 2234151554107702007ULL, 2234157051665840887ULL,
    2234288993061174010ULL, 2234293391107685114ULL, 2234270301363501818ULL, 2234301087689079546ULL, 2234292291596057338ULL,
    2234540781223934722ULL, 2234545179270445826ULL, 2234552875851840258ULL, 2234497900270451457ULL, 2234477009549523713ULL,
    2234482507107662593ULL, 2234486905154173697ULL, 2234500099293707009ULL, 2234387949107003346ULL, 2234401143246536658ULL,
    2234395645688397778ULL, 2234524288548847576ULL, 2234518790990708696ULL, 2234507795874430936ULL, 2234526487572103128ULL,
    2234419834944208851ULL, 2234402242758164435ULL, 2234403342269792211ULL, 2234048200014020546ULL, 2234050399037276098ULL,
    2233801909409398713ULL, 2233803008921026489ULL, 2234486905154781061ULL, 2234477009550131077ULL, 2234475910038503301ULL,
    2234480308085014405ULL, 2234488004666408837ULL, 11024819585944080864ULL, 11024830581060358624ULL, 11024821784967336471ULL,
    11024820685455708695ULL, 2234422033968742271ULL, 2234409939340836735ULL, 2234326376457125754ULL, 2234320878898986874ULL,
    2234367058387353467ULL, 2234385750085025659ULL, 2234402242759442299ULL, 2234024010759487337ULL, 2234020712224604009ULL,
    2234017413689720681ULL, 11024826183013847844ULL, 11024834979106870052ULL, 11024842675688264484ULL, 2233875576689737571ULL,
    2233881074247876451ULL, 2233872278154854243ULL, 2233821700619976545ULL, 2233823899643232097ULL, 2233820601108348769ULL,
    2233816203061837665ULL, 2235078442409570451ULL, 2235079541921198227ULL, 2235080641432826003ULL, 2235113626781659283ULL,
    2235114726293287059ULL, 2235115825804914835ULL, 5332491758296178028ULL, 5332474166110133612ULL, 5332487360249666924ULL,
    5332478564156644716ULL, 2235013571223531667ULL, 2234981685386326163ULL, 2234982784897953939ULL, 2234983884409581715ULL,
    2234984983921209491ULL, 2234811261084020882ULL, 2234812360595648658ULL, 2234813460107276434ULL, 2234840947897970834ULL,
    2234842047409598610ULL, 2234843146921226386ULL, 2234844246432854162ULL, 5332420290040372557ULL, 5332404896877583693ULL,
    5332419190528744781ULL, 2234552875851493522ULL, 2234553975363121298ULL, 2235306041317409435ULL, 2235311538875548315ULL,
    11024827282524874083ULL, 11024842675687662947ULL, 11024820685455107427ULL, 2233790914294423349ULL, 2233794212829306677ULL,
    2233796411852562229ULL, 2236436339269378658ULL, 2236437438781006434ULL, 2236251621315912290ULL, 2236219735478706786ULL,
    2236220834990334562ULL, 2236221934501962338ULL, 2236456130478678626ULL, 2236457229990306402ULL, 2236458329501934178ULL,
    2236459429013561954ULL, 2235920668315951713ULL, 2235644690897379937ULL, 2235645790409007713ULL, 2235646889920635489ULL,
    2235683173804352097ULL, 2235771134734574177ULL, 2235772234246201953ULL, 2235773333757829729ULL, 2235774433269457505ULL,
    2235412693943919200ULL, 2235413793455546976ULL, 2235414892967174752ULL, 2235490759269491297ULL, 2235491858781119073ULL,
    10160219716954485186ULL, 10160212020373090754ULL, 10160218617442857410ULL, 2234265903317608292ULL, 2234259306247841636ULL,
    10160249403768435145ULL, 10160234010605646281ULL, 10160229612559135177ULL, 10160241707187040713ULL, 2233923955201369947ULL,
    2233932751294392155ULL, 2233919557154858843ULL, 2233950343480436571ULL, 2233949243968808795ULL, 2233954741526947675ULL,
    2233883273271147672ULL, 2233879974736264344ULL, 2233899765945564312ULL, 2233888770829286552ULL
};

bool isRequiredKill(u64 hash) {
    if (std::find(std::begin(actor_exclusions), std::end(actor_exclusions), hash) != std::end(actor_exclusions)) {
        return true;
    }
    return false;
}

std::vector<u16> getValidEnemies(u16 id) {
    std::vector<u16> vec = {};

    if (std::find(std::begin(land_ids), std::end(land_ids), id) != std::end(land_ids)) {
        vec.insert(vec.end(), std::begin(land_ids), std::end(land_ids));
        // vec.insert(vec.end(), std::begin(air_ids), std::end(air_ids));
    }
    else if (id == 0x4b || id == 0x4c || id == 0x4e || id == 0x4f) { // EnemyStretchyGhost and EnemyKarakoro color variants
        vec.insert(vec.end(), std::begin(land_ids), std::end(land_ids));
        // vec.insert(vec.end(), std::begin(air_ids), std::end(air_ids));
    }
    else if (std::find(std::begin(air_ids), std::end(air_ids), id) != std::end(air_ids)) {
        vec.insert(vec.end(), std::begin(air_ids), std::end(air_ids));
    }

    else if (std::find(std::begin(tree_ids), std::end(tree_ids), id) != std::end(tree_ids)) {
        vec.insert(vec.end(), std::begin(tree_ids), std::end(tree_ids));
    }

    else if (std::find(std::begin(water_shallow_ids), std::end(water_shallow_ids), id) != std::end(water_shallow_ids)) {
        vec.insert(vec.end(), std::begin(water_shallow_ids), std::end(water_shallow_ids));
    }

    else if (std::find(std::begin(water_ids), std::end(water_ids), id) != std::end(water_ids)) {
        vec.insert(vec.end(), std::begin(water_ids), std::end(water_ids));
        // vec.insert(vec.end(), std::begin(water_shallow_ids), std::end(water_shallow_ids));
    }

    else if (std::find(std::begin(water_2d_ids), std::end(water_2d_ids), id) != std::end(water_2d_ids)) {
        vec.insert(vec.end(), std::begin(water_2d_ids), std::end(water_2d_ids));
    }

    return vec;
}

u16 blocking_ids[] = {0x3, 0x15, 0x16, 0x30, 0x41};
u16 annoying_ids[] = {0x26, 0x3e, 0x48, 0x8, 0x9, 0x13, 0x14, 0x2e, 0x2f, 0x4d};
std::deque<int> lastTen;

bool isEnemyValid(u16 vanilla_id, u16 new_id) {
    // if the enemy is vanilla then it is obviously valid :P
    if (new_id == vanilla_id) {
        return true;
    }

    // specific case where we want to block vires from spawning in the overworld
    if (new_id == 0x26 && currentLevel == "Field") {
        return false;
    }

    // make sure a non-blocking enemy is not changed into a blocking enemy
    if (std::find(std::begin(blocking_ids), std::end(blocking_ids), new_id) != std::end(blocking_ids)) {
        return false;
    }

    // prevent a powerful or annoying enemies from spawning too frequently
    if (std::find(std::begin(annoying_ids), std::end(annoying_ids), new_id) != std::end(annoying_ids)) {
        if (std::find(lastTen.begin(), lastTen.end(), (int)new_id) != lastTen.end()) {
            return false;
        }
    }

    return true;
}

// The variants are just different colors but the same enemy effectively
// We only include the first variant in the enemy pool so that they arent prominent
u16 randomizeEnemyVariants(u16 id) {
    if (id == 0x4a || id == 0x4d) {
        id += exl::util::GetRandomU64() % 3;
    }
    return id;
}

struct Vector3 {
    float x;
    float y;
    float z;
};

// although pretty much anything can work, these are ones that look the best
// we can easily add more later if we want
uint16_t chest_ids[] = {0xc, 0x10, 0x12, 0x19, 0x1b, 0x1c, 0x1d, 0x25, 0x34, 0x3c};

HOOK_DEFINE_INLINE(ObjTreasureBox__PopEnemy) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->W[9] = chest_ids[exl::util::GetRandomU64() % 10];
    }
};

namespace EnemyRandomizer {
    void RandomizeEnemy(u64 actorDataOffset) {
        u16* actorID = reinterpret_cast<u16*>(actorDataOffset + 0xc);
        std::vector<u16> vec = getValidEnemies(*actorID);
        if (vec.size() > 1) {
            u64* hash = reinterpret_cast<u64*>(actorDataOffset);
            if (!isRequiredKill(*hash)) {
                u16 new_enemy;
                do {
                    new_enemy = vec[exl::util::GetRandomU64() % vec.size()];
                }
                while (!isEnemyValid(*actorID, new_enemy));
                if (lastTen.size() >= 10) {
                    lastTen.pop_front();
                }
                lastTen.push_back((int)new_enemy);
                new_enemy = randomizeEnemyVariants(new_enemy);
                *actorID = new_enemy;
                // some enemies are rotated, we do not want them to randomize into an enemy that is rotation locked
                // while they still work, I just dont like how it looks
                f32* rotY = reinterpret_cast<f32*>(actorDataOffset + 0x24);
                *rotY = 0.0f;
                rotY = nullptr;
            }
            if (global_config.randomizer.enemy_sizes) {
                Vector3* scale = reinterpret_cast<Vector3*>(actorDataOffset + 0x2c);
                float scale_factor = exl::util::GetRandomF32(0.5f, 1.5f);
                scale->x = scale_factor;
                scale->y = scale_factor;
                scale->z = scale_factor;
                scale = nullptr;
            }
            hash = nullptr;
        }
        actorID = nullptr;
    }

    void InstallHooks() {
        EXL_ASSERT(global_config.initialized);
        if (global_config.randomizer.enemies) {
            ObjTreasureBox__PopEnemy::InstallAtOffset(offset_manager.Offset(0xca92c4));
        }
    }
}