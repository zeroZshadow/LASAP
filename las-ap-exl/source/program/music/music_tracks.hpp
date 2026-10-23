#pragma once
// The Jukebox's track names and descriptions, for players. Ids in the order of apworld/las/music.py TRACKS
// (test_music checks it). Names fit one list row (at most 24 characters); an info is at most 80 characters, split
// by "\n" into two lines of at most 40. The lengths come from the RomFS (scripts/track_lengths.py writes them).
// constexpr only: a dynamically initialised static freezes the game.

namespace AP::Tracks {
    struct Track {
        const char* id;    // BGM label, as the client's MUSIC line names it
        const char* name;  // list row and title
        const char* info;  // what and where, under the list
        unsigned ms;       // one play-through (a short loop repeated to a minute), after which the Jukebox moves on
    };

    constexpr Track TRACKS[] = {
        {"BGM_DUNGEON_LV6_FACE", "Face Shrine", "The theme of Face Shrine, the sixth\ndungeon.", 197236},
        {"BGM_DUNGEON_LV4_ANGLER", "Angler's Tunnel", "The theme of Angler's Tunnel, the fourth\ndungeon.", 101778},
        {"BGM_CHICKEN_HURT", "Henhouse", "Heard in the Henhouse, home of the\ncuccos and their keeper.", 68215},
        {"BGM_GHOST_HOUSE", "House by the Bay", "Heard in the House by the Bay, the\nghost's old home.", 63285},
        {"BGM_ANIMAL_VILLAGE", "Animal Village", "The theme of Animal Village, home of the\ntalking animals.", 60291},
        {"BGM_DUNGEON_LV1_TAIL", "Tail Cave", "The theme of Tail Cave, the first\ndungeon.", 92730},
        {"BGM_DUNGEON_2D_SIDEVIEW", "Underground Passage", "Heard in the side-view passages beneath\nthe dungeons.", 64063},
        {"BGM_DUGEON_CASTLE", "Kanalet Castle", "Heard inside Kanalet Castle, where\nGolden Leaves are hidden.", 62198},
        {"BGM_DUNGEON_LV3_KEY", "Key Cavern", "The theme of Key Cavern, the third\ndungeon.", 93786},
        {"BGM_CAVE", "Cave", "Heard in the caves and wells all over\nKoholint Island.", 67513},
        {"BGM_TARUTARU", "Tal Tal Mountain Range", "The theme of the Tal Tal Mountain Range\nin the north of the island.", 274530},
        {"BGM_HOUSE", "Inside a House", "Heard in homes such as Marin and Tarin's\nHouse.", 66495},
        {"BGM_MEVE", "Mabe Village", "The theme of Mabe Village, where Link\nwakes up on Koholint.", 147174},
        {"BGM_FISHINGMAN", "Fishing Under the Bridge", "The fisherman's tune, on his boat under\nthe bridge at Martha's Bay.", 74914},
        {"BGM_DUNGEON_LV2_POT", "Bottle Grotto", "The theme of Bottle Grotto, the second\ndungeon.", 63400},
        {"BGM_STRANGE_FOREST", "Mysterious Forest", "The theme of the Mysterious Forest, full\nof mushrooms and Moblins.", 95875},
        {"BGM_DUNGEON_HOLY_EGG", "Wind Fish's Egg", "Heard inside the Wind Fish's Egg, at the\ntop of Mt. Tamaranch.", 96277},
        {"BGM_SEASHELL_HOUSE", "Seashell Mansion", "Heard in the Seashell Mansion, where\nsecret seashells are counted.", 110769},
        {"BGM_TELEPHONE", "Telephone Booth", "Heard in the telephone booths where Old\nMan Ulrira gives advice.", 66667},
        {"BGM_DANPEI", "Dampe's Shack", "Heard in Dampe's Shack, where chambers\nbecome dungeons.", 70752},
        {"BGM_SHOP", "Town Tool Shop", "Heard in the Town Tool Shop, the Witch's\nHut and the Raft Shop.", 76272},
        {"BGM_GAME_SHOP", "Trendy Game", "The tune of the Trendy Game, the crane\ngame in Mabe Village.", 70604},
        {"BGM_GOAT_HOUSE", "Christine's House", "Heard in the house of Christine the goat\nin Animal Village.", 67543},
        {"BGM_DREAMSHRINE", "Dream Shrine", "Heard inside the Dream Shrine in Mabe\nVillage.", 70179},
        {"BGM_WRIGHT", "Mr. Write's House", "Heard in the house of Mr. Write, who\nwrites letters to Christine.", 77143},
        {"BGM_GAME_SHOP_FOR_POND", "Fishing Pond", "Heard at the Fishing Pond, a tune much\nlike the Trendy Game's.", 64124},
        {"BGM_RICHARD", "Richard's Villa", "Heard in Richard's Villa, beside the\nPothole Field.", 96599},
        {"BGM_EVENT_RESCUE_BOWBOW", "BowWow Kidnapped", "Plays in Mabe Village while the Moblins\nhold BowWow.", 67046},
        {"BGM_DUNGEON_LV10_CLOTH", "Color Dungeon", "The theme of the Color Dungeon, found\nunder the Cemetery.", 143381},
        {"BGM_PLACE_OF_FACE_KEY", "Ancient Ruins", "Heard in the Ancient Ruins, where the\nFace Key and the mural lie.", 64931},
        {"BGM_DUNGEON_LV5_CATFISH", "Catfish's Maw", "The theme of Catfish's Maw, the fifth\ndungeon.", 64563},
        {"BGM_FAIRY", "Fairy Fountain", "Heard at the fountains where fairies\nrestore Link's hearts.", 67276},
        {"BGM_DUNGEON_LV7_TOWER", "Eagle's Tower", "The theme of Eagle's Tower, the seventh\ndungeon.", 210131},
        {"BGM_DUNGEON_LV8_TURTLE", "Turtle Rock", "The theme of Turtle Rock, the eighth\ndungeon.", 81929},
        {"BGM_FIELD_NORMAL", "Koholint Island", "The main theme of the island, heard\nacross its fields and shores.", 97215},
        {"BGM_FIELD_MARINE_NORMAL", "Koholint with Marin", "The island theme as it plays while\nMarin walks with Link.", 93069},
        {"BGM_LASTBOSS_APPEAR", "Last Nightmare", "A final battle theme for the Shadow\nNightmares.", 86306},
        {"BGM_LASTBOSS_BATTLE", "Shadow Nightmares", "The final battle against the Shadow\nNightmares in the Wind Fish's Egg.", 108816},
        {"BGM_MARINE_NAME", "File Named Marin", "A secret tune for a file named Marin.", 63567},
        {"BGM_MINIGAME_FISHING", "Fish on the Line", "Plays at the Fishing Pond while a fish\nfights on the line.", 68459},
        {"BGM_PANEL_DUNG_BEGINNER", "Chamber Dungeon: Easy", "Plays in an easy Chamber Dungeon from\nDampe's Shack.", 70483},
        {"BGM_PANEL_DUNG_DIFFICULT", "Chamber Dungeon: Hard", "Plays in a hard Chamber Dungeon from\nDampe's Shack.", 66899},
        {"BGM_PANEL_DUNG_MEDIUM", "Chamber Dungeon: Normal", "Plays in a medium Chamber Dungeon from\nDampe's Shack.", 69889},
        {"BGM_PANEL_EDIT_MODE", "Arranging Chambers", "Plays while arranging chambers into a\ndungeon with Dampe.", 60246},
        {"BGM_NAME_INPUT", "File Select", "Plays while choosing a file and entering\na name.", 81323},
        {"BGM_DUNGEON_BOSS", "Dungeon Boss", "The battle against the Nightmare at the\nend of each dungeon.", 82471},
        {"BGM_DUNGEON_BOSS_MIDDLE", "Dungeon Mini-Boss", "The battle against a mini-boss inside a\ndungeon.", 65749},
        {"BGM_DUNGEON_LV8_ENT_BATTLE", "Turtle Rock Entrance", "The battle against the stone turtle at\nthe door of Turtle Rock.", 73945},
        {"BGM_EVENT_MARINE_IN_BEACH", "Marin at the Beach", "Marin's tune by the sea at Toronbo\nShores.", 5357},
        {"BGM_EVENT_RESCUE_BOWBOW_INTRO", "BowWow Kidnapped Intro", "The opening of the theme that plays\nwhile BowWow is held.", 67039},
        {"BGM_FIELD_FIRST", "Sword Search", "Heard outside Mabe Village before Link\nhas found his sword.", 76428},
        {"BGM_FIELD_NORMAL_INTRO", "Koholint Island Intro", "The island theme with its opening, as\nLink finds his sword.", 97513},
        {"BGM_GAME_OF_RAFT", "Rapids Ride", "Plays while riding a raft down the\nrapids near Tal Tal Heights.", 73175},
        {"BGM_PANEL_SHADOW_LINK", "Shadow Link", "Plays when Shadow Link gives chase in a\nChamber Dungeon.", 73945},
        {"BGM_RAFTING_TIMEATTACK", "Rapids Race", "Plays during a timed raft race down the\nrapids.", 72367},
        {"BGM_RICHARD_230", "Richard's Villa (Alt)", "Another take on the theme of Richard's\nVilla.", 9953},
        {"BGM_STRANGE_FOREST_MARINE", "Forest with Marin", "The Mysterious Forest theme as it plays\nwhile Marin walks with Link.", 101255},
        {"BGM_TARUTARU2_AFTER_THE_RESCUE", "Tal Tal Mountains (Alt)", "Another take on the theme of the Tal\nTal Mountain Range.", 289711},
        {"BGM_TARUTARU_MARINE", "Tal Tal with Marin", "The Tal Tal Mountain Range theme as it\nplays while Marin walks with Link.", 106370},
        {"BGM_TOTAKEKE_SONG", "Totaka's Song", "A hidden tune by composer Kazumi Totaka,\nfor a certain file name.", 63674},
        {"BGM_ZELDA_NAME", "File Named Zelda", "A secret tune for a file named Zelda.", 61870},
        {"BGM_PANEL_RESULT", "Chamber Dungeon Clear", "Plays when a Chamber Dungeon is cleared.", 65947},
        {"BGM_DUNGEON_LV7_BOSS", "Evil Eagle", "The battle against the Evil Eagle atop\nEagle's Tower.", 102481},
        {"BGM_HOUSE_FIRST", "Link Awakens", "Plays as Link wakes up in Marin and\nTarin's House.", 61056},
        {"BGM_MADBATTER", "Mad Batter", "Plays when the Mad Batter appears in his\nwell.", 73882},
        {"BGM_EVENT_DATE", "Date with Marin", "Plays as Link and Marin sit together at\nToronbo Shores.", 85434},
        {"BGM_LASTBOSS_WIN", "Nightmares Defeated", "Plays after the Shadow Nightmares are\ndefeated.", 72942},
        {"BGM_OWL", "The Owl", "Plays when the owl lands to give Link\nadvice.", 78255},
        {"BGM_OWL_LAST", "The Owl's Last Words", "The owl's last words, at the door of the\nWind Fish's Egg.", 68064},
        {"BGM_DEMO_AFTER_LASTBOSS", "The Wind Fish", "Plays as the Wind Fish speaks to Link\nafter the final battle.", 70143},
    };

    constexpr int COUNT = sizeof(TRACKS) / sizeof(TRACKS[0]);
}
