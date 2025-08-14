#pragma once

enum QuestResultHQBackgroundMode {
    // Image capture after ReShade, ReShade will be turned off on quest screen
    ReshadePreapplied = 1,

    // Image capture before ReShade, ReShade will be enabled on quest screen
    ReshadeApplyLater = 2,

    // Image capture before ReShade, ReShade will be turned off on quest screen
    NoReshade = 3,

    // Frame provided by the game through REEngine, unaffected by DLSS framegen
    REEngineFrame = 4,
};
