/*
 * MMU.cpp handles all of the memory management of the system. 
 * All reads and writes should go through the writeByte() and readByte() functions, with a couple of exceptions
*/

#include "stdafx.h"
#include "MMU.h"
#include "GBEmu.h"

enum RTCRegister{
	S = 0x8u, M, H, DL, DH
};
RTCRegister currentRTCRegister;
MMU::cartType type;


MMU::MMU(cartType t, int numRom, int numRam, uint8* cartRom)
{
	type = t;
	numROMBanks = numRom;
	numRAMBanks = numRam;

	rom = cartRom;								//0000-7FFF
	bios = new uint8[0xFFu];					//0000-00FF
	vram = new uint8[0x1FFFu];					//8000-9FFF
	eram = new uint8[(0x1FFFu * numRAMBanks)];	//A000-BFFF
	wram = new uint8[0x3DFFu];					//C000-FDFF
	oam = new uint8[0xFFu];						//FE00-FE9F
	io = new uint8[0x7Fu];						//FF00-FF7F
	zram = new uint8[0x7Fu];					//FF80-FFFF
	if (type == MBC3){
		RTC = new uint8[5];
	}

	inBIOS = true;
	currentRAMBank = 0;
	currentROMBank = 1;
	if (type == MBC1){
		mode = romSwitch;
	}

	RTCLatchPossible = false;
}

MMU::~MMU()
{
	delete rom;
	delete bios;
	delete vram;
	delete eram;
	delete wram;
	delete oam;
	delete io;
	delete zram;
	delete RTC;
}

uint8 MMU::operator[](const unsigned _int16 index){
	return readByte(index);
}

//Read the byte from memory at address
uint8 MMU::readByte(unsigned _int16 address){
	//TODO bank switching, etc.
	switch (address & 0xF000u){
	//0x0000-0x3FFF : Cartridge rom (first 16,384 bytes)
	case 0x0000u:
		//0x0000-0x00FF : GB BIOS after it is complete this maps to cartridge ROM
		if (inBIOS){
			if (address < 0x0100)
				return bios[address];
		}
	case 0x1000:
	case 0x2000:
	case 0x3000:
		//(MBC1) Always points to the first 16KB of ROM
		//(MBC2) Same as MBC1
		//(MBC3) Same as MBC1
		return rom[address];

	//0x4000-0x7FFF : Switchable bank #1
	case 0x4000:
	case 0x5000:
	case 0x6000:
	case 0x7000:
		//(MBC1) Contains the contents of each 16KB bank of ROM
		//(MBC1) Bank numbers 0x20, 0x40 and 0x60 are not valid (125 banks max)
		//(MBC2) Same as MBC1, only 16 ROM banks are supported
		//(MBC3) Same as MBC1, but now 0x20, 0x40 and 0x60 are supported (128 banks)
		return rom[(address - 0x4000u) + (currentROMBank * 0x4000u)];
	//0x8000-0x9FFF : Graphics RAM area
	case 0x8000:
	case 0x9000:
		return vram[address & 0x1FFFu];
	//0xA000-0xBFFF : Cartridge RAM
	case 0xA000:
	case 0xB000:
		//Eram should be buffered; many games have a battery to keep volatile memory alive
		switch (type){
		//(MBC1) If in 16/8 mode, all of eram is accessable
		//(MBC1) If in 4/32 mode, eram is banked (4 banks of 8KB)
		case MBC1:
			if (type == romSwitch){
				return eram[address - 0xA000u];
			}
			else{	//type == ramSwitch
				return eram[(address - 0xA000u) + (currentRAMBank * 0x2000u)];
			}
		//(MBC2) Only supports 512 4 bit values (addressable 0xA000-0xA1FF)
		case MBC2:
			if (address > 0xA1FFu){
				break;
			}
			else{	//0xA000 <= address <= 0xA1FF
				return eram[address - 0xA000u];
			}
		//(MBC3) eram is banked (4 banks)
		//(MBC3) Also used to read RTC register, if selected (any address corresponds to that register)
		case MBC3:
			if (RAMmode == ramAccess){
				return eram[(address - 0xA000u) + (currentRAMBank * 0x2000u)];
			}
			else{	//RAMmode == RTCAccess
				return RTC[currentRTCRegister - 0x8u];
			}
		}
		return eram[address];
	//0xC000-0xDFFF : Working RAM (the 8k on the GB chip)
	case 0xC000:
	case 0xD000:
		return wram[address & 0x1FFF];
	//0xE000-0xFDFF : Working RAM shadow (exact copy of what's in 0xC000-0xDFFF, just translate address to that range)
	case 0xE000:
		return wram[address & 0x1FFF];
	case 0xF000:
		switch (address & 0x0F00){
			//RAM shadow continued
		case 0x000: case 0x100: case 0x200: case 0x300: case 0x400: case 0x500:
		case 0x600: case 0x700: case 0x800: case 0x900: case 0xA00: case 0xB00:
		case 0xC00: case 0xD00:
			return wram[address & 0x1FFF];
	//0xFE00-0xFE9F : Sprite information
		case 0xE00:
			//OAM for 160 bytes, then the rest is always 0
			if (address < 0xFEA0)
				return oam[address & 0xFF];
			else
				return 0;
		case 0xF00:
			if (address >= 0xFF80){
				//0xFF80-0xFFFF : Working RAM (128 bytes of high speed RAM)
				return zram[address & 0x7F];
			}
			else{
				//0xFF00-0xFF7F : I/O
				return io[address & 0x7F];
			}
		}
	}
	return 0;	//just to get rid of the compiler warning; this will never be reached
}

//Read the byte from memory at PC and increment PC
uint8 MMU::readInstruction(_int16* PC){
	*PC = (*PC + 1) & 0xFFFFu;
	return readByte(*PC - 1);
}

RTCRegister getRTCReg(uint8 value){
	switch (value){
	case 0x08u:
		return S;
	case 0x09u:
		return M;
	case 0x0Au:
		return H;
	case 0x0Bu:
		return DL;
	case 0x0C:
		return DH;
	default:	//just to get rid of compiler warning, will never reach this
		return S;
	}
}

void MMU::writeByte(unsigned _int16 address, uint8 value){
	switch (address & 0xF000){
		//TODO: support for (MBC5), not that its used much anyway
	case 0x0000:
	case 0x1000:
		switch (type){
		//(MBC1)Enable RAM Bank if XXXX1010_2 (0x0A) is written, disable if anything else is written
		//(MBC2)Toggle cart RAM if the LSB of the upper address byte is 0 and same test as MBC1
		case MBC2:
			if ((address & 0x10u) > 0){
				break;
			}//if 0, just fall through
		//(MBC3)Same as MBC1, also enables read/writes to clock registers on cart
		case MBC1:
		case MBC3:
			if ((value & 0xFu) == 0xAu)
				ramWriteEnable = true;
			else
				ramWriteEnable = false;
			break;
		}
		break;
	case 0x2000:
	case 0x3000:
		switch (type){
		//(MBC1)Switch bank to XXXBBBBB_2, where X=don't care (0x00-0x1F)
		//(MBC1)Values of B=1 and B=0 are functionally equivalent, bank#0 is accessible from 0x0000-0x3FFF
		//(MBC1)Values of 0x20, 0x40, and 0x60 will result in 0x21, 0x41, 0x61 respectively
		case MBC1:
			currentROMBank &= 0xE0u;	//set lower 5 bits to 0;
			currentROMBank |= (value & 0x1F);
			if (currentROMBank == 0 || currentROMBank == 0x20u || currentROMBank == 0x40u || currentRAMBank == 0x60u)
				currentROMBank++;
			break;
		//(MBC2)Switch ROM bank to XXXXBBBB_2, where X=don't care
		//(MBC2)Address must have the LSB of the upper address byte = 1 (X1XX,X3XX,X5XX,etc.)
		case MBC2:
			currentROMBank &= 0xF0u;
			currentROMBank |= (value & 0xFu);
			if (currentROMBank == 0)
				currentROMBank++;
			break;
		//(MBC3)Writing a value of XBBBBBBB_2 selects the appropriate ROM bank (value of 0 is a value of 1 again)
		case MBC3:
			currentROMBank &= 0x80u;
			currentROMBank |= (value & 0x7F);
			if (currentROMBank == 0)
				currentROMBank++;
			break;
		}
		break;
	case 0x4000:
	case 0x5000:
		switch (type){
		//(MBC1)If in 4/32 Mode, switch RAM bank XXXXXXBB_2 (0x00-0x03)
		//(MBC1)If in 16/8 Mode, set the two most significant ROM address lines to XXXXXXBB_2 (Bits 5 and 6)
		case MBC1:
			if (type == romSwitch){
				currentROMBank &= 0x9Fu; //turn off bits 5 and 6
				currentROMBank |= (value & 0x60u);
				if (currentROMBank == 0)
					currentROMBank++;
			}
			else{	//RAM switching
				currentRAMBank = (value & 0x03u);
			}
		case MBC2:
			break;
		//(MBC3)If a value of 0x00-0x03 is written, select that RAM bank
		//(MBC3)If a value of 0x08-0x0C is written, select that RTC register
		case MBC3:
			if (value <= 0x03u){
				currentRAMBank = value & 0x03u;
			}
			else if (value >= 0x08u && value <= 0x0cu){
				currentRTCRegister = getRTCReg(value);
			}
		}
		break;
	case 0x6000:
	case 0x7000:
		switch (type){
		//(MBC1)Switch to memory mode specified by XXXXXXXB_2, where X= don't care
		//(MBC1)B=0 -> 16Mb ROM/8Kb RAM, B=1 -> 4Mb ROM/32Kb RAM
		case MBC1:
			if ((value & 0x01u) == 0){
				mode = romSwitch;
			}
			else{
				mode = ramSwitch;
				currentRAMBank = 0;	//no ram switching
			}
		case MBC2:
			break;
		//(MBC3)Latch clock data if 0x00 then 0x01 is written (write current time to RTC registers)
		case MBC3:
			if (value == 0x00u){
				RTCLatchPossible = true;
			}
			if (RTCLatchPossible && (value == 0x01u)){
				//TODO: RTC Latch here
				RTCLatchPossible = false;
			}
		}
		break;
	case 0x8000:
	case 0x9000:
		vram[address & 0x1FFFu] = value;
		break;
	case 0xA000:
	case 0xB000:
		//(MBC2) only addressable from 0xA000 to 0xA1FF
		//(MBC2) only write lower 4 bits of value (512 x 4bit values)
		if (!ramWriteEnable)
			break;
		if (type == MBC2){
			if (address <= 0xA1FFu){
				eram[address & 0x1FFFu] = value & 0x0001u;
			}
		}
		else
			eram[address & 0x1FFFu] = value;
		break;
	case 0xC000:
	case 0xD000:
	case 0xE000:
		wram[address & 0x1FFFu] = value;
		break;
	case 0xF000:
		switch (address & 0x0F00u)
		{
			// Echo RAM
		case 0x000: case 0x100: case 0x200: case 0x300:
		case 0x400: case 0x500: case 0x600: case 0x700:
		case 0x800: case 0x900: case 0xA00: case 0xB00:
		case 0xC00: case 0xD00:
			wram[address & 0x1FFFu] = value;
			break;

			// OAM
		case 0xE00:
			if ((address & 0xFF)<0xA0)	//anything after A0 should be 0s
				oam[address & 0xFFu] = value;
			break;

			// Zeropage RAM, I/O
		case 0xF00:
			if (address > 0xFF7F)
				zram[address & 0x7Fu] = value;
			else{
				if (address == 0xFF04u){	//writes to the DIV register reset the DIV timer
					io[0x04] = 0;
				}
				else if (address == 0xFF07){
					uint8 currentTMC = io[0x07];
					uint8 newTMC = value;
					io[0x07] = value;

					if ((currentTMC & 0x03) != (newTMC & 0x03)){	//clock frequency has changed, so reset the counter for it
						switch (newTMC & 0x03){
						case 0x0u:
							timerCounter = 1024;
							break;
						case 0x1u:
							timerCounter = 16;
							break;
						case 0x2u:
							timerCounter = 64;
							break;
						case 0x3u:
							timerCounter = 256;
							break;
						}
					}
				}
				else if (address == 0xFF44){
					io[0x44] = 0;
				}
				else{
					io[address & 0x7Fu] = value;
				}
			}
		}
		break;
	}
}

//need this because any writes to 0xFF04 or 0xFF44 reset them to 0
void MMU::incrementDIV(){	
	io[0x04]++;
}

void MMU::incrementLY(){
	io[0x44]++;
}