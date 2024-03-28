# agon-mos

Part of the official firmware for the [Agon Console8](https://www.heber.co.uk/agon-console8)

This firmware is intended for use on any Agon Light compatible computer.  As well as the Agon Console8, it has been tested on the Olimex Agon Light 2.

This version of agon-mos may differ from the [official Quark firmware](https://github.com/breakintoprogram/agon-mos) and contain some extensions, but software written for the official Quark firmware should be fully compatible with this version.

### What is the Agon

Agon is a modern, fully open-source, 8-bit microcomputer and microcontroller in one small, low-cost board. As a computer, it is a standalone device that requires no host PC: it puts out its own video (VGA), audio (2 identical mono channels), accepts a PS/2 keyboard and has its own mass-storage in the form of a micro-SD card.

https://www.thebyteattic.com/p/agon.html

The Agon Console8 is a fully compatible variant of the Agon Light from the same designer, with the addition of a built-in PS/2 mouse port and two Atari-style DB-9 joystick ports.

### What is a MOS

The MOS is a command line Machine Operating System, similar to CP/M or DOS, that provides a human interface to the Agon file system.  It runs on the eZ80 CPU of the Agon.

It also provides an API for file I/O and other common operations for BBC Basic for Z80 and other third-party applications.

### Loading BBC Basic for Z80

1. Download bbcbasic.bin from [agon-bbc-basic releases](https://github.com/breakintoprogram/agon-bbc-basic/releases)
2. Copy it to the root directory of the Agon SD card
3. Insert the SD card into the AGON and reset/boot it
4. Check the file is on the SD card with a `CAT` or `.` command
5. Type the following commands into MOS:
	- `LOAD bbcbasic.bin`
	- `RUN`
6. You should then be greeted with the BBC Basic for Z80 prompt

### Etiquette

Reporting issues and pull requests are welcome.

A Contributing guide will be added in due course.

### Build

The MOS is built using the Zilog Developer Studio II (ZDS II - eZ80Acclaim! version 5.3.5) tools.

You can download the ZDS II tools for free via the following link. The software contains an IDE, Debugger, C Compiler and eZ80 Assembler.

- [Zilog ZDS II Tools version 5.3.5](https://zilog.com/index.php?option=com_zcm&task=view&soft_id=54&Itemid=74)

ZDS II is a Windows application.  Development of the MOS for the Console8 has been conducted on a MacBook Pro with an M1Max CPU running Windows 10 for ARM in a Parallels VM.  Other developers have used differing combinations of Windows, Linux and OSX, using VMs or Wine to run the ZDS II tools.

#### Creating a .bin file

The ZDS II tooling will compile the firmware and produce a `MOS.hex` file.  Unless you are programming the eZ80 on your Agon directly using ZDS II (see below), you will need to convert this to a `.bin` file to flash to the eZ80.

To convert the `.hex` file to a `.bin` file, use the [Hex2Bin utility](https://sourceforge.net/projects/hex2bin/).

### Testing the MOS

#### Using the Agon Emulator

MOS can be tested out without the need to reprogram your Agon Console8 or Agon Light by using the [Fab Agon Emulator](https://github.com/tomm/fab-agon-emulator).

To test using the emulator, create a new `MOS.bin` file and place that into the emulator directory and run the emulator.  The emulator will automatically load the `MOS.bin` file and run it.

It should be noted that the emulator is not 100% accurate, so some features may not work as expected, but it is a very close simulation.

Unless you are using the ZDS II tools to program the eZ80 directly, it is recommended that you test your MOS on the emulator before testing on real hardware.

#### Flashing your Agon Console8 or Agon Light

The MOS can also be flashed on your device using the [agon-flash Agon MOS firmware upgrade utility](https://github.com/envenomator/agon-flash).  This is a command line utility that runs on the Agon itself, and can flash MOS to the eZ80 from a file stored on your SD card.

In the event that you flash a MOS that does not work, you will need to revert your Agon back to a known working version.  With the exception of the Agon Console8, this can be done using the [agon-vdpflash utility](https://github.com/envenomator/agon-vdpflash).  

Recovery for an Agon Console8 requires the use of a second external ESP32-based machine.  Guidance on this, and a tool for recovery, can be found in the [Agon Console8 Recovery Tool](https://github.com/envenomator/console8-recover) repository.

It is recommended when using the agon-flash utility that you use a filename other than `MOS.bin` for your new experimental MOS version, and keep a known working version of the `MOS.bin` file in the root of your SD card, as this is the file that the agon-vdpflash utility uses.

#### Programming the eZ80 directly

To program the eZ80 directly using ZDS II you will need a Zilog Smart Cable to connect to the ZDI connector on the board.  These are available from online stockists such as Mouser or RS Components.  Please note however that development of the MOS for the Console8 has *not* been conducted using a Zilog USB Smart Cable.

There are three compatible cables with the following part numbers:

- `ZUSBSC00100ZACG`: USB Smart Cable (discontinued)
- `ZUSBASC0200ZACG`: USB Smart Cable (in stock - this requires ZDS II version 5.3.5)
- `ZENETSC0100ZACG`: Ethernet Smart Cable (in stock)

Important! Make sure you get the exact model of cable; there are variants for the Zilog Encore CPU that have similar part numbers, but are not compatible with the Acclaim! eZ80 CPU.

Any custom settings for Agon development is contained within the project files, so no further configuration will need to be done.

Other options for programming the Agon are available, and the community will be happy to advise on these.

### Documentation

The Agon Platform documentation can now be found on the [Community Documentation](https://agonconsole8.github.io/agon-docs/) site.  This site provides extensive documentation about the Agon platform firmware, covering both Quark and Console8 firmware releases.

### Community

There is a [vibrant and active community on Discord](https://discord.gg/7Ruseg98T9), where you can get help and advice on developing for the Agon.

There is also the [Agon Programmers Group on Facebook](https://www.facebook.com/groups/667325088311886).

### Licenses

This code is released under an MIT license, with the following exceptions:

* FatFS: The license for the [FAT filing system by ChaN](http://elm-chan.org/fsw/ff/00index_e.html) can be found here [src_fatfs/LICENSE](src_fatfs/LICENSE) along with the accompanying code.

### Additional Links

- [Zilog eZ80 User Manual](http://www.zilog.com/docs/um0077.pdf)
- [ZiLOG Developer Studio II User Manual](http://www.zilog.com/docs/devtools/um0144.pdf)
- [FatFS - Generic File System](http://elm-chan.org/fsw/ff/00index_e.html)
- [AVRC Tutorials - Initialising an SD Card](http://www.rjhcoding.com/avrc-sd-interface-1.php)
