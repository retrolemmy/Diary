/*
    Diary - a simple one-page-per-day journal for the classic Mac OS (68k).

    Each day's entry is stored as a plain TEXT file named YYYY-MM-DD in a
    "Diary Entries" folder next to the application, so entries can also be
    opened with SimpleText/TeachText.

    The window shows a list of all entries on the left and the current
    day's entry on the right. Find narrows the list to matching entries.
*/

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Devices.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Resources.h>
#include <Sound.h>
#include <string.h>

enum {
    kMenuApple = 128,
    kMenuFile,
    kMenuEdit,
    kMenuGo,
    kMenuFont,
    kMenuSize
};

enum { kItemAbout = 1 };
enum { kItemSave = 1, kItemQuit = 3 };
enum {
    kItemUndo = 1, kItemCut = 3, kItemCopy, kItemPaste, kItemClear,
    kItemSelectAll = 8, kItemFind = 10, kItemFindAgain, kItemShowAll
};
enum { kItemPrevDay = 1, kItemNextDay, kItemToday = 4 };

enum { kDlogAbout = 128, kDlogMessage = 129, kDlogFind = 130 };
enum { kAboutRingItem = 4, kMessageRingItem = 3 };
enum { kFindOK = 1, kFindCancel = 2, kFindTextItem = 4, kFindRingItem = 5 };

/* Control part codes (Inside Macintosh: Toolbox Essentials) */
enum {
    kPartButton = 10,
    kPartUp = 20,
    kPartDown = 21,
    kPartPageUp = 22,
    kPartPageDown = 23,
    kPartThumb = 129
};

enum {
    kHeaderHeight = 32,
    kListWidth = 160,               /* entry list, including its scroll bar */
    kPaneLeft = kListWidth + 1,     /* left edge of the entry pane */
    kScrollWidth = 16,
    kTextMargin = 4,
    kMaxTextLength = 32000,         /* TextEdit's practical limit */
    kSecondsPerDay = 86400L
};

/* Month calendar at the top of the left column, above the entry list */
enum {
    kCalTop = kHeaderHeight,
    kCalLeft = 6,
    kCalCellWidth = 21,
    kCalCellHeight = 14,
    kCalGridTop = kCalTop + 34,
    kCalBottom = kCalGridTop + 6 * kCalCellHeight + 3,     /* separator line */
    kListTop = kCalBottom + 1
};

static WindowPtr gWindow;
static TEHandle gTE;
static ListHandle gList;
static ControlHandle gScroll, gPrevButton, gNextButton, gTodayButton;
static long gDay;                   /* days since 1 Jan 1904, the Mac epoch */
static Boolean gDirty;
static Boolean gQuit;
static short gVRefNum;
static long gEntriesDirID;
static Boolean gHaveFolder;

/* Days that have an entry file, and the days shown in the list (all of
   them, or only those matching gSearch). Both are sorted newest first. */
static Handle gAllDays, gShownDays;
static short gAllCount, gShownCount;
static Str255 gSearch;

/* Font and size of the entry text, remembered in the settings file. */
static short gTextFont = applFont;
static short gTextSize = 12;
static const short kSizes[] = { 9, 10, 12, 14, 18, 24 };
enum { kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]) };

/* The month the calendar shows; follows gDay, but the arrows can move it. */
static long gCalYear;
static short gCalMonth;

static const char *kWeekdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *kMonths[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static void AdjustScrollBar(void);
static void RefreshList(void);
static void SyncListSelection(void);
static void ShowMonthOf(long day);

/* ---------- Pascal string helpers ---------- */

static void PAppendN(Str255 s, const char *c, short max)
{
    while (*c && max-- > 0 && s[0] < 255)
        s[++s[0]] = *c++;
}

static void PAppend(Str255 s, const char *c)
{
    PAppendN(s, c, 255);
}

static void PAppendNum(Str255 s, long n, short minDigits)
{
    char buf[12];
    short i = 0;

    do {
        buf[i++] = '0' + n % 10;
        n /= 10;
    } while (n > 0);
    while (i < minDigits)
        buf[i++] = '0';
    while (i > 0 && s[0] < 255)
        s[++s[0]] = buf[--i];
}

static void PSet(Str255 s, const char *c)
{
    s[0] = 0;
    PAppend(s, c);
}

static Boolean PEqual(ConstStr255Param a, ConstStr255Param b)
{
    return memcmp(a, b, a[0] + 1) == 0;
}

/* ---------- Date arithmetic ---------- */

/* Converts days since 1 Jan 1904 to a civil date (H. Hinnant's algorithm). */
static void DayToDate(long day, long *year, short *month, short *dayOfMonth)
{
    long z = day - 24107L + 719468L;    /* days since 1 Mar 0000 */
    long era = z / 146097L;
    long doe = z - era * 146097L;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long m = mp < 10 ? mp + 3 : mp - 9;

    *year = yoe + era * 400 + (m <= 2);
    *month = m;
    *dayOfMonth = doy - (153 * mp + 2) / 5 + 1;
}

/* The inverse of DayToDate. */
static long DateToDay(long year, short month, short dayOfMonth)
{
    long y = year - (month <= 2);
    long era = y / 400;
    long yoe = y - era * 400;
    long doy = (153L * (month > 2 ? month - 3 : month + 9) + 2) / 5 + dayOfMonth - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097L + doe - 719468L + 24107L;
}

static long Today(void)
{
    unsigned long secs;

    GetDateTime(&secs);
    return secs / kSecondsPerDay;
}

static short Weekday(long day)
{
    return (day + 5) % 7;       /* 1 Jan 1904 was a Friday */
}

static void MakeEntryName(long day, Str255 name)
{
    long y;
    short m, d;

    DayToDate(day, &y, &m, &d);
    name[0] = 0;
    PAppendNum(name, y, 4);
    PAppend(name, "-");
    PAppendNum(name, m, 2);
    PAppend(name, "-");
    PAppendNum(name, d, 2);
}

/* Parses a YYYY-MM-DD file name; rejects anything else. */
static Boolean ParseEntryName(ConstStr255Param name, long *day)
{
    static const char kPattern[] = "####-##-##";
    long y, checkYear;
    short m, d, i, checkMonth, checkDay;

    if (name[0] != 10)
        return false;
    for (i = 0; i < 10; i++) {
        unsigned char c = name[i + 1];
        if (kPattern[i] == '#' ? (c < '0' || c > '9') : c != '-')
            return false;
    }
    y = (name[1] - '0') * 1000L + (name[2] - '0') * 100 + (name[3] - '0') * 10 + (name[4] - '0');
    m = (name[6] - '0') * 10 + (name[7] - '0');
    d = (name[9] - '0') * 10 + (name[10] - '0');
    if (y < 1904 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;

    *day = DateToDay(y, m, d);
    DayToDate(*day, &checkYear, &checkMonth, &checkDay);   /* rejects e.g. 31 April */
    return checkYear == y && checkMonth == m && checkDay == d;
}

static void MakeLongDate(long day, Str255 s)
{
    long y;
    short m, d;

    DayToDate(day, &y, &m, &d);
    PSet(s, kWeekdays[Weekday(day)]);
    PAppend(s, ", ");
    PAppend(s, kMonths[m - 1]);
    PAppend(s, " ");
    PAppendNum(s, d, 1);
    PAppend(s, ", ");
    PAppendNum(s, y, 4);
}

static void MakeShortDate(long day, Str255 s)
{
    long y;
    short m, d;

    DayToDate(day, &y, &m, &d);
    s[0] = 0;
    PAppendN(s, kWeekdays[Weekday(day)], 3);
    PAppend(s, " ");
    PAppendN(s, kMonths[m - 1], 3);
    PAppend(s, " ");
    PAppendNum(s, d, 1);
    PAppend(s, ", ");
    PAppendNum(s, y, 4);
}

/* ---------- Day lists (handles of longs, newest first) ---------- */

static long *Days(Handle h)
{
    return (long *)*h;
}

static short FindDay(Handle h, short count, long day)
{
    short i;

    for (i = 0; i < count; i++)
        if (Days(h)[i] == day)
            return i;
    return -1;
}

static Boolean InsertDay(Handle h, short *count, long day)
{
    short i;

    if (FindDay(h, *count, day) >= 0)
        return true;
    SetHandleSize(h, (*count + 1) * sizeof(long));
    if (MemError() != noErr)
        return false;
    for (i = *count; i > 0 && Days(h)[i - 1] < day; i--)
        Days(h)[i] = Days(h)[i - 1];
    Days(h)[i] = day;
    (*count)++;
    return true;
}

static void RemoveDay(Handle h, short *count, long day)
{
    short i = FindDay(h, *count, day);

    if (i < 0)
        return;
    for (; i < *count - 1; i++)
        Days(h)[i] = Days(h)[i + 1];
    (*count)--;
}

/* ---------- Dialogs ---------- */

/* Draws the bold outline around a dialog's default button (item 1). */
static pascal void DrawDefaultRing(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;

    GetDialogItem(dlg, 1, &type, &h, &r);
    InsetRect(&r, -4, -4);
    PenSize(3, 3);
    FrameRoundRect(&r, 16, 16);
    PenSize(1, 1);
}

static void InstallDefaultRing(DialogPtr dlg, short ringItem)
{
    short type;
    Handle h;
    Rect r;

    GetDialogItem(dlg, ringItem, &type, &h, &r);
    SetDialogItem(dlg, ringItem, userItem, (Handle)&DrawDefaultRing, &r);
}

static void ShowMessage(const char *message, OSErr err, Boolean beep)
{
    Str255 msg, empty;
    short item;
    DialogPtr dlg;

    PSet(msg, message);
    empty[0] = 0;
    if (err != noErr) {
        PAppend(msg, " (error ");
        if (err < 0) {
            PAppend(msg, "-");
            PAppendNum(msg, -(long)err, 1);
        } else {
            PAppendNum(msg, err, 1);
        }
        PAppend(msg, ")");
    }
    ParamText(msg, empty, empty, empty);

    if (beep)
        SysBeep(10);
    dlg = GetNewDialog(kDlogMessage, NULL, (WindowPtr)-1);
    if (dlg == NULL)
        return;
    InstallDefaultRing(dlg, kMessageRingItem);
    ModalDialog(NULL, &item);
    DisposeDialog(dlg);
}

static void ShowError(const char *message, OSErr err)
{
    ShowMessage(message, err, true);
}

static void ShowAbout(void)
{
    short item;
    DialogPtr dlg = GetNewDialog(kDlogAbout, NULL, (WindowPtr)-1);

    if (dlg == NULL)
        return;
    InstallDefaultRing(dlg, kAboutRingItem);
    ModalDialog(NULL, &item);
    DisposeDialog(dlg);
}

static Boolean AskForSearchText(Str255 text)
{
    short item, type;
    Handle field;
    Rect r;
    DialogPtr dlg = GetNewDialog(kDlogFind, NULL, (WindowPtr)-1);

    if (dlg == NULL)
        return false;
    InstallDefaultRing(dlg, kFindRingItem);
    GetDialogItem(dlg, kFindTextItem, &type, &field, &r);
    SetDialogItemText(field, gSearch);
    SelectDialogItemText(dlg, kFindTextItem, 0, 32767);

    do {
        ModalDialog(NULL, &item);
    } while (item != kFindOK && item != kFindCancel);

    if (item == kFindOK)
        GetDialogItemText(field, text);
    DisposeDialog(dlg);
    return item == kFindOK;
}

/* ---------- Entry storage ---------- */

static void FindEntriesFolder(void)
{
    WDPBRec wd;
    CInfoPBRec cat;
    HParamBlockRec hpb;
    Str255 folder;
    long appDirID;
    OSErr err;

    /* At launch the default directory is the application's folder. */
    memset(&wd, 0, sizeof(wd));
    if (PBHGetVolSync(&wd) != noErr)
        return;
    gVRefNum = wd.ioWDVRefNum;
    appDirID = wd.ioWDDirID;

    PSet(folder, "Diary Entries");
    memset(&cat, 0, sizeof(cat));
    cat.dirInfo.ioNamePtr = folder;
    cat.dirInfo.ioVRefNum = gVRefNum;
    cat.dirInfo.ioDrDirID = appDirID;
    cat.dirInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&cat);
    if (err == noErr && (cat.dirInfo.ioFlAttrib & 0x10)) {
        gEntriesDirID = cat.dirInfo.ioDrDirID;
        gHaveFolder = true;
        return;
    }

    memset(&hpb, 0, sizeof(hpb));
    hpb.fileParam.ioNamePtr = folder;
    hpb.fileParam.ioVRefNum = gVRefNum;
    hpb.fileParam.ioDirID = appDirID;
    err = PBDirCreateSync(&hpb);
    if (err == noErr) {
        gEntriesDirID = hpb.fileParam.ioDirID;
        gHaveFolder = true;
    } else {
        ShowError("Could not create the \"Diary Entries\" folder.", err);
    }
}

/* Fills gAllDays with every YYYY-MM-DD file in the entries folder. */
static void ScanEntries(void)
{
    CInfoPBRec cat;
    Str255 name;
    short index;
    long day;

    gAllCount = 0;
    if (!gHaveFolder)
        return;
    for (index = 1; ; index++) {
        memset(&cat, 0, sizeof(cat));
        cat.hFileInfo.ioNamePtr = name;
        cat.hFileInfo.ioVRefNum = gVRefNum;
        cat.hFileInfo.ioDirID = gEntriesDirID;
        cat.hFileInfo.ioFDirIndex = index;
        if (PBGetCatInfoSync(&cat) != noErr)
            break;
        if (cat.hFileInfo.ioFlAttrib & 0x10)
            continue;
        if (ParseEntryName(name, &day))
            InsertDay(gAllDays, &gAllCount, day);
    }
}

/* Reads a file in the entries folder into buf (resized to fit, at most
   kMaxTextLength bytes). */
static OSErr ReadEntriesFile(ConstStr255Param name, Handle buf, long *len)
{
    short refNum;
    OSErr err;

    *len = 0;
    SetHandleSize(buf, 0);
    if (!gHaveFolder)
        return fnfErr;
    err = HOpen(gVRefNum, gEntriesDirID, name, fsRdPerm, &refNum);
    if (err != noErr)
        return err;
    err = GetEOF(refNum, len);
    if (err == noErr) {
        if (*len > kMaxTextLength)
            *len = kMaxTextLength;
        SetHandleSize(buf, *len);
        err = MemError();
    }
    if (err == noErr) {
        HLock(buf);
        err = FSRead(refNum, len, *buf);
        HUnlock(buf);
    }
    FSClose(refNum);
    if (err != noErr)
        *len = 0;
    return err;
}

/* Creates or replaces a file in the entries folder. */
static OSErr WriteEntriesFile(ConstStr255Param name, OSType creator, OSType type,
                              Ptr data, long count)
{
    short refNum;
    OSErr err;

    err = HCreate(gVRefNum, gEntriesDirID, name, creator, type);
    if (err != noErr && err != dupFNErr)
        return err;
    err = HOpen(gVRefNum, gEntriesDirID, name, fsWrPerm, &refNum);
    if (err != noErr)
        return err;
    err = FSWrite(refNum, &count, data);
    if (err == noErr)
        err = SetEOF(refNum, count);
    FSClose(refNum);
    FlushVol(NULL, gVRefNum);
    return err;
}

/* Reads a day's entry into buf (resized to fit). Missing files read as empty. */
static OSErr ReadEntry(long day, Handle buf, long *len)
{
    Str255 name;

    MakeEntryName(day, name);
    return ReadEntriesFile(name, buf, len);
}

static void LoadEntry(void)
{
    Handle h = NewHandle(0);
    long len = 0;

    if (h != NULL)
        ReadEntry(gDay, h, &len);

    TESetSelect(0, 32767, gTE);
    TEDelete(gTE);
    (**gTE).destRect = (**gTE).viewRect;
    if (h != NULL) {
        HLock(h);
        TESetText(*h, len, gTE);
        DisposeHandle(h);
    }
    TESetSelect(32767, 32767, gTE);
    gDirty = false;
    ShowMonthOf(gDay);

    SetPort(gWindow);
    InvalRect(&gWindow->portRect);
    AdjustScrollBar();
    SyncListSelection();
}

static Boolean SaveEntry(void)
{
    Str255 name;
    long count;
    Handle text;
    OSErr err;

    if (!gDirty)
        return true;
    if (!gHaveFolder) {
        ShowError("There is no \"Diary Entries\" folder to save into.", noErr);
        return false;
    }

    MakeEntryName(gDay, name);
    count = (**gTE).teLength;

    if (count == 0) {
        /* An emptied entry is removed rather than kept as an empty file. */
        HDelete(gVRefNum, gEntriesDirID, name);
        gDirty = false;
        RemoveDay(gAllDays, &gAllCount, gDay);
        RemoveDay(gShownDays, &gShownCount, gDay);
        RefreshList();
        return true;
    }

    text = (Handle)TEGetText(gTE);
    HLock(text);
    err = WriteEntriesFile(name, 'ttxt', 'TEXT', *text, count);
    HUnlock(text);

    if (err != noErr) {
        ShowError("Could not save the entry.", err);
        return false;
    }
    gDirty = false;

    if (FindDay(gAllDays, gAllCount, gDay) < 0) {
        InsertDay(gAllDays, &gAllCount, gDay);
        if (gSearch[0] == 0)
            InsertDay(gShownDays, &gShownCount, gDay);
        RefreshList();
    }
    return true;
}

static void GoToDay(long day)
{
    if (day == gDay) {
        SyncListSelection();
        return;
    }
    if (!SaveEntry()) {
        SyncListSelection();
        return;
    }
    gDay = day;
    LoadEntry();
}

/* ---------- Searching ---------- */

static unsigned char FoldCase(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Case-insensitive search for pattern in text, starting at from. */
static long FindText(const char *text, long len, long from, ConstStr255Param pattern)
{
    long i;
    short j, n = pattern[0];

    for (i = from; i + n <= len; i++) {
        for (j = 0; j < n; j++)
            if (FoldCase(text[i + j]) != FoldCase(pattern[j + 1]))
                break;
        if (j == n)
            return i;
    }
    return -1;
}

static Boolean EntryContains(long day, Handle buf)
{
    long len;

    if (ReadEntry(day, buf, &len) != noErr)
        return false;
    return FindText(*buf, len, 0, gSearch) >= 0;
}

/* Selects the next match in the open entry at or after from. */
static Boolean SelectMatchFrom(long from)
{
    long found = FindText(*(**gTE).hText, (**gTE).teLength, from, gSearch);

    if (found < 0)
        return false;
    TESetSelect(found, found + gSearch[0], gTE);
    TESelView(gTE);
    AdjustScrollBar();
    return true;
}

static void ShowAllEntries(void)
{
    gSearch[0] = 0;
    SetHandleSize(gShownDays, gAllCount * sizeof(long));
    if (MemError() == noErr) {
        BlockMove(*gAllDays, *gShownDays, gAllCount * sizeof(long));
        gShownCount = gAllCount;
    }
    RefreshList();
}

static void DoFind(void)
{
    Str255 text, previous;
    Handle buf, matches;
    short i, matchCount = 0;

    if (!AskForSearchText(text))
        return;
    if (text[0] == 0) {
        ShowAllEntries();
        return;
    }
    if (!SaveEntry())
        return;

    buf = NewHandle(0);
    matches = NewHandle(0);
    if (buf == NULL || matches == NULL) {
        ShowError("Not enough memory to search.", MemError());
        goto done;
    }

    SetCursor(*GetCursor(watchCursor));
    BlockMove(gSearch, previous, gSearch[0] + 1);
    BlockMove(text, gSearch, text[0] + 1);
    for (i = 0; i < gAllCount; i++) {
        long day = Days(gAllDays)[i];
        if (EntryContains(day, buf))
            InsertDay(matches, &matchCount, day);
    }
    SetCursor(&qd.arrow);

    if (matchCount == 0) {
        ShowMessage("No entries contain that text.", noErr, false);
        BlockMove(previous, gSearch, previous[0] + 1);    /* keep the current filter */
        goto done;
    }

    SetHandleSize(gShownDays, matchCount * sizeof(long));
    BlockMove(*matches, *gShownDays, matchCount * sizeof(long));
    gShownCount = matchCount;
    RefreshList();

    if (FindDay(gShownDays, gShownCount, gDay) < 0)
        GoToDay(Days(gShownDays)[0]);
    SelectMatchFrom(0);

done:
    if (buf != NULL)
        DisposeHandle(buf);
    if (matches != NULL)
        DisposeHandle(matches);
}

/* Moves to the next match: first within the open entry, then on to the
   following (older) matching entries, wrapping around to the newest. */
static void DoFindAgain(void)
{
    Handle buf;
    short start, step;

    if (gSearch[0] == 0) {
        DoFind();
        return;
    }
    if (SelectMatchFrom((**gTE).selEnd))
        return;

    buf = NewHandle(0);
    if (buf == NULL)
        return;
    start = FindDay(gShownDays, gShownCount, gDay);
    for (step = 1; step <= gShownCount; step++) {
        short i = start < 0 ? step - 1 : (start + step) % gShownCount;
        long day = Days(gShownDays)[i];

        if (day == gDay) {
            if (SelectMatchFrom(0))
                break;
        } else if (EntryContains(day, buf)) {
            GoToDay(day);
            SelectMatchFrom(0);
            break;
        }
    }
    if (step > gShownCount)
        SysBeep(10);
    DisposeHandle(buf);
}

/* ---------- Scrolling ---------- */

static short TotalLines(void)
{
    TEPtr te = *gTE;
    short lines = te->nLines;

    /* TextEdit doesn't count the empty line after a trailing return. */
    if (te->teLength > 0 && (*te->hText)[te->teLength - 1] == '\r')
        lines++;
    return lines;
}

static short VisibleLines(void)
{
    TEPtr te = *gTE;

    return (te->viewRect.bottom - te->viewRect.top) / te->lineHeight;
}

static void AdjustScrollBar(void)
{
    TEPtr te = *gTE;
    short lineHeight = te->lineHeight;
    short value = (te->viewRect.top - te->destRect.top + lineHeight - 1) / lineHeight;
    short max = TotalLines() - VisibleLines();

    if (max < 0)
        max = 0;
    if (value > max)
        max = value;
    SetControlMaximum(gScroll, max);
    SetControlValue(gScroll, value);
}

static void ScrollToLine(short line)
{
    short lineHeight = (**gTE).lineHeight;
    short current = (**gTE).viewRect.top - (**gTE).destRect.top;
    short target = line * lineHeight;

    if (target != current)
        TEScroll(0, current - target, gTE);
    SetControlValue(gScroll, line);
}

static pascal void ScrollAction(ControlHandle control, short part)
{
    short delta, value, page = VisibleLines() - 1;

    if (page < 1)
        page = 1;
    switch (part) {
        case kPartUp:       delta = -1; break;
        case kPartDown:     delta = 1; break;
        case kPartPageUp:   delta = -page; break;
        case kPartPageDown: delta = page; break;
        default: return;
    }
    value = GetControlValue(control) + delta;
    if (value < 0)
        value = 0;
    if (value > GetControlMaximum(control))
        value = GetControlMaximum(control);
    ScrollToLine(value);
}

/* ---------- Entry list ---------- */

/* The list's cells plus its scroll bar. */
static void GetListArea(Rect *r)
{
    *r = (**gList).rView;
    r->right += kScrollWidth - 1;
}

static void InvalListArea(void)
{
    Rect r;

    SetPort(gWindow);
    SetRect(&r, 0, 0, kListWidth, gWindow->portRect.bottom);
    InvalRect(&r);
}

static void RefreshList(void)
{
    Str255 label;
    Cell cell;
    short i;

    if (gList == NULL)      /* the window isn't created yet */
        return;
    LSetDrawingMode(false, gList);
    LDelRow(0, 0, gList);
    if (gShownCount > 0)
        LAddRow(gShownCount, 0, gList);
    for (i = 0; i < gShownCount; i++) {
        MakeShortDate(Days(gShownDays)[i], label);
        SetPt(&cell, 0, i);
        LSetCell((Ptr)&label[1], label[0], cell, gList);
    }
    LSetDrawingMode(true, gList);
    SyncListSelection();
    InvalListArea();
}

/* Highlights the open day in the list, if it is listed. */
static void SyncListSelection(void)
{
    Cell cell;
    short row = FindDay(gShownDays, gShownCount, gDay);

    SetPt(&cell, 0, 0);
    while (LGetSelect(true, &cell, gList)) {
        if (cell.v == row)
            return;
        LSetSelect(false, cell, gList);
    }
    if (row >= 0) {
        SetPt(&cell, 0, row);
        LSetSelect(true, cell, gList);
        LAutoScroll(gList);
    }
}

/* Opens a day picked from the list or the calendar. While searching,
   the first match in it is selected. */
static void OpenDay(long day)
{
    if (day == gDay) {
        SyncListSelection();
        return;
    }
    GoToDay(day);
    if (gDay == day && gSearch[0] != 0)
        SelectMatchFrom(0);
}

static void DoListClick(Point pt, EventRecord *ev)
{
    Cell cell;

    LClick(pt, ev->modifiers, gList);
    SetPt(&cell, 0, 0);
    if (LGetSelect(true, &cell, gList) && cell.v < gShownCount)
        OpenDay(Days(gShownDays)[cell.v]);
    else
        SyncListSelection();
}

/* ---------- Calendar ---------- */

static void ShowMonthOf(long day)
{
    short dayOfMonth;

    DayToDate(day, &gCalYear, &gCalMonth, &dayOfMonth);
}

static long CalendarFirstDay(void)
{
    return DateToDay(gCalYear, gCalMonth, 1);
}

static short CalendarMonthLength(void)
{
    long year = gCalYear;
    short month = gCalMonth + 1;

    if (month > 12) {
        month = 1;
        year++;
    }
    return DateToDay(year, month, 1) - CalendarFirstDay();
}

static void InvalCalendar(void)
{
    Rect r;

    SetPort(gWindow);
    SetRect(&r, 0, kCalTop, kListWidth, kCalBottom);
    InvalRect(&r);
}

/* direction is -1 for the previous-month arrow, 1 for the next-month one. */
static void GetCalendarArrow(short direction, Rect *r)
{
    short left = direction < 0 ? kCalLeft : kListWidth - kCalLeft - 12;

    SetRect(r, left, kCalTop + 5, left + 12, kCalTop + 17);
}

/* Cells are numbered 0..41, row by row, starting on a Sunday. */
static void GetCalendarCell(short index, Rect *r)
{
    short top = kCalGridTop + (index / 7) * kCalCellHeight;
    short left = kCalLeft + (index % 7) * kCalCellWidth;

    SetRect(r, left, top, left + kCalCellWidth, top + kCalCellHeight);
}

static void DrawCalendarArrow(short direction)
{
    Rect r;
    short i, x, y;

    GetCalendarArrow(direction, &r);
    y = (r.top + r.bottom) / 2;
    for (i = 0; i < 6; i++) {
        x = direction < 0 ? r.left + 3 + i : r.right - 4 - i;
        MoveTo(x, y - i);
        LineTo(x, y + i);
    }
}

static void DrawCalendar(void)
{
    static const char kInitials[] = "SMTWTFS";
    Str255 s;
    Rect r;
    long first = CalendarFirstDay(), today = Today();
    short start = Weekday(first), length = CalendarMonthLength(), i;

    TextFont(systemFont);
    TextSize(0);
    PSet(s, kMonths[gCalMonth - 1]);
    PAppend(s, " ");
    PAppendNum(s, gCalYear, 4);
    MoveTo((kListWidth - StringWidth(s)) / 2, kCalTop + 15);
    DrawString(s);
    DrawCalendarArrow(-1);
    DrawCalendarArrow(1);

    TextFont(applFont);
    TextSize(9);
    for (i = 0; i < 7; i++) {
        MoveTo(kCalLeft + i * kCalCellWidth + (kCalCellWidth - CharWidth(kInitials[i])) / 2,
               kCalGridTop - 4);
        DrawChar(kInitials[i]);
    }

    /* Days with a (listed) entry are bold, today is framed and the open
       day is highlighted. */
    for (i = 0; i < length; i++) {
        long day = first + i;

        GetCalendarCell(start + i, &r);
        s[0] = 0;
        PAppendNum(s, i + 1, 1);
        TextFace(FindDay(gShownDays, gShownCount, day) >= 0 ? bold : normal);
        MoveTo(r.left + (kCalCellWidth - StringWidth(s)) / 2, r.top + 11);
        DrawString(s);
        if (day == today)
            FrameRect(&r);
        if (day == gDay)
            InvertRect(&r);
    }
    TextFace(normal);

    MoveTo(0, kCalBottom);
    LineTo(kListWidth, kCalBottom);

    TextFont(applFont);
    TextSize(12);
}

static void ChangeCalendarMonth(short delta)
{
    gCalMonth += delta;
    if (gCalMonth < 1) {
        gCalMonth = 12;
        gCalYear--;
    } else if (gCalMonth > 12) {
        gCalMonth = 1;
        gCalYear++;
    }
    InvalCalendar();
}

/* Highlights r while the mouse is held down inside it, like a button. */
static Boolean TrackRect(const Rect *r)
{
    Boolean inside = true;
    Point pt;

    InvertRect(r);
    while (StillDown()) {
        GetMouse(&pt);
        if (PtInRect(pt, r) != inside) {
            inside = !inside;
            InvertRect(r);
        }
    }
    if (inside)
        InvertRect(r);
    return inside;
}

static void DoCalendarClick(Point pt)
{
    Rect r;
    long first = CalendarFirstDay();
    short start = Weekday(first), length = CalendarMonthLength(), i;

    for (i = -1; i <= 1; i += 2) {
        GetCalendarArrow(i, &r);
        InsetRect(&r, -3, -3);
        if (PtInRect(pt, &r)) {
            if (TrackRect(&r))
                ChangeCalendarMonth(i);
            return;
        }
    }
    for (i = 0; i < length; i++) {
        GetCalendarCell(start + i, &r);
        if (PtInRect(pt, &r)) {
            OpenDay(first + i);
            return;
        }
    }
}

/* ---------- Text font ---------- */

/* The settings file holds the text size (2 bytes) followed by the font
   name as a Pascal string. The name is stored rather than the font number,
   which can differ between Macs. */
static void LoadSettings(void)
{
    Handle buf = NewHandle(0);
    Str255 name, actual;
    unsigned char *p;
    short font, size;
    long len;

    /* Resolve the default application font to its real number, so the
       Font menu can find it by name. */
    GetFontName(applFont, name);
    GetFNum(name, &gTextFont);

    if (buf == NULL)
        return;
    PSet(name, "Diary Settings");
    if (ReadEntriesFile(name, buf, &len) == noErr && len >= 3) {
        p = (unsigned char *)*buf;
        size = (p[0] << 8) | p[1];
        if (len >= 3 + p[2] && size >= 6 && size <= 127) {
            BlockMove(p + 2, name, p[2] + 1);
            GetFNum(name, &font);
            GetFontName(font, actual);
            if (PEqual(name, actual))       /* the font is still installed */
                gTextFont = font;
            gTextSize = size;
        }
    }
    DisposeHandle(buf);
}

static void SaveSettings(void)
{
    unsigned char data[2 + 256];
    Str255 name, fontName;

    if (!gHaveFolder)
        return;
    GetFontName(gTextFont, fontName);
    data[0] = gTextSize >> 8;
    data[1] = gTextSize & 0xFF;
    BlockMove(fontName, data + 2, fontName[0] + 1);
    PSet(name, "Diary Settings");
    WriteEntriesFile(name, 'DIAR', 'pref', (Ptr)data, 3 + fontName[0]);
}

/* Sets the entry text's font and size, and the line metrics that go
   with them. */
static void ApplyTextFont(void)
{
    FontInfo info;
    Rect view;

    SetPort(gWindow);
    TextFont(gTextFont);
    TextSize(gTextSize);
    GetFontInfo(&info);
    TextFont(applFont);     /* back to the list's font */
    TextSize(12);

    (**gTE).txFont = gTextFont;
    (**gTE).txSize = gTextSize;
    (**gTE).fontAscent = info.ascent;
    (**gTE).lineHeight = info.ascent + info.descent + info.leading;
    view = (**gTE).viewRect;
    (**gTE).destRect.top = view.top;
    (**gTE).destRect.bottom = view.bottom;
    TECalText(gTE);
    TESelView(gTE);
    AdjustScrollBar();
    InvalRect(&view);
}

static void SetTextFont(short font, short size)
{
    if (font == gTextFont && size == gTextSize)
        return;
    gTextFont = font;
    gTextSize = size;
    ApplyTextFont();
    SaveSettings();
}

/* Checks the current font and size; sizes the font has as a bitmap
   (which look best) are shown in outline style, as Mac apps do. */
static void MarkFontMenus(void)
{
    MenuHandle menu = GetMenuHandle(kMenuFont);
    Str255 current, name;
    short i, count = CountMItems(menu);

    GetFontName(gTextFont, current);
    for (i = 1; i <= count; i++) {
        GetMenuItemText(menu, i, name);
        CheckItem(menu, i, PEqual(name, current));
    }

    menu = GetMenuHandle(kMenuSize);
    for (i = 0; i < kSizeCount; i++) {
        CheckItem(menu, i + 1, kSizes[i] == gTextSize);
        SetItemStyle(menu, i + 1, RealFont(gTextFont, kSizes[i]) ? outline : normal);
    }
}

/* ---------- Window ---------- */

static void LayoutWindow(void)
{
    Rect port = gWindow->portRect;
    Rect view;

    LSize(kListWidth - kScrollWidth + 1, port.bottom - kListTop, gList);

    MoveControl(gScroll, port.right - kScrollWidth + 1, kHeaderHeight - 1);
    SizeControl(gScroll, kScrollWidth, port.bottom - kHeaderHeight - kScrollWidth + 3);

    view.top = kHeaderHeight + kTextMargin;
    view.left = kPaneLeft + kTextMargin;
    view.bottom = port.bottom - kTextMargin;
    view.right = port.right - kScrollWidth - kTextMargin + 1;

    (**gTE).viewRect = view;
    (**gTE).destRect.left = view.left;
    (**gTE).destRect.right = view.right;
    TECalText(gTE);
    AdjustScrollBar();
}

static void CreateWindow(void)
{
    Rect r, bounds, screen = qd.screenBits.bounds;
    Str255 title;
    Point cellSize;
    short width = 620, height = 380;

    /* Fit on small screens such as the Mac Plus's 512 x 342. */
    if (width > screen.right - 8)
        width = screen.right - 8;
    if (height > screen.bottom - 48)
        height = screen.bottom - 48;
    r.left = (screen.right - width) / 2;
    r.top = 40 + (screen.bottom - 40 - height) / 3;
    r.right = r.left + width;
    r.bottom = r.top + height;

    PSet(title, "Diary");
    gWindow = NewWindow(NULL, &r, title, false, documentProc, (WindowPtr)-1, true, 0);
    SetPort(gWindow);

    SetRect(&r, kPaneLeft + 8, 6, kPaneLeft + 32, 26);
    PSet(title, "<");
    gPrevButton = NewControl(gWindow, &r, title, true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, kPaneLeft + 38, 6, kPaneLeft + 96, 26);
    PSet(title, "Today");
    gTodayButton = NewControl(gWindow, &r, title, true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, kPaneLeft + 102, 6, kPaneLeft + 126, 26);
    PSet(title, ">");
    gNextButton = NewControl(gWindow, &r, title, true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 0, 0, kScrollWidth, 100);
    title[0] = 0;
    gScroll = NewControl(gWindow, &r, title, true, 0, 0, 0, scrollBarProc, 0);

    /* The list and the text both use Geneva 12; DrawHeader switches to
       the system font only while it draws. */
    TextFont(applFont);
    TextSize(12);

    SetRect(&r, 0, kListTop, kListWidth - kScrollWidth + 1, kListTop + 100);
    SetRect(&bounds, 0, 0, 1, 0);
    SetPt(&cellSize, 0, 0);
    gList = LNew(&r, &bounds, cellSize, 0, gWindow, true, false, false, true);
    (**gList).selFlags = lOnlyOne;

    SetRect(&r, 0, 0, 100, 100);
    gTE = TENew(&r, &r);
    TEAutoView(true, gTE);

    LayoutWindow();
    ApplyTextFont();
    RefreshList();
    ShowWindow(gWindow);
}

static void DrawHeader(void)
{
    Str255 s;
    Rect port = gWindow->portRect;

    TextFont(systemFont);
    TextSize(0);

    PSet(s, gSearch[0] != 0 ? "Matches (" : "Entries (");
    PAppendNum(s, gShownCount, 1);
    PAppend(s, ")");
    MoveTo(8, 21);
    DrawString(s);

    MakeLongDate(gDay, s);
    TextFace(gDay == Today() ? bold : normal);
    MoveTo(kPaneLeft + 138, 21);
    DrawString(s);
    TextFace(normal);

    MoveTo(0, kHeaderHeight - 1);
    LineTo(port.right, kHeaderHeight - 1);
    MoveTo(kListWidth, 0);
    LineTo(kListWidth, port.bottom);

    TextFont(applFont);
    TextSize(12);
}

/* Draws just the size box, without the scroll bar outlines DrawGrowIcon
   would draw across the list and the text. */
static void DrawGrowBox(void)
{
    Rect r = gWindow->portRect;
    RgnHandle saved = NewRgn();

    r.left = r.right - kScrollWidth + 1;
    r.top = r.bottom - kScrollWidth + 1;
    GetClip(saved);
    ClipRect(&r);
    DrawGrowIcon(gWindow);
    SetClip(saved);
    DisposeRgn(saved);
}

static void DoUpdate(WindowPtr w)
{
    SetPort(w);
    BeginUpdate(w);
    EraseRect(&w->portRect);
    DrawHeader();
    DrawCalendar();
    DrawControls(w);
    DrawGrowBox();
    LUpdate(w->visRgn, gList);
    TEUpdate(&w->portRect, gTE);
    EndUpdate(w);
}

static void DoActivate(Boolean active)
{
    SetPort(gWindow);
    if (active) {
        TEActivate(gTE);
        HiliteControl(gScroll, 0);
    } else {
        TEDeactivate(gTE);
        HiliteControl(gScroll, 255);
    }
    LActivate(active, gList);
    DrawGrowBox();
}

static void DoGrow(Point where)
{
    Rect limits;
    long size;

    SetRect(&limits, 480, kListTop + 60, 32767, 32767);
    size = GrowWindow(gWindow, where, &limits);
    if (size == 0)
        return;
    SizeWindow(gWindow, LoWord(size), HiWord(size), true);
    SetPort(gWindow);
    InvalRect(&gWindow->portRect);
    LayoutWindow();
}

static void DoContentClick(EventRecord *ev)
{
    Point pt = ev->where;
    ControlHandle control;
    Rect listArea;
    short part;

    SetPort(gWindow);
    GlobalToLocal(&pt);

    if (pt.h < kListWidth && pt.v >= kCalTop && pt.v < kCalBottom) {
        DoCalendarClick(pt);
        return;
    }
    GetListArea(&listArea);
    if (PtInRect(pt, &listArea)) {
        DoListClick(pt, ev);
        return;
    }

    part = FindControl(pt, gWindow, &control);
    if (control == gScroll) {
        if (part == kPartThumb) {
            TrackControl(control, pt, NULL);
            ScrollToLine(GetControlValue(control));
        } else if (part != 0) {
            TrackControl(control, pt, (ControlActionUPP)&ScrollAction);
        }
    } else if (control != NULL && part == kPartButton) {
        if (TrackControl(control, pt, NULL)) {
            if (control == gPrevButton)
                GoToDay(gDay - 1);
            else if (control == gNextButton)
                GoToDay(gDay + 1);
            else if (control == gTodayButton)
                GoToDay(Today());
        }
    } else if (PtInRect(pt, &(**gTE).viewRect)) {
        TEClick(pt, (ev->modifiers & shiftKey) != 0, gTE);
        AdjustScrollBar();
    }
}

static void AdjustCursor(void)
{
    Point pt;
    GrafPtr savePort;

    if (gWindow == NULL || FrontWindow() != gWindow)
        return;
    GetPort(&savePort);
    SetPort(gWindow);
    GetMouse(&pt);
    if (PtInRect(pt, &(**gTE).viewRect))
        SetCursor(*GetCursor(iBeamCursor));
    else
        SetCursor(&qd.arrow);
    SetPort(savePort);
}

/* ---------- Editing ---------- */

/* Retro68's libraries lack the TEToScrap/TEFromScrap glue, so move
   TextEdit's private scrap to and from the desk scrap ourselves. */
static void CopyTEScrapToDesk(void)
{
    Handle teScrap = LMGetTEScrpHandle();

    ZeroScrap();
    HLock(teScrap);
    PutScrap(LMGetTEScrpLength(), 'TEXT', *teScrap);
    HUnlock(teScrap);
}

static void CopyDeskScrapToTE(void)
{
    long offset;
    long length = GetScrap(LMGetTEScrpHandle(), 'TEXT', &offset);

    LMSetTEScrpLength(length > 0 ? length : 0);
}

static long SelectionLength(void)
{
    return (**gTE).selEnd - (**gTE).selStart;
}

static void AfterEdit(void)
{
    gDirty = true;
    TESelView(gTE);
    AdjustScrollBar();
}

static void DoKey(char c)
{
    Boolean isNavigation = (c >= 0x1C && c <= 0x1F);
    Boolean isDelete = (c == 0x08);

    if (!isNavigation && !isDelete
            && (**gTE).teLength - SelectionLength() >= kMaxTextLength) {
        SysBeep(10);
        return;
    }
    TEKey(c, gTE);
    if (isNavigation) {
        TESelView(gTE);
        AdjustScrollBar();
    } else {
        AfterEdit();
    }
}

static void DoPaste(void)
{
    long offset;
    long scrapLength = GetScrap(NULL, 'TEXT', &offset);

    if (scrapLength <= 0)
        return;
    if ((**gTE).teLength - SelectionLength() + scrapLength > kMaxTextLength) {
        ShowError("This entry is too long to paste that much text.", noErr);
        return;
    }
    CopyDeskScrapToTE();
    TEPaste(gTE);
    AfterEdit();
}

/* ---------- Menus ---------- */

static void SetItemEnabled(MenuHandle menu, short item, Boolean enabled)
{
    if (enabled)
        EnableItem(menu, item);
    else
        DisableItem(menu, item);
}

static void AdjustMenus(void)
{
    WindowPtr front = FrontWindow();
    Boolean isDA = front != NULL && GetWindowKind(front) < 0;
    Boolean isOurs = front == gWindow;
    Boolean hasSelection = isOurs && SelectionLength() > 0;
    long offset;
    MenuHandle menu;

    menu = GetMenuHandle(kMenuFile);
    SetItemEnabled(menu, kItemSave, isOurs && gDirty);

    menu = GetMenuHandle(kMenuEdit);
    SetItemEnabled(menu, kItemUndo, isDA);
    SetItemEnabled(menu, kItemCut, isDA || hasSelection);
    SetItemEnabled(menu, kItemCopy, isDA || hasSelection);
    SetItemEnabled(menu, kItemPaste, isDA || (isOurs && GetScrap(NULL, 'TEXT', &offset) > 0));
    SetItemEnabled(menu, kItemClear, isDA || hasSelection);
    SetItemEnabled(menu, kItemSelectAll, isOurs);
    SetItemEnabled(menu, kItemFind, isOurs);
    SetItemEnabled(menu, kItemFindAgain, isOurs && gSearch[0] != 0);
    SetItemEnabled(menu, kItemShowAll, isOurs && gSearch[0] != 0);

    MarkFontMenus();
}

static void DoQuit(void)
{
    if (SaveEntry())
        gQuit = true;
}

static void DoMenuCommand(long command)
{
    short menuID = HiWord(command);
    short item = LoWord(command);
    short font;
    Str255 name;

    switch (menuID) {
        case kMenuApple:
            if (item == kItemAbout) {
                ShowAbout();
            } else {
                GetMenuItemText(GetMenuHandle(kMenuApple), item, name);
                OpenDeskAcc(name);
            }
            break;

        case kMenuFile:
            if (item == kItemSave)
                SaveEntry();
            else if (item == kItemQuit)
                DoQuit();
            break;

        case kMenuEdit:
            if (SystemEdit(item - 1))
                break;
            switch (item) {
                case kItemCut:
                    TECut(gTE);
                    CopyTEScrapToDesk();
                    AfterEdit();
                    break;
                case kItemCopy:
                    TECopy(gTE);
                    CopyTEScrapToDesk();
                    break;
                case kItemPaste:
                    DoPaste();
                    break;
                case kItemClear:
                    TEDelete(gTE);
                    AfterEdit();
                    break;
                case kItemSelectAll:
                    TESetSelect(0, 32767, gTE);
                    break;
                case kItemFind:
                    DoFind();
                    break;
                case kItemFindAgain:
                    DoFindAgain();
                    break;
                case kItemShowAll:
                    ShowAllEntries();
                    break;
            }
            break;

        case kMenuGo:
            if (item == kItemPrevDay)
                GoToDay(gDay - 1);
            else if (item == kItemNextDay)
                GoToDay(gDay + 1);
            else if (item == kItemToday)
                GoToDay(Today());
            break;

        case kMenuFont:
            GetMenuItemText(GetMenuHandle(kMenuFont), item, name);
            GetFNum(name, &font);
            SetTextFont(font, gTextSize);
            break;

        case kMenuSize:
            if (item >= 1 && item <= kSizeCount)
                SetTextFont(gTextFont, kSizes[item - 1]);
            break;
    }
    HiliteMenu(0);
}

/* ---------- Events ---------- */

static void DoMouseDown(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);

    switch (part) {
        case inMenuBar:
            AdjustMenus();
            DoMenuCommand(MenuSelect(ev->where));
            break;
        case inSysWindow:
            SystemClick(ev, w);
            break;
        case inDrag:
            DragWindow(w, ev->where, &qd.screenBits.bounds);
            break;
        case inGrow:
            if (w == gWindow)
                DoGrow(ev->where);
            break;
        case inGoAway:
            if (w == gWindow && TrackGoAway(w, ev->where))
                DoQuit();
            break;
        case inContent:
            if (w != FrontWindow())
                SelectWindow(w);
            else if (w == gWindow)
                DoContentClick(ev);
            break;
    }
}

static void DoEvent(EventRecord *ev)
{
    char c;

    switch (ev->what) {
        case mouseDown:
            DoMouseDown(ev);
            break;
        case keyDown:
        case autoKey:
            c = ev->message & charCodeMask;
            if (ev->modifiers & cmdKey) {
                if (ev->what == keyDown) {
                    AdjustMenus();
                    DoMenuCommand(MenuKey(c));
                }
            } else if (FrontWindow() == gWindow) {
                DoKey(c);
            }
            break;
        case updateEvt:
            if ((WindowPtr)ev->message == gWindow)
                DoUpdate(gWindow);
            break;
        case activateEvt:
            if ((WindowPtr)ev->message == gWindow)
                DoActivate((ev->modifiers & activeFlag) != 0);
            break;
        case osEvt:
            /* Suspend/resume: message bit 0 is set on resume. */
            if (((ev->message >> 24) & 0xFF) == 0x01 && FrontWindow() == gWindow)
                DoActivate((ev->message & 1) != 0);
            break;
    }
}

static void SetUpMenus(void)
{
    Handle menuBar = GetNewMBar(128);

    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(kMenuApple), 'DRVR');
    AppendResMenu(GetMenuHandle(kMenuFont), 'FONT');
    DrawMenuBar();
}

int main(void)
{
    EventRecord ev;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    MaxApplZone();

    gAllDays = NewHandle(0);
    gShownDays = NewHandle(0);

    SetUpMenus();
    FindEntriesFolder();
    LoadSettings();
    ScanEntries();
    ShowAllEntries();
    gDay = Today();
    CreateWindow();
    LoadEntry();

    while (!gQuit) {
        if (WaitNextEvent(everyEvent, &ev, LMGetCaretTime(), NULL))
            DoEvent(&ev);
        if (gWindow == FrontWindow())
            TEIdle(gTE);
        AdjustCursor();
    }
    return 0;
}
