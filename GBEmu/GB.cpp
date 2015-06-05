#include "stdafx.h"
#include "GB.h"

uint8 *cartROM;

GB::GB(const char* filePath)
{
	if (!openROM(filePath)){
		std::cout << "Error opening file";
	}
	g = new GPU();
	c = new CPU();
	m = new MMU(MMU::MBC1,1,1,cartROM);
	g->Reset(m, c);
	c->setGPU(g);
	c->setMMU(m);
	m->setCPU(c);
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
		std::cout << "Error opening ROM at " << file;
		return false;
	}

	fseek(input, 0, SEEK_END);
	long int size = ftell(input);
	fclose(input);
	input = fopen(file, "r+");
	cartROM = new uint8[size];
	fread(cartROM, sizeof(uint8), size, input);
	fclose(input);
	m = new MMU(MMU::MBC1, 1, 1, cartROM);
	return true;

}

//Prints out the cart's info from 0x134u to 0x14Fu
void getCartInfo(){
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
	switch (cartROM[0x146u]){
	case 0:
		printf("ROM ONLY\n");
		break;
	case 1:
		printf("ROM+MBC1\n");
		break;
	case 2:
		printf("ROM+MBC1+RAM\n");
		break;
	case 3:
		printf("ROM+MBC1+RAM+BATT\n");
		break;
	case 5:
		printf("ROM+MBC2\n");
		break;
	case 6:
		printf("ROM+MBC2+BATT\n");
		break;
	case 8:
		printf("ROM+RAM\n");
		break;
	case 9:
		printf("ROM+RAM+BATT\n");
		break;
	case 0xBu:
		printf("ROM+MMM01\n");
		break;
	case 0xCu:
		printf("ROM+MMM01+SRAM\n");
		break;
	case 0xDu:
		printf("ROM+MMM01+SRAM+BATT\n");
		break;
	case 0xFu:
		printf("ROM+MBC3+TIMER+BATT\n");
		break;
	case 0x10u:
		printf("ROM+MBC3+TIMER+RAM+BATT\n");
		break;
	case 0x11u:
		printf("ROM+MBC3\n");
		break;
	case 0x12u:
		printf("ROM+MBC3+RAM\n");
		break;
	case 0x13u:
		printf("ROM+MBC3+RAM+BATT\n");
		break;
	case 0x19u:
		printf("ROM+MBC5\n");
		break;
	case 0x1Au:
		printf("ROM+MBC5+RAM\n");
		break;
	case 0x1Bu:
		printf("ROM+MBC5+RAM+BATT\n");
		break;
	case 0x1Cu:
		printf("ROM+MBC5+RUMBLE\n");
		break;
	case 0x1Du:
		printf("ROM+MBC5+RUMBLE+SRAM\n");
		break;
	case 0x1Eu:
		printf("ROM+MBC5+RUMBLE+SRAM+BATT\n");
		break;
	case 0x1Fu:
		printf("Pocket Camera\n");
		break;
	case 0xFDu:
		printf("Bandai TAMA5\n");
		break;
	case 0xFEu:
		printf("Hudson HuC-3\n");
		break;
	case 0xFFu:
		printf("Hudson HuC-1\n");
		break;
	default:
		printf("Error\n");
	}

	printf("ROM size: ");
	switch (cartROM[0x148u]){
	case 0:
		printf("256Kbit - 2 banks\n");
		break;
	case 1:
		printf("512Kbit - 4 banks\n");
		break;
	case 2:
		printf("1Mbit - 8 banks\n");
		break;
	case 3:
		printf("2Mbit - 16 banks\n");
		break;
	case 4:
		printf("4Mbit - 32 banks\n");
		break;
	case 5:
		printf("8Mbit - 64 banks\n");
		break;
	case 6:
		printf("16Mbit - 128 banks\n");
		break;
	case 0x52u:
		printf("9Mbit - 72 banks\n");
		break;
	case 0x53u:
		printf("10Mbit - 80 banks\n");
		break;
	case 0x54u:
		printf("12Mbit - 96 banks\n");
		break;
	default:
		printf("Error\n");
	}

	printf("RAM size: ");
	switch (cartROM[0x149u]){
	case 0:
		printf("None\n");
		break;
	case 1:
		printf("16Kbit - 1 bank\n");
		break;
	case 2:
		printf("64Kbit - 1 bank\n");
		break;
	case 3:
		printf("256Kbit - 4 banks\n");
		break;
	case 4:
		printf("1Mbit - 16 banks\n");
		break;
	default:
		printf("Error\n");
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
}

void GB::UpdateToVBlank(){
	const int cyclesPerUpdate = 70224;
	int cycles = 0;

	while (cycles < cyclesPerUpdate){
		int addedCycles = c->update();

		g->Step(cycles);
		c->handleInterrupts();
		c->updateTimer(addedCycles);
		cycles += addedCycles;
	}
	g->Update();
}