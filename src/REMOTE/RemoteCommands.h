
#pragma once

#include <stdint.h>

namespace REMOTE {

// Actions captured from the BTF-LIGHTING RC03RFB (MiLight V2 FUT089).
enum class RemoteAction : uint8_t {
    ON,
    OFF,
    COLOR_CYCLE,
    SUN_TOP,
    MODE,
    O_MINUS,
    WHITE,
    O_PLUS,
    SPEED_MINUS,
    SUN_BOTTOM,
    SPEED_PLUS,
    GROUP_1_TOP,
    GROUP_1_BOTTOM,
    GROUP_2_TOP,
    GROUP_2_BOTTOM,
    GROUP_3_TOP,
    GROUP_3_BOTTOM,
    GROUP_4_TOP,
    GROUP_4_BOTTOM,
    WHEEL_RED,
    WHEEL_ORANGE,
    WHEEL_YELLOW,
    WHEEL_GREEN,
    WHEEL_CYAN,
    WHEEL_BLUE,
    WHEEL_PINK
};

struct RemoteCommand {
    uint8_t command;
    uint8_t argument;
    uint8_t group;
};

// Returns the number of captured variants for an action.
uint8_t getCommandVariantCount(RemoteAction action);

// Copies one variant into outCommand. Returns false for an invalid action/index
// or a null output pointer.
bool getCommandVariant(RemoteAction action,
                       uint8_t variantIndex,
                       RemoteCommand* outCommand);

// Human-readable action name, useful for Serial diagnostics.
const char* getActionName(RemoteAction action);

} // namespace REMOTE
