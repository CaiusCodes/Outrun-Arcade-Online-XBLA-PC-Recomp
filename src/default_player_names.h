#pragma once
#include <array>
#include <string_view>

namespace outrun::multiplayer {
// Player-name pool supplied by the user in outrun.txt.
inline constexpr std::array<std::string_view, 84> kDefaultPlayerNames{
    "Turbo Dave",
    "Turbo Gary",
    "Big Turbo",
    "Little Turbo",
    "OutRun Bob",
    "Out Runner",
    "Some Driver",
    "The Driver",
    "Road Guy",
    "Car Guy",
    "Fast Guy",
    "Road Lad",
    "Turbo Lad",
    "Big Wheels",
    "Wheel Man",
    "Sunday Driver",
    "Wrong Way",
    "No Brakes",
    "Lead Foot",
    "Heavy Foot",
    "Foot Down",
    "Flat Out",
    "Rev Head",
    "Gear Head",
    "Petrol Head",
    "Road Hog",
    "Burnout Bob",
    "Skid Mark",
    "Turbo",
    "Suzuki",
    "Nitro Nigel",
    "Turbo Tom",
    "Speedy Steve",
    "Ferrari Frank",
    "Miami Dave",
    "Malibu Bob",
    "Daytona Gary",
    "Sunset Driver",
    "Sunset Kid",
    "Palm Guy",
    "Beach Boy",
    "Coast Guy",
    "Cruise Dude",
    "Cruise Man",
    "Captain Cruise",
    "Big Kev",
    "Big Al",
    "Barry",
    "Gary",
    "Dave",
    "Bruce",
    "Frank",
    "Stig",
    "Road Runner",
    "The Dude",
    "Old Mate",
    "Good Mate",
    "Big Steve",
    "Big Dave",
    "Tiny Tim",
    "Clive",
    "Bob",
    "Doug",
    "Jeremy",
    "Nigel",
    "Trevor",
    "Terry",
    "Road Man",
    "Highway Man",
    "Fast Lad",
    "Hot Wheels",
    "Speed Boy",
    "Speed Demon",
    "Slow Poke",
    "Full Send",
    "Pedal Man",
    "Drift King",
    "Drift Guy",
    "Spin Doctor",
    "Road Warrior",
    "Highway Star",
    "Cruise Control",
    "Auto Pilot",
    "Redline",
};
// Local (high-score) names for the pool above, same order: up to 4 letters
// or numbers each, all different (what the Ranking screen gets while Local
// Name is blank and the online name is one of the pool's).
inline constexpr std::array<std::string_view, 84> kDefaultLocalNames{
    "TDAV",  // Turbo Dave
    "TGRY",  // Turbo Gary
    "BIGT",  // Big Turbo
    "LILT",  // Little Turbo
    "OBOB",  // OutRun Bob
    "ORUN",  // Out Runner
    "SOME",  // Some Driver
    "DRVR",  // The Driver
    "RGUY",  // Road Guy
    "CGUY",  // Car Guy
    "FGUY",  // Fast Guy
    "RLAD",  // Road Lad
    "TLAD",  // Turbo Lad
    "BIGW",  // Big Wheels
    "WMAN",  // Wheel Man
    "SUND",  // Sunday Driver
    "WWAY",  // Wrong Way
    "NOBR",  // No Brakes
    "LEAD",  // Lead Foot
    "HVFT",  // Heavy Foot
    "FTDN",  // Foot Down
    "FLAT",  // Flat Out
    "REVH",  // Rev Head
    "GEAR",  // Gear Head
    "PTRL",  // Petrol Head
    "RHOG",  // Road Hog
    "BURN",  // Burnout Bob
    "SKID",  // Skid Mark
    "TRBO",  // Turbo
    "SUZU",  // Suzuki
    "NITR",  // Nitro Nigel
    "TTOM",  // Turbo Tom
    "SPDY",  // Speedy Steve
    "FRRI",  // Ferrari Frank
    "MIAM",  // Miami Dave
    "MALI",  // Malibu Bob
    "DAYT",  // Daytona Gary
    "SUNS",  // Sunset Driver
    "KID",  // Sunset Kid
    "PALM",  // Palm Guy
    "BBOY",  // Beach Boy
    "COAS",  // Coast Guy
    "CDUD",  // Cruise Dude
    "CMAN",  // Cruise Man
    "CAPT",  // Captain Cruise
    "BKEV",  // Big Kev
    "BIGA",  // Big Al
    "BARY",  // Barry
    "GARY",  // Gary
    "DAVE",  // Dave
    "BRCE",  // Bruce
    "FRNK",  // Frank
    "STIG",  // Stig
    "RRUN",  // Road Runner
    "DUDE",  // The Dude
    "MATE",  // Old Mate
    "GDMT",  // Good Mate
    "BSTV",  // Big Steve
    "BDAV",  // Big Dave
    "TINY",  // Tiny Tim
    "CLIV",  // Clive
    "BOB",  // Bob
    "DOUG",  // Doug
    "JEZ",  // Jeremy
    "NIGE",  // Nigel
    "TREV",  // Trevor
    "TEZ",  // Terry
    "RMAN",  // Road Man
    "HWAY",  // Highway Man
    "FLAD",  // Fast Lad
    "HOTW",  // Hot Wheels
    "SBOY",  // Speed Boy
    "DEMN",  // Speed Demon
    "SLOW",  // Slow Poke
    "SEND",  // Full Send
    "PEDL",  // Pedal Man
    "KING",  // Drift King
    "DRFT",  // Drift Guy
    "SPIN",  // Spin Doctor
    "WARR",  // Road Warrior
    "STAR",  // Highway Star
    "CRUZ",  // Cruise Control
    "AUTO",  // Auto Pilot
    "REDL",  // Redline
};
static_assert(kDefaultLocalNames.size() == kDefaultPlayerNames.size());
}
