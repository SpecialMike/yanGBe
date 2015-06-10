#include "stdafx.h"
#include <sstream>
#include "GB.h"
#include <Windows.h>
#include <wx/defs.h>

using namespace std;

GB::GB(const char* filePath)
{
	m = nullptr;
	if (!openROM(filePath)){
		cout << "Error opening file";
	}
	else{
		g = new GPU();
		c = new CPU();
		//m = new MMU(MMU::MBC1,1,1,cartROM);
		g->Reset(m, c);
		c->setGPU(g);
		c->setMMU(m);
		m->setCPU(c);
	}
}

GB::GB(){
	g = new GPU();
	c = new CPU();
}


GB::~GB()
{
	delete cartROM;
	delete g;
	delete c;
	delete m;
}

bool GB::openROM(const char* file){
	FILE* input = fopen(file, "r+");
	if (input == NULL){
		cout << "Error opening ROM at " << file;
		return false;
	}

	fseek(input, 0, SEEK_END);
	long int size = ftell(input);
	fclose(input);
	input = fopen(file, "r+");
	cartROM = new uint8[size];
	fread(cartROM, sizeof(uint8), size, input);
	fclose(input);
	getCartInfo();
	//m = new MMU(MMU::MBC1, 1, 1, cartROM);

	isProcessing = false;

	return m != nullptr;
	//return true;

}

//Prints out the cart's info from 0x134u to 0x14Fu
//
//Also creates the MMU, because this information contains
//everything you need to know about it.
void GB::getCartInfo(){
	MMU::cartType type;
	int numRomBanks;
	int numRamBanks;

	char name[15];
	for (int i = 0; i <15; i++){
		name[i] = cartROM[0x134u + i];
	}
	printf("Game name: %s\n", name);
	if (cartROM[0x143u] == 0x80u){
		printf("For color GB\n");
	}
	else{
		printf("Not for color GB\n");
	}
	printf("Licensee code: %X%X\n", cartROM[0x144u] & 0xFFu, cartROM[0x145u] & 0xFFu);
	printf("GB/SGB indicator: %X\n", cartROM[0x146u] & 0xFFu);
	printf("Cartridge type: ");
	switch (cartROM[0x147u]){
	case 0:
		printf("ROM ONLY\n");
		type = MMU::MBC1;
		break;
	case 1:
		printf("ROM+MBC1\n");
		type = MMU::MBC1;
		break;
	case 2:
		printf("ROM+MBC1+RAM\n");
		type = MMU::MBC1;
		break;
	case 3:
		printf("ROM+MBC1+RAM+BATT\n");
		type = MMU::MBC1;
		break;
	case 5:
		printf("ROM+MBC2\n");
		type = MMU::MBC2;
		break;
	case 6:
		printf("ROM+MBC2+BATT\n");
		type = MMU::MBC2;
		break;
	case 8:
		printf("ROM+RAM\n");
		type = MMU::MBC1;
		break;
	case 9:
		printf("ROM+RAM+BATT\n");
		type = MMU::MBC1;
		break;
	case 0xBu:
		printf("ROM+MMM01\n");
		type = MMU::MBC1;
		break;
	case 0xCu:
		printf("ROM+MMM01+SRAM\n");
		type = MMU::MBC1;
		break;
	case 0xDu:
		printf("ROM+MMM01+SRAM+BATT\n");
		type = MMU::MBC1;
		break;
	case 0xFu:
		printf("ROM+MBC3+TIMER+BATT\n");
		type = MMU::MBC3;
		break;
	case 0x10u:
		printf("ROM+MBC3+TIMER+RAM+BATT\n");
		type = MMU::MBC3;
		break;
	case 0x11u:
		printf("ROM+MBC3\n");
		type = MMU::MBC3;
		break;
	case 0x12u:
		printf("ROM+MBC3+RAM\n");
		type = MMU::MBC3;
		break;
	case 0x13u:
		printf("ROM+MBC3+RAM+BATT\n");
		type = MMU::MBC3;
		break;
	case 0x19u:
		printf("ROM+MBC5\n");
		type = MMU::MBC5;
		break;
	case 0x1Au:
		printf("ROM+MBC5+RAM\n");
		type = MMU::MBC5;
		break;
	case 0x1Bu:
		printf("ROM+MBC5+RAM+BATT\n");
		type = MMU::MBC5;
		break;
	case 0x1Cu:
		printf("ROM+MBC5+RUMBLE\n");
		type = MMU::MBC5;
		break;
	case 0x1Du:
		printf("ROM+MBC5+RUMBLE+SRAM\n");
		type = MMU::MBC5;
		break;
	case 0x1Eu:
		printf("ROM+MBC5+RUMBLE+SRAM+BATT\n");
		type = MMU::MBC5;
		break;
	case 0x1Fu:
		printf("Pocket Camera\n");
		type = MMU::MBC1;
		break;
	case 0xFDu:
		printf("Bandai TAMA5\n");
		type = MMU::MBC1;
		break;
	case 0xFEu:
		printf("Hudson HuC-3\n");
		type = MMU::MBC1;
		break;
	case 0xFFu:
		printf("Hudson HuC-1\n");
		type = MMU::MBC1;
		break;
	default:
		printf("Error\n");
		return;
	}

	printf("ROM size: ");
	switch (cartROM[0x148u]){
	case 0:
		printf("256Kbit - 2 banks\n");
		numRomBanks = 2;
		break;
	case 1:
		printf("512Kbit - 4 banks\n");
		numRomBanks = 4;
		break;
	case 2:
		printf("1Mbit - 8 banks\n");
		numRomBanks = 8;
		break;
	case 3:
		printf("2Mbit - 16 banks\n");
		numRomBanks = 16;
		break;
	case 4:
		printf("4Mbit - 32 banks\n");
		numRomBanks = 32;
		break;
	case 5:
		printf("8Mbit - 64 banks\n");
		numRomBanks = 64;
		break;
	case 6:
		printf("16Mbit - 128 banks\n");
		numRomBanks = 128;
		break;
	case 0x52u:
		printf("9Mbit - 72 banks\n");
		numRomBanks = 72;
		break;
	case 0x53u:
		printf("10Mbit - 80 banks\n");
		numRomBanks = 80;
		break;
	case 0x54u:
		printf("12Mbit - 96 banks\n");
		numRomBanks = 96;
		break;
	default:
		printf("Error\n");
		return;
	}

	printf("RAM size: ");
	switch (cartROM[0x149u]){
	case 0:
		printf("None\n");
		numRamBanks = 0;
		break;
	case 1:
		printf("16Kbit - 1 bank\n");
		numRamBanks = 1;
		break;
	case 2:
		printf("64Kbit - 1 bank\n");
		numRamBanks = 1;
		break;
	case 3:
		printf("256Kbit - 4 banks\n");
		numRamBanks = 4;
		break;
	case 4:
		printf("1Mbit - 16 banks\n");
		numRamBanks = 16;
		break;
	default:
		printf("Error\n");
		return;
	}

	printf("Destination code: ");
	switch (cartROM[0x14Au]){
	case 0:
		printf("Japanese\n");
		break;
	case 1:
		printf("Non-Japanese\n");
		break;
	default:
		printf("Error\n");
	}
	m = new MMU(type, numRomBanks, numRamBanks, cartROM);
}

void GB::UpdateToVBlank(){
	isProcessing = true;
	const int cyclesPerUpdate = 70224;
	int cycles = 0;

	while (cycles < cyclesPerUpdate){
		int addedCycles = c->update();

		g->Step(addedCycles);
		c->handleInterrupts();
		c->updateTimer(addedCycles);
		cycles += addedCycles;
	}

	g->Update();
	isProcessing = false;
}

void GB::buttonDown(int key){
	switch (key){
	case WXK_UP:
		m->column[1] &= 0xB;
		break;
	case WXK_DOWN:
		m->column[1] &= 0x7;
		break;
	case WXK_LEFT:
		m->column[1] &= 0xD;
		break;
	case WXK_RIGHT:
		m->column[1] &= 0xE;
		break;
	case WXK_RETURN:
		m->column[0] &= 0x7;
		break;
	case '\\':
		m->column[0] &= 0xB;
		break;
	case 'A':
		m->column[0] &= 0xE;
		break;
	case 'B':
		m->column[0] &= 0xD;
		break;
	}
}

void GB::buttonUp(int key){
	switch (key){
	case WXK_UP:
		m->column[1] |= 0x4;
		break;
	case WXK_DOWN:
		m->column[1] |= 0x8;
		break;
	case WXK_LEFT:
		m->column[1] |= 0x2;
		break;
	case WXK_RIGHT:
		m->column[1] |= 0x1;
		break;
	case WXK_RETURN:
		m->column[0] |= 0x8;
		break;
	case '\\':
		m->column[0] |= 0x4;
		break;
	case 'A':
		m->column[0] |= 0x1;
		break;
	case 'B':
		m->column[0] |= 0x2;
		break;
	}
}

void GB::SaveState(const char* filePath){
	if (isProcessing){
		printf("Is Processing");
	}
	ofstream fout;
	fout.open(filePath, ios::binary);
	m->SaveState(fout);
	c->SaveState(fout);
	fout.close();
}

void GB::LoadState(const char* filePath){
	ifstream fin;
	fin.open(filePath, ios::binary);
	m->LoadState(fin);
	c->LoadState(fin);
	fin.close();
}