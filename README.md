# Diary

A simple one-page-per-day journal for the classic Mac OS on 68k Macs
(System 7; should also run on System 6.0.8 with HFS).

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
- Clearing an entry completely deletes its file
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

## Icon

The icons (`icons.r`) are drawn by `tools/make_icons.py`. Edit the script
and regenerate with:

```sh
python3 tools/make_icons.py > icons.r
```

## License

MIT; see [LICENSE](LICENSE).
