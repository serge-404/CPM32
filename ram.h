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

uint8* _RamAddr(uint16 address) {
	if (!orion128) return &mem[address];
	return dispatcher && (address < 0x4000) ? &mem16[address] : 
	 		( (address < 0xf000) || fullram ? &mem[address] : &mem1024[address]);  
}
/*
uint8 _RamRead(uint16 address) {
	return *_RamAddr(address); 
}
*/
#define _RamRead(x) (*_RamAddr(x)) 

void _RamWrite(uint16 address, uint8 value) {
	if ((! orion128) || fullram || (address < 0xf800))
		*_RamAddr(address)=value;
}
#endif

#endif
