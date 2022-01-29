# CPM32
It is a CP/M operation system emulator.
Appears as a hybrid fork of Keiji Murakami (CPM emu part) and Mockba the Borg (Z80 emu part) projects
with some corrections for ORION-128 VT52 screen control codes subset support.
Compiled with BCB5(Windows) and gcc(Linux).

Windows getch() issue: DEL key used instead of BACKSPACE (and BACKSPACE acts as a LEFT)
 because of cmd.exe 08h/7Fh codes processing features. 
