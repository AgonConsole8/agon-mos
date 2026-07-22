# agon-mos

This is a port in progress from the proprietary Zilog ZDS-II toolchain
to the open source Clang-based AgonAdev toolchain. This does not work on
real hardwware yet! Don't try! It does run on fab-agon-emulator.

DO NOT INSTALL ON REAL HARDWARE YET!

Part of the official Agon Platform organisation firmware for all Agon computers.

This firmware is intended for use on any Agon Light compatible computer.  As well as the Agon Console8, it has been tested on the Olimex Agon Light 2.

The Agon Platform firmware is a fork from the original [official Quark firmware](https://github.com/breakintoprogram/agon-mos) for the Agon Light.  It contains many extensions and bug fixes.  Software written to run on Quark firmware should be fully compatible with Agon Platform releases.

### What is the Agon

Agon is a modern, fully open-source, 8-bit microcomputer and microcontroller in one small, low-cost board. As a computer, it is a standalone device that requires no host PC: it puts out its own video (VGA), audio (2 identical mono channels), accepts a PS/2 keyboard and has its own mass-storage in the form of a micro-SD card.

https://www.thebyteattic.com/p/agon.html

There are several variants of the Agon, including the original Agon Light, the Olimex Agon Light 2, the Agon Light Origins Editions, and the [Agon Console8](https://www.heber.co.uk/agon-console8).

The Agon Console8 is a fully compatible variant of the Agon Light from the same designer, with the addition of a built-in PS/2 mouse port and two Atari-style DB-9 joystick ports.

### What is a MOS

The MOS is a command line Machine Operating System, similar to CP/M or DOS, that provides a human interface to the Agon file system.  It runs on the eZ80 CPU of the Agon.

It also provides an API for file I/O and other common operations for BBC Basic for Z80 and other third-party applications.

### Loading BBC Basic for Z80

1. Download bbcbasic.bin from [agon-bbc-basic releases](https://github.com/breakintoprogram/agon-bbc-basic/releases)
2. Copy it to the `/bin` directory of your Agon's SD card
3. Insert the SD card into the AGON and reset/boot it
4. Run BBC BASIC by typing `bbcbasic` at the MOS command prompt
5. You should then be greeted with the BBC Basic for Z80 prompt

### Etiquette

Reporting issues and pull requests are welcome.

A Contributing guide will be added in due course.

### Build

The MOS is built using the Zilog Developer Studio II (ZDS II - eZ80Acclaim! version 5.3.5) tools.

You can download the ZDS II tools for free via the following link. The software contains an IDE, Debugger, C Compiler and eZ80 Assembler.

- [Zilog ZDS II Tools version 5.3.5](https://zilog.com/index.php?option=com_zcm&task=view&soft_id=54&Itemid=74)

ZDS II is a Windows application.  Development of Agon Platform MOS releases has been conducted on a MacBook Pro with an M1Max CPU running Windows 10 for ARM in a Parallels VM.  Other developers have used differing combinations of Windows, Linux and OSX, using VMs or Wine to run the ZDS II tools.

#### Creating a .bin file

The ZDS II tooling will compile the firmware and produce a `MOS.hex` file.  Unless you are programming the eZ80 on your Agon directly using ZDS II (see below), you will need to convert this to a `.bin` file to flash to the eZ80.

To convert the `.hex` file to a `.bin` file, use the [Hex2Bin utility](https://sourceforge.net/projects/hex2bin/).

### Testing the MOS

#### Using the Agon Emulator

MOS can be tested out without the need to reprogram your Agon Console8 or Agon Light by using the [Fab Agon Emulator](https://github.com/tomm/fab-agon-emulator).

To test using the emulator, create a new `MOS.bin` file and place that into the emulator directory and run the emulator.  The emulator will automatically load the `MOS.bin` file and run it.

It should be noted that the emulator is not 100% accurate, so some features may not work as expected, but it is a very close simulation.

Unless you are using the ZDS II tools to program the eZ80 directly, it is recommended that you test your MOS on the emulator before testing on real hardware.

#### Flashing your Agon Light or Agon Console8

The MOS can also be flashed on your device using the [agon-flash Agon MOS firmware upgrade utility](https://github.com/AgonPlatform/agon-flash).  This is a command line utility that runs on the Agon itself, and can flash MOS to the eZ80 from a file stored on your SD card.

In case of emergency, such as flashing an experimental build of MOS you have built that does not work, there is an [Agon recovery utility](https://github.com/AgonPlatform/agon-recovery) that can be used to revert the Agon back to a known working version of the MOS.

It is recommended when using the agon-flash utility that you use a filename other than `MOS.bin` for your new experimental MOS version, and keep a known working version of the `MOS.bin` file in the root of your SD card.

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

The Agon Platform documentation can be found on the [Community Documentation](https://agonplatform.github.io/agon-docs/) site.  This site provides extensive documentation about the Agon Platform firmware, covering both Quark and Console8 firmware releases.

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
