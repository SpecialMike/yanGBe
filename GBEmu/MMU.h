#pragma once

#define uint8 unsigned _int8

class CPU;

class MMU
{

public:
	enum cartType{
		MBC1, MBC2, MBC3, MBC5
	};

	MMU(cartType t, int numRom, int numRam, uint8* cartRom);
	~MMU();

	void IncrementDIV();
	void IncrementLY();
	void LoadRam(std::ifstream& fin);
	void LoadState(std::ifstream& fin);
	uint8 ReadByte(unsigned _int16 address);
	uint8 ReadInstruction(unsigned _int16* PC);
	uint8 ReadVram(unsigned _int16 address);
	void SaveRam(std::ofstream& fout);
	void SaveState(std::ofstream& fout);
	void SetCPU(CPU* cpu);
	void WriteByte(unsigned _int16 address, uint8 value);
	
	bool inBIOS;
	int keyColumn;
	uint8 column[2];

protected:

private:
	enum MBC1Mode{
		ramSwitch, romSwitch //4/32 and 16/8 mode, respectively
	};
	enum MBC3Mode{
		ramAccess, RTCAccess //if 0xA000-0xBFFF maps to eram or RTC register
	};

	void DMATransfer(uint8 value);

	uint8* rom;		//cartridge ROM
	uint8* bios;	//contains the startup program for the GB
	uint8* eram;	//contains the cartridge RAM
	uint8* vram;	//video RAM
	uint8* wram;	//working RAM
	uint8* oam;		//OAM (sprite data)
	uint8* io;		//I/O registers
	uint8* zram;	//Zero-page RAM
	uint8* RTC;		//RTC register, clock register on cart (only in MBC3)

	CPU* c;
	unsigned int currentRAMBank;
	unsigned int currentROMBank;
	MBC1Mode mode;
	int numROMBanks;
	int numRAMBanks;
	MBC3Mode RAMmode;
	bool ramWriteEnable;
	bool RTCLatchPossible;

};