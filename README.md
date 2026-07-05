# SC-55mkII boot animation stuff

Offset: 0x070000

Size:   0x2580 bytes

Frames: 150

Format: 64 bytes per frame

0x070000–0x0712BF  75 frames  SC-55mkII boot animation

0x0712C0–0x07257F  75 frames  SC-155mkII boot animation

Each frame is the raw 64-byte LCD/SysEx display payload, without the SysEx header/checksum.

The frame is stored as four 16-row strips:

bytes  0–15   columns  1–5

bytes 16–31   columns  6–10

bytes 32–47   columns 11–15

bytes 48–63   column 16 plus four off-screen/clipped bits

Each byte contains a 5-bit horizontal slice for one display row.

Only the first 16 columns are visible; bits that would map beyond column 16 are ignored/clipped by the display.

extract will extract to P1 PBM/ASCII

inject will inject P1 PBM/ASCII into a SC55mkII ROM

## inject
inject -55  input.rom output.rom frames_dir

inject -155 input.rom output.rom frames_dir

inject -55  --loop input.rom output.rom frames_dir

-55 will inject input data into the SC-55 region of the ROM (0x070000)

-155 will inject input data into the SC-155 region of the ROM (0x0712C0)

Input is a maximum of 75 frames. If more are supplied, only the first 75 will be used. If less than 75, the frames will be inserted and the remaining frames in ROM will be blanked out.

If --loop is specified, the frames supplied will be looped as many times as they can fully, and the remaining frames blanked; i.e. if 6 input frames are provided, they will be looped 12 times (72 frames), and the remaining 3 frames in ROM will be blanked

Note that the inject tool has NOT BEEN TESTED.

As far as I'm aware, the SC-155 animation has not been seen before, as Roland never realeased a mkII SC-155. This does give me hope however that a SC-55mkII board could be retrofitted into an original SC-155, and a modification discovered to put the board into SC-155 mode, as the code is already there.
