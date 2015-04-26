#pragma once

#define uint8 unsigned _int8

class MMU
{
private:
	uint8* rom;		//cartridge ROM
	uint8* bios;	//contains the startup program for the GB
	uint8* eram;	//contains the cartridge RAM
	uint8* vram;	//video RAM
	uint8* wram;	//working RAM
	uint8* oam;		//OAM (sprite data)
	uint8* io;		//I/O registers
	uint8* zram;	//Zero-page RAM
	uint8* RTC;		//RTC register, clock register on cart (only in MBC3)
	unsigned int currentRAMBank;
	unsigned int currentROMBank;
	int numROMBanks;
	int numRAMBanks;
	enum MBC1Mode{
		ramSwitch, romSwitch //4/32 and 16/8 mode, respectively
	};
	MBC1Mode mode;
	enum MBC3Mode{
		ramAccess, RTCAccess //if 0xA000-0xBFFF maps to eram or RTC register
	};
	MBC3Mode RAMmode;
	bool ramWriteEnable;
	bool RTCLatchPossible;
public:
	enum cartType{
		MBC1, MBC2, MBC3
	};
	bool inBIOS;
	MMU(cartType t, int numRom, int numRam, uint8* cartRom);
	~MMU();
	uint8 readByte(unsigned _int16 address);
	uint8 readInstruction(_int16* PC);
	void writeByte(unsigned _int16 address, uint8 value);
	uint8 operator[](unsigned _int16 index);
	void incrementDIV();
	void incrementLY();
};