# CPM32
It is a CP/M operation system emulator.
Appears as a hybrid fork of Keiji Murakami (CPM emu part) and Mockba the Borg (Z80 emu part) projects
with some corrections for ORION-128 VT52 screen control codes subset support.
Compiled with BCB5(Windows) and gcc(Linux).

Windows getch() issue: DEL key used instead of BACKSPACE (and BACKSPACE acts as a LEFT)
 because of cmd.exe 08h/7Fh codes processing features.

Use "./cpm -?" for short help:

```
$ ./cpm -?
CPM -- CP/M-80 program EXEcutor for LINUX V0.5
Copyright (C) 2004-2012 by K.Murakami
  Usage: CPM [-hxapdCkr][-w[0-9]] command arg1 arg2 ...
        -h .. return HI-TECH C exit code
        -x .. return ERROR if A:$$$.SUB deleted
        -a .. select A: (program directry)
        -p .. pause before exit
        -d .. disable auto drive assign
        -C .. args to uppercase
        -o .. orion128 ROM F800 mode (+allow exec RKO,ORD)
        -8 .. do not KOI8 conversion
        -r .. do Robotron-1715 escapes
        -k .. do Kaypro(adm3a) escapes
        -w[0-9] .. wait on console status check (9:max)

``` 

Environment variables:
```
CPMPATH - default CP/M binaries dir
ORDPATH - default ORDOS binaries dir (Orion-128 mode)
```
