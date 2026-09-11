Command Line Utilities
======================

CLU tools and GUI applications are simply Real Bodies that contain one or more special records:

| Record | Type         | Name                      | Purpose |
----------------------------------------------------------------
| 9      | RT_PROG      | Executable Program Record | Contains the actual machine code (native binary for the CPU). Subtype indicates CPU architecture. |
| 7 / 8  | Function / Execution Fusen | Menu entries, launch parameters, icons, etc. |
| 1      | TAD main     | Documentation, help text, or resources in TAD format |
| 0      | Link records | Virtual Bodies pointing to other objects |

```
#define RT_PROG      9   /* 実行プログラムレコード – Executable Program Record */
#define OBJ_EXEC     0x8000  /* 実行可能ファイル – executable Real Body flag */
```

Registration mechanism
----------------------

* Applications are registered with apreg.
* The system keeps a special Real Body called $$PROGRAM.BOX on each volume.
* This box holds links (Virtual Bodies) to all registered executable Real Bodies.
* When you double-click a Virtual Body or type a command name in the CLI,
  the system looks up the corresponding Real Body that has a type-9 (program)
  record and runs the code inside it.

ELF-rumtime loader vs TAD-Runtime
---------------------------------

Aspect          | ELF / PE (Unix/Windows)    | BTRON / Cho-Kanji / B-System
------------------------------------------------------------------------------------------
Container       | Standalone binary file     | Real Body (multi-record object)
Code storage    | .text / .code section      | Record type 9 (RT_PROG)
Metadata / resources | Sections, resources   | Other records (TAD, fusen, etc.)
Linking / icons | External or embedded       | Virtual Bodies + fusen records
Launch          | Loader reads ELF header    | System finds type-9 record via registration
Format of code  | Machine code + ELF headers | Raw native machine code (no ELF)

The closest modern analogy is “a multi-stream (BeFS, NTFS) object whose one stream is raw machine code,”
managed entirely by the Real Body / Virtual Body system rather than by a traditional executable format.

Device Commands
----------------------------------------------------------------

```
att   – Attach device (mount)
  att [-r][-k][-S][-s] <device> <attach-name>

det   – Detach device (unmount)
  det [-e][-k][-s] <device>

eject – Eject media
  eject [device]
```

Bodies
----------------------------------------------------------------

```
cd    – Change / show working Real Body
  cd [<path>]
  (no argument shows current)

ls    – List files (Real Bodies)
  ls [-f][-F][-l|-t] [<path>...]

  -l columns : ATYPE ATR NREC NREF SIZE MTIME NAME
  -t columns : CTIME ATIME MTIME NAME
  -F         : mark files on other/unmounted volumes with *
  -f         : (additional flag)

fs    – Show record structure of a file or subdirectory tree
  fs [-l][-r] [<path>...]

  Options:
    -l    detailed listing with FID and attributes
    -r    recursive listing of subdirectory tree (drawers / containers)

  Normal form:
    NO: TYPE STYPE : SIZE          (data record)
    NO: 0    STYPE : NAME          (link record)
    (Columned one-liners; indentation indicates subdirectory depth)

  -l form (link records):
    NO: 0 STYPE : FID [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME

stat  – Retrieve detailed file metadata and record table by FID or name
  stat <path|FID>
  (Aliases: finfo, info)
  Accepts either a filename ("BOOK.md", "errnoh"), a numeric FID ("781", "1"),
  a prefixed FID ("#781", "FID:781"), or volume-prefixed FID ("/CHOKANJI#781").
  Displays File name, FID, backing Volume, Header & Data blocks, Total Size,
  Format/Kind, Access flags, Timestamps (Modify, Access, Create), and all
  contained Record descriptors with decoded link/stream targets.
```

File Records
----------------------------------------------------------------

```
mkf   – Create new file (Real Body)
  mkf [-t#] <path>...

cp    – Copy file(s)
  cp [-b][-r][-v] <src>... <dest>

rcp   – Copy and rename
  rcp [-b][-r] <src> <dest>
  (dest name "=" means parent of destination)

ln    – Create link (Virtual Body)
  ln [-p#][-a#.#.#.#.#] <src>... <dest>
  Example: -a0x8000.1.2.3.4

rm    – Delete file / link
  rm [-i][-r|-u][-v][-f|-F] <path>...
  (without -r, error if children exist)
  (if other links remain, only the link is removed)

empf  – Empty file contents (delete all records)
  empf <path>...

ren   – Rename
  ren <path> <new-name>

apd   – Append / insert / delete records
  apd [-p#|-d#.#][-r#.#][-t#.#] <path1> [<path2>]

  -d#.#   delete starting record #, count #
  -p#     insert position
  -r#.#   copy range from path2
  -t#.#   force type / subtype after copy

  Record type reference:
    0  Link
    1  TAD main
    2  TAD note
    3  TAD auxiliary
    5  Setting fusen
    6  Designation fusen
    7  Function fusen
    8  Binary
    9  Program
   10  Databox
   11  Font
   12  Dictionary
   15  System data
  16-31 Application specific
```

File Attributes
----------------------------------------------------------------

```
chdef – Show / set default access mode
  chdef [-o#] [-g#] [-p#] [-n#]
  (bit fields for R/W/E access levels 0-F)

chmod – Change file access mode / attributes
  chmod [-o#] [-g#] [-p#] [-n#] [-a#] <path>...
  -a1  set write-protect
  -a2  clear write-protect
  -a3  set delete-protect
  -a4  clear delete-protect

chtime – Set file timestamps
  chtime [-l#.#.#.#.#.#] [-a#.#.#.#.#.#] [-m#.#.#.#.#.#] <path>...

touch – Update timestamps (or create)
  touch <path>...
```

Printing
--------

```
tp    – Display file contents
  tp [-p#][-r#][-e][-a|-x|-b] <path>
  -a  ASCII
  -x  hexadecimal
  -b  binary
  -p# start position
  -r# number of records
  -e  (extra)

df    – Disk free / volume status
  df

date  – Show / set system date-time
  date [<year> <month> <day> <hour> <min> <sec>]

sync  – Flush all filesystem caches
  sync
```

Application Registration
------------------------

```
apreg – Register application
  apreg [-t] <app-path>

apdel – Unregister application
  apdel [-f] <app-path>
```

CLU Session Start
-----------------

```
================================================================
B-System 3.20 Console
Sakamura BTRON3 compatible CLI
================================================================

Type '?' for command summary.

[/SYS]% ?
att det eject
cd ls fs
mkf cp rcp ln rm empf ren apd
chdef chmod chtime touch
tp df date sync
apreg apdel
exit
```

Volume & device
---------------

```
[/SYS]% df
PATH    DEV   TOTAL  FREE  USED  UNIT  MAXFILE  NAME
/SYS    hdc   2048M  1736M  15%  1024   65535   hdc
/fda    fda   1.4M   1.2M   14%   512     112   fda

[/SYS]% att fda /fda
Device fda attached as /fda

[/SYS]% det fda
Device fda detached

[/SYS]% sync
(all caches flushed)
```

Navigation & listing
--------------------

```
[/SYS]% ls
BTRON Spec Book 1
Cho-Kanji Guide
Kernel Internals
Graphics & Display
Applications & HMI
TRASH

[/SYS]% ls -l
ATYPE ATR NREC NREF SIZE  MTIME            NAME
0001  --- 5    0    18420 2026-09-08 14:22 BTRON Spec Book 1
0001  --- 4    0    15680 2026-09-08 14:25 Cho-Kanji Guide
0001  --- 6    0    22100 2026-09-08 13:50 Kernel Internals
0001  --- 4    0    14320 2026-09-08 15:10 Graphics & Display
0001  --- 3    0    12840 2026-09-08 15:30 Applications & HMI
0000  --- 1    0      512 2026-09-08 12:00 TRASH

[/SYS]% ls -t
CTIME              ATIME              MTIME              NAME
2026-09-08 14:20   2026-09-08 14:22   2026-09-08 14:22   BTRON Spec Book 1
2026-09-08 14:24   2026-09-08 14:25   2026-09-08 14:25   Cho-Kanji Guide

[/SYS]% cd "BTRON Spec Book 1"
[Book1]% ls -l
ATYPE ATR NREC NREF SIZE  MTIME            NAME
0001  --- 5    0    18420 2026-09-08 14:22 (self)
0001  --- 2    1     8320 2026-09-08 14:18 Chapter 1
0001  --- 2    1     6540 2026-09-08 14:19 Chapter 2
0001  --- 3    1     9210 2026-09-08 14:20 Chapter 3
0005  --- 1    2     2100 2026-09-08 14:21 Figure 12
0006  --- 1    1     1892 2026-09-08 15:01 RootCA.der
```

Record introspection (fs)
-------------------------

```
[Book1]% fs
NO: TYPE STYPE : SIZE / NAME
0:  0    0000  : Chapter 1
1:  0    0000  : Chapter 2
2:  0    0000  : Chapter 3
3:  0    0000  : Figure 12
4:  0    0000  : RootCA.der
5:  1    0000  : 18420          (TAD main)

[Book1]% fs -l
NO: 0 STYPE : FID [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME
0:  0 0000  : 43  [0000 0000 0000 0000 0000] : Chapter 1
1:  0 0000  : 44  [0000 0000 0000 0000 0000] : Chapter 2
2:  0 0000  : 45  [0000 0000 0000 0000 0000] : Chapter 3
3:  0 0000  : 50  [0000 0000 0000 0000 0000] : Figure 12
4:  0 0000  : 60  [0000 0000 0000 0000 0000] : RootCA.der
5:  1 0000  :     (data record)

[Book1]% fs "Chapter 1"
NO: TYPE STYPE : SIZE / NAME
0:  0    0000  : Figure 12
1:  1    0000  : 8288           (TAD main)

[Book1]% fs -l "Chapter 1"
NO: 0 STYPE : FID [ATR1 .. ATR5] : NAME
0:  0 0000  : 50  [0000 0000 0000 0000 0000] : Figure 12
1:  1 0000  :     (TAD data)

[Book1]% fs -r
NO: TYPE STYPE : SIZE / NAME
0:  0    0000  : Chapter 1
0:  0    0000  :   Figure 12
1:  0    0000  : Chapter 2
2:  0    0000  : Chapter 3
3:  0    0000  : Figure 12
4:  0    0000  : RootCA.der
5:  1    0000  : 18420          (TAD main)
```

Content display (tp)
--------------------

```
[Book1]% tp "Chapter 1"
Chapter 1 – Introduction
```

This is the converted content from the original HTML.
It contains text and embedded objects.

```
[Book1]% tp -x "Chapter 1"
0000: FF E0 00 18 00 00 ...   (TS_INFO)
0018: FF E1 00 30 ...         (TS_TEXT)

[Book1]% tp -a "Chapter 1"
(ASCII dump of TAD record)
```

File & link operations
----------------------

```
[Book1]% mkf -t1 "New Chapter"
[Book1]% ls -l "New Chapter"
ATYPE ATR NREC NREF SIZE MTIME NAME
0001  --- 1    0      0  ...   New Chapter

[Book1]% ln "Chapter 1" "Alias of Chapter 1"
[Book1]% rcp "Chapter 2" "Chapter 2 Copy"
[Book1]% ren "Chapter 2 Copy" "Section 2"
[Book1]% cp "Figure 12" /SYS/TRASH/
[Book1]% rm "Alias of Chapter 1"
[Book1]% empf "New Chapter"
[Book1]% rm -r "New Chapter"
```

Record surgery (apd)
--------------------

```
[Book1]% apd -t1 "Chapter 1" "Section 2"
(appended TAD record from Section 2 into Chapter 1)

[Book1]% apd -d1.1 "Chapter 1"
(deleted record 1)

[Book1]% fs "Chapter 1"
NO: TYPE STYPE : SIZE / NAME
0:  0    0000  : Figure 12
1:  1    0000  : 9210
```

Attributes & time
-----------------

```
[Book1]% chmod -a1 "Chapter 1"
(write-protect set)

[Book1]% chatr +R "Chapter 1"
[Book1]% touch "Chapter 1"
[Book1]% chtime -m2026.9.8.15.0.0 "Chapter 1"

[Book1]% chdef
(current default access mode shown)
```

System
------

```
[/SYS]% date
2026-09-08 21:05:00
```



[/SYS]% date 2026 9 8 21 10 0
(time set)

[/SYS]% sync
[/SYS]% exit
Console closed.
