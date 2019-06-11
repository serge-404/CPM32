/*
Derived from RUNCPM project: https://github.com/MockbaTheBorg/RunCPM

MIT License

Copyright (c) 2017 Mockba the Borg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/ 

#ifndef RAM_H
#define RAM_H

/* see main.c for definition */

#ifndef RAM_FAST
#define RAM mem

uint8* _RamSysAddr(uint16 address) {
	return(&RAM[address]);
}

uint8 _RamRead(uint16 address) {
	return(RAM[address]);
}

void _RamWrite(uint16 address, uint8 value) {
	RAM[address] = value;
}

void _RamWrite16(uint16 address, uint16 value) {
	// Z80 is a "little indian" (8 bit era joke)
	_RamWrite(address, value & 0xff);
	_RamWrite(address + 1, (value >> 8) & 0xff);
}
#endif

#endif
