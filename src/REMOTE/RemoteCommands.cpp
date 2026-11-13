#include "RemoteCommands.h"

namespace REMOTE {
namespace {

struct CommandEntry {
    RemoteAction action;
    const char* name;
    const RemoteCommand* variants;
    uint8_t count;
};

#define CMD(c, a, g) \
    { static_cast<uint8_t>(c), static_cast<uint8_t>(a), static_cast<uint8_t>(g) }

// ============================================================
// BASIC COMMANDS
// ============================================================

static const RemoteCommand V_ON[] =
{
    CMD(0x01, 0x00, 0x00)
};

static const RemoteCommand V_OFF[] =
{
    CMD(0x01, 0x09, 0x00)
};

static const RemoteCommand V_COLOR_CYCLE[] =
{
    CMD(0x02, 0x15, 0x00),
    CMD(0x02, 0x2B, 0x00)
};

static const RemoteCommand V_SUN_TOP[] =
{
    CMD(0x05, 0x50, 0x00),
    CMD(0x05, 0x5A, 0x00)
};

static const RemoteCommand V_MODE[] =
{
    CMD(0x06, 0x00, 0x00),
    CMD(0x06, 0x01, 0x00),
    CMD(0x06, 0x02, 0x00)
};

static const RemoteCommand V_O_MINUS[] =
{
    CMD(0x07, 0x0A, 0x00),
    CMD(0x07, 0x14, 0x00)
};

static const RemoteCommand V_WHITE[] =
{
    CMD(0x01, 0x14, 0x00)
};

static const RemoteCommand V_O_PLUS[] =
{
    CMD(0x07, 0x00, 0x00),
    CMD(0x07, 0x0A, 0x00)
};

static const RemoteCommand V_SPEED_MINUS[] =
{
    CMD(0x01, 0x13, 0x00)
};

static const RemoteCommand V_SUN_BOTTOM[] =
{
    CMD(0x05, 0x32, 0x00),
    CMD(0x05, 0x3C, 0x00),
    CMD(0x05, 0x46, 0x00),
    CMD(0x05, 0x50, 0x00)
};

static const RemoteCommand V_SPEED_PLUS[] =
{
    CMD(0x01, 0x12, 0x00)
};

// ============================================================
// GROUP BUTTONS
// ============================================================

// TOP buttons carry the group number in GROUP.
static const RemoteCommand V_G1_TOP[] =
{
    CMD(0x01, 0x01, 0x01)
};

static const RemoteCommand V_G2_TOP[] =
{
    CMD(0x01, 0x02, 0x02)
};

static const RemoteCommand V_G3_TOP[] =
{
    CMD(0x01, 0x03, 0x03)
};

static const RemoteCommand V_G4_TOP[] =
{
    CMD(0x01, 0x04, 0x04)
};

// BOTTOM buttons have GROUP = 0x00.
// Confirmed from individual captures of all four buttons.
static const RemoteCommand V_G1_BOTTOM[] =
{
    CMD(0x01, 0x0A, 0x00)
};

static const RemoteCommand V_G2_BOTTOM[] =
{
    CMD(0x01, 0x0B, 0x00)
};

static const RemoteCommand V_G3_BOTTOM[] =
{
    CMD(0x01, 0x0C, 0x00)
};

static const RemoteCommand V_G4_BOTTOM[] =
{
    CMD(0x01, 0x0D, 0x00)
};

// ============================================================
// COLOR WHEEL CAPTURES
// ============================================================

// Captured color positions. Values intentionally unchanged.

static const RemoteCommand V_WHEEL_RED[] =
{
    CMD(0x02, 0x02, 0x00),
    CMD(0x02, 0xF6, 0x00)
};

static const RemoteCommand V_WHEEL_ORANGE[] =
{
    CMD(0x02, 0x15, 0x00),
    CMD(0x02, 0x19, 0x00),
    CMD(0x02, 0x20, 0x00)
};

static const RemoteCommand V_WHEEL_YELLOW[] =
{
    CMD(0x02, 0x25, 0x00),
    CMD(0x02, 0x27, 0x00),
    CMD(0x02, 0x2D, 0x00)
};

static const RemoteCommand V_WHEEL_GREEN[] =
{
    CMD(0x02, 0x48, 0x00),
    CMD(0x02, 0x4E, 0x00),
    CMD(0x02, 0x4F, 0x00)
};

static const RemoteCommand V_WHEEL_CYAN[] =
{
    CMD(0x02, 0x8E, 0x00),
    CMD(0x02, 0x91, 0x00),
    CMD(0x02, 0x93, 0x00)
};

static const RemoteCommand V_WHEEL_BLUE[] =
{
    CMD(0x02, 0xB2, 0x00),
    CMD(0x02, 0xB6, 0x00)
};

static const RemoteCommand V_WHEEL_PINK[] =
{
    CMD(0x02, 0xD5, 0x00),
    CMD(0x02, 0xDD, 0x00),
    CMD(0x02, 0xDE, 0x00)
};

#undef CMD

#define ENTRY(action, label, values) \
    { \
        RemoteAction::action, \
        label, \
        values, \
        static_cast<uint8_t>(sizeof(values) / sizeof((values)[0])) \
    }

// ============================================================
// COMMAND TABLE
// ============================================================

static const CommandEntry ENTRIES[] =
{
    ENTRY(ON, "ON", V_ON),
    ENTRY(OFF, "OFF", V_OFF),
    ENTRY(COLOR_CYCLE, "COLOR_CYCLE", V_COLOR_CYCLE),

    ENTRY(SUN_TOP, "SUN_TOP", V_SUN_TOP),
    ENTRY(MODE, "MODE", V_MODE),
    ENTRY(O_MINUS, "O_MINUS", V_O_MINUS),
    ENTRY(WHITE, "WHITE", V_WHITE),
    ENTRY(O_PLUS, "O_PLUS", V_O_PLUS),
    ENTRY(SPEED_MINUS, "SPEED_MINUS", V_SPEED_MINUS),
    ENTRY(SUN_BOTTOM, "SUN_BOTTOM", V_SUN_BOTTOM),
    ENTRY(SPEED_PLUS, "SPEED_PLUS", V_SPEED_PLUS),

    ENTRY(GROUP_1_TOP, "GROUP_1_TOP", V_G1_TOP),
    ENTRY(GROUP_1_BOTTOM, "GROUP_1_BOTTOM", V_G1_BOTTOM),
    ENTRY(GROUP_2_TOP, "GROUP_2_TOP", V_G2_TOP),
    ENTRY(GROUP_2_BOTTOM, "GROUP_2_BOTTOM", V_G2_BOTTOM),
    ENTRY(GROUP_3_TOP, "GROUP_3_TOP", V_G3_TOP),
    ENTRY(GROUP_3_BOTTOM, "GROUP_3_BOTTOM", V_G3_BOTTOM),
    ENTRY(GROUP_4_TOP, "GROUP_4_TOP", V_G4_TOP),
    ENTRY(GROUP_4_BOTTOM, "GROUP_4_BOTTOM", V_G4_BOTTOM),

    ENTRY(WHEEL_RED, "WHEEL_RED", V_WHEEL_RED),
    ENTRY(WHEEL_ORANGE, "WHEEL_ORANGE", V_WHEEL_ORANGE),
    ENTRY(WHEEL_YELLOW, "WHEEL_YELLOW", V_WHEEL_YELLOW),
    ENTRY(WHEEL_GREEN, "WHEEL_GREEN", V_WHEEL_GREEN),
    ENTRY(WHEEL_CYAN, "WHEEL_CYAN", V_WHEEL_CYAN),
    ENTRY(WHEEL_BLUE, "WHEEL_BLUE", V_WHEEL_BLUE),
    ENTRY(WHEEL_PINK, "WHEEL_PINK", V_WHEEL_PINK)
};

#undef ENTRY

const CommandEntry* findEntry(RemoteAction action)
{
    for (
        uint8_t i = 0;
        i < sizeof(ENTRIES) / sizeof(ENTRIES[0]);
        ++i
    )
    {
        if (ENTRIES[i].action == action)
        {
            return &ENTRIES[i];
        }
    }

    return nullptr;
}

} // namespace

// ============================================================
// PUBLIC API
// ============================================================

uint8_t getCommandVariantCount(RemoteAction action)
{
    const CommandEntry* entry = findEntry(action);

    return entry ? entry->count : 0;
}

bool getCommandVariant(
    RemoteAction action,
    uint8_t variantIndex,
    RemoteCommand* outCommand
)
{
    if (outCommand == nullptr)
    {
        return false;
    }

    const CommandEntry* entry = findEntry(action);

    if (
        entry == nullptr ||
        variantIndex >= entry->count
    )
    {
        return false;
    }

    *outCommand = entry->variants[variantIndex];

    return true;
}

// ============================================================
// COMMAND IDENTIFICATION
// ============================================================

bool identifyCommand(
    uint8_t command,
    uint8_t argument,
    uint8_t group,
    RemoteAction* outAction
)
{
    if (outAction == nullptr)
    {
        return false;
    }

    // Bit 7 is the held/repeat flag.
    const uint8_t commandBase = command & 0x7F;

    // COMMAND 0x02 is treated as a generic color-related action.
    // Its argument alone cannot reliably distinguish COLOR CYCLE
    // from color-wheel movement.
    if (
        commandBase == 0x02 &&
        group == 0x00
    )
    {
        *outAction = RemoteAction::COLOR_CONTROL;
        return true;
    }

    // Match all other commands against captured variants.
    for (
        uint8_t i = 0;
        i < sizeof(ENTRIES) / sizeof(ENTRIES[0]);
        ++i
    )
    {
        const CommandEntry& entry = ENTRIES[i];

        for (uint8_t v = 0; v < entry.count; ++v)
        {
            const RemoteCommand& variant = entry.variants[v];

            if (
                (variant.command & 0x7F) == commandBase &&
                variant.argument == argument &&
                variant.group == group
            )
            {
                *outAction = entry.action;
                return true;
            }
        }
    }

    return false;
}

// ============================================================
// ACTION NAME
// ============================================================

const char* getActionName(RemoteAction action)
{
    if (action == RemoteAction::COLOR_CONTROL)
    {
        return "COLOR_CONTROL";
    }

    const CommandEntry* entry = findEntry(action);

    return entry ? entry->name : "UNKNOWN";
}

} // namespace REMOTE