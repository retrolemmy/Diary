#include "Processes.r"
#include "Menus.r"
#include "Dialogs.r"
#include "Finder.r"

resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About Diary...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "Save", noIcon, "S", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Select All", noIcon, "A", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Find...", noIcon, "F", noMark, plain;
        "Find Again", noIcon, "G", noMark, plain;
        "Show All Entries", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (131) {
    131, textMenuProc;
    allEnabled, enabled;
    "Go";
    {
        "Previous Day", noIcon, "[", noMark, plain;
        "Next Day", noIcon, "]", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Today", noIcon, "T", noMark, plain;
    }
};

resource 'MENU' (132) {
    132, textMenuProc;
    allEnabled, enabled;
    "Font";
    {
        /* filled in with the installed fonts */
    }
};

resource 'MENU' (133) {
    133, textMenuProc;
    allEnabled, enabled;
    "Size";
    {
        "9 Point", noIcon, noKey, noMark, plain;
        "10 Point", noIcon, noKey, noMark, plain;
        "12 Point", noIcon, noKey, noMark, plain;
        "14 Point", noIcon, noKey, noMark, plain;
        "18 Point", noIcon, noKey, noMark, plain;
        "24 Point", noIcon, noKey, noMark, plain;
    }
};

resource 'MBAR' (128) {
    { 128, 129, 130, 131, 132, 133 };
};

/* About box */
resource 'DLOG' (128) {
    { 0, 0, 135, 320 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    128,
    "",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        { 100, 230, 120, 300 },
        Button { enabled, "OK" };

        { 15, 62, 33, 300 },
        StaticText { disabled, "Diary 1.3" };

        { 38, 62, 92, 300 },
        StaticText { disabled, "A simple day-by-day journal for the classic Mac OS. Entries are kept in the \"Diary Entries\" folder." };

        { 96, 226, 124, 304 },
        UserItem { disabled };

        { 15, 15, 47, 47 },
        Icon { disabled, 128 };
    }
};

/* Message; the text is filled in with ParamText */
resource 'DLOG' (129) {
    { 0, 0, 110, 320 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    129,
    "",
    centerMainScreen
};

resource 'DITL' (129) {
    {
        { 75, 230, 95, 300 },
        Button { enabled, "OK" };

        { 15, 20, 65, 300 },
        StaticText { disabled, "^0" };

        { 71, 226, 99, 304 },
        UserItem { disabled };
    }
};

/* Find */
resource 'DLOG' (130) {
    { 0, 0, 100, 320 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    130,
    "",
    centerMainScreen
};

resource 'DITL' (130) {
    {
        { 68, 230, 88, 300 },
        Button { enabled, "Find" };

        { 68, 150, 88, 220 },
        Button { enabled, "Cancel" };

        { 12, 15, 30, 300 },
        StaticText { disabled, "Find entries containing:" };

        { 36, 18, 52, 302 },
        EditText { enabled, "" };

        { 64, 226, 92, 304 },
        UserItem { disabled };
    }
};

/* Finder bundle: ties the 'DIAR' creator to the application icon. */
type 'DIAR' as 'STR ';

resource 'DIAR' (0, "Owner resource") {
    "Diary 1.3"
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'BNDL' (128) {
    'DIAR', 0,
    {
        'ICN#', { 0, 128 };
        'FREF', { 0, 128 };
    }
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    384 * 1024,
    384 * 1024
};
