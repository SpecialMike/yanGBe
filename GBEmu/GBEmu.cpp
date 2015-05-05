//Will be the entry-point, though it currently contains all the CPU logic in it as well for now.

#include "stdafx.h"
#include "GBEmu.h"
#include "GPU.h"
#include <sstream>

int timerCounter;	//counting the number of cycles for use by the timers
int timerPeriod;

int dividerCounter;

using namespace std;
uint16 SP = 0xFFFEu;
uint16 PC = 0x0100u;
uint8 *cartROM;
unsigned int clockM = 0;
unsigned int clockT = 0;
bool interruptEnabled = true;
bool skipPCIncrement = false;

void updateTimer(int cycles);
void handleStop();
void handleHalt();
int OP(uint8 code);
void CB(uint8 code);
bool openROM(char* file);
void getCartInfo();

void RLC(uint8* reg);
void RRC(uint8* reg);
void RL(uint8* reg);
void RR(uint8* reg);

GPU* g = new GPU();
MMU* m;
unsigned int next_time = 0;
ofstream fout;
bool skipTimerUpdate = false;

unsigned int time_left(void)
{
	unsigned int now;

	now = SDL_GetTicks();
	if (next_time <= now)
		return 0;
	else
		return next_time - now;
}

void init(){
	SP = 0xFFFEu;
	m->writeByte(0xFF05, 0);
	m->writeByte(0xFF06, 0);
	m->writeByte(0xFF07, 0);
	m->writeByte(0xFF10, 0x80u);
	m->writeByte(0xFF11, 0xBFu);
	m->writeByte(0xFF12, 0xF3u);
	m->writeByte(0xFF14, 0xBFu);
	m->writeByte(0xFF16, 0x3Fu);
	m->writeByte(0xFF17, 0);
	m->writeByte(0xFF19, 0xBFu);
	m->writeByte(0xFF1A, 0x7Fu);
	m->writeByte(0xFF1B, 0xFFu);
	m->writeByte(0xFF1C, 0x9Fu);
	m->writeByte(0xFF1E, 0xBFu);
	m->writeByte(0xFF20, 0xFFu);
	m->writeByte(0xFF21, 0);
	m->writeByte(0xFF22, 0);
	m->writeByte(0xFF23, 0xBFu);
	m->writeByte(0xFF24, 0x77u);
	m->writeByte(0xFF25, 0xF3u);
	m->writeByte(0xFF26, 0xF1u);
	m->writeByte(0xFF40, 0x91u);
	m->writeByte(0xFF42, 0);
	m->writeByte(0xFF43, 0);
	m->writeByte(0xFF45, 0);
	m->writeByte(0xFF47, 0xFCu);
	m->writeByte(0xFF48, 0xFFu);
	m->writeByte(0xFF49, 0xFFu);
	m->writeByte(0xFF4A, 0);
	m->writeByte(0xFF4B, 0);
	m->writeByte(0xFFFF, 0);

	dividerCounter = 40;
	timerPeriod = 1024;
}

int main(int argc, char* argv[]){
	if (argc != 2)
		return 1;
	if (!openROM(argv[1])){
		return 1;
	}
	fout.open("output.txt");
	getCartInfo();
	m = new MMU(MMU::MBC1, 1, 1, cartROM);
	g->Reset();
	init();
	bool done = false;
	SDL_Event event;
	while (true){
		MainLoop();
		SDL_Delay(time_left());
		next_time += 16;
	}
	fout.close();
	delete g;
	delete m;
	return 0;
	while (!done){
		SDL_WaitEvent(&event);

		switch (event.type){
			case SDL_QUIT:
				done = true;
		}
		g->Update();
	}
}

void serviceInterrupt(int bit){
	interruptEnabled = false;
	m->writeByte(0xFF0fu, IF & ~(1 << bit));	//reset the interrupt specific flag in IF
	g->Step(20);
	updateTimer(20);
	//push PC onto stack
	SP -= 1;
	m->writeByte(SP, PC >> 8);
	SP -= 1;
	m->writeByte(SP, PC & 0xFFu);

	switch (bit){
	case 0: 
		PC = 0x40u; 
		break;
	case 1:
		PC = 0x48u;
		break;
	case 2:
		PC = 0x50u;
		break;
	case 3:
		PC = 0x58u;
		break;
	case 4:
		PC = 0x60u;
		break;
	}
}

void requestInterrupt(interrupts i){
	m->writeByte(0xFF0Fu, IF | (0x1u << i));
}

bool handleInterrupts(){
	if (interruptEnabled){
		if (IF > 0){	//if there are any flags set in IF
			for (int i = 0; i < 5; i++){	//go from bit 0 to bit 5, in order of priority
				if ( ((IF & (1 << i)) > 0) && 
					 ((IE & (1 << i)) > 0) ){	//if the specific interrupt flag is set and it is enabled
					serviceInterrupt(i);
					return true;
				}
			}
		}
	}
	return false;
}


std::string HexDec2String(int hexIn) {
	char hexString[4 * sizeof(int) + 1];
	// returns decimal value of hex
	sprintf(hexString, "%X", hexIn);
	return std::string(hexString);
}

void MainLoop(){
	const int cyclesPerUpdate = 70224;
	int cycles = 0;

	while (cycles < cyclesPerUpdate){
//		fout << HexDec2String(PC) << " : " << HexDec2String(m->readByte(PC));
		int oldPC = PC;
		int lastOP = OP(m->readByte(PC++));
//		fout << std::endl;
		if (lastOP == -1){
			printf("Error: %4X %X", PC-1, m->readByte(PC-1));
			fout.close();
		}
		if (PC == 0xC2C4)
			printf("");
		g->Step(lastOP);
		handleInterrupts();
		updateTimer(lastOP);
		cycles += lastOP;
	}
	g->Update();
}

//Load the given ROM into Emu memory
bool openROM(char* file){
	FILE* input = fopen(file, "r+");
	if (input == NULL){
		cout << "Error opening ROM at " << file;
		return false;
	}
	else{
		fseek(input, 0, SEEK_END);
		long int size = ftell(input);
		fclose(input);
		input = fopen(file, "r+");
		cartROM = new uint8[size];
		fread(cartROM, sizeof(uint8), size, input);
		fclose(input);
		return true;
	}

}

//Load the given ROM's memory bank #0 into GB memory
void loadROM(){
	for (int i = 0; i < 0x4000u; i++){

	}
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