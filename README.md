# Diary

A simple one-page-per-day journal for the classic Mac OS on 68k Macs
(System 7; should also run on System 6.0.8 with HFS).
There's also a [version for AmigaOS 3.2](https://github.com/retrolemmy/Diary-Amiga)
(the two can read each other's entries), and ones for
[Mac OS X 10.4 and 10.5](https://github.com/retrolemmy/Diary-MacOSX) and
[Windows 98](https://github.com/retrolemmy/Diary-Win98).

- One window: a month calendar and a list of all entries (newest first) on
  the left, the selected day on the right with `<`, `Today` and `>` buttons
- In the calendar, days with an entry are **bold**, today is framed and the
  open day is highlighted; the arrows switch months. Click a day (or a list
  entry) to open it
- **Find** (⌘F) searches every entry (ignoring case) and narrows the list to
  the matches; **Find Again** (⌘G) jumps to the next match, across entries;
  **Show All Entries** clears the search
- **Go** menu: Previous Day (⌘[), Next Day (⌘]), Today (⌘T)
- **Font** and **Size** menus set the entry text's font and size (9–24 point);
  the choice is remembered in a "Diary Settings" file in the entries folder
- The current entry is saved automatically when you switch days or quit; ⌘S saves at any time
- Entries are plain `TEXT` files named `YYYY-MM-DD` in a **Diary Entries** folder
  next to the application, so they open in SimpleText/TeachText too
- Entries written by the [Amiga version](https://github.com/retrolemmy/Diary-Amiga)
  can be copied in and read as they are: their line ends and accented
  letters (ISO Latin-1) are converted, and they get the Mac file type of
  a text document
- Clearing an entry completely deletes its file
- **File → Diary Server...** keeps the entries on a
  [Diary Server](https://github.com/retrolemmy/Diary-Server) instead, so all
  your Macs and Amigas share them. Search then runs on the server, and if an
  entry was changed on another computer since you opened it, Diary asks
  before saving over it. It needs MacTCP or Open Transport, and the server's
  IP address (Diary can't look up server names); without a server set,
  Diary never touches the network
- Up to 32,000 characters per day (the TextEdit limit)

## Building

You need the [Retro68](https://github.com/autc04/Retro68) cross-compiler:

```sh
git clone --recursive https://github.com/autc04/Retro68.git
mkdir Retro68-build && cd Retro68-build
../Retro68/build-toolchain.bash --no-ppc --no-carbon
```

Then build Diary:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$HOME/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake --build build
```

The finished app is in `build/dist/`:

- `Diary.bin`: MacBinary, for transferring to a real Mac (unpack with StuffIt Expander)
- `Diary.dsk`: an 800K disk image for emulators (Mini vMac, Basilisk II) or a floppy

Use these rather than the files directly in `build/`: Retro68 can't set the
Finder's "has bundle" flag, so a post-build step (`tools/set_bundle_bit.py`)
sets it to make the custom icon show up.

For testing in [Basilisk II](https://basilisk.cebix.net), `cmake --build build
--target emulator` unpacks the app into the folder Basilisk II shares with the
Mac (`~/BasiliskII/Shared` by default; set `BASILISK_SHARED` to change it),
with its type, creator and icon intact.

## Icon

The icons (`icons.r`) are drawn by `tools/make_icons.py`. Edit the script
and regenerate with:

```sh
python3 tools/make_icons.py > icons.r
```

## License

No rights reserved: Diary is dedicated to the public domain under
[CC0 1.0](LICENSE). Use it for anything, no attribution needed.
