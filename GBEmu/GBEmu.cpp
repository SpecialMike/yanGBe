//Will be the entry-point, though it currently contains all the CPU logic in it as well for now.

#include "stdafx.h"
#include "GBEmu.h"
#include "GPU.h"
#include <sstream>

#define FLAG_ZERO 7
#define FLAG_SUB 6
#define FLAG_HC 5
#define FLAG_C 4
#define R_BC ((B<<8) | C)
#define R_HL ((H<<8) | L)
#define R_DE ((D<<8) | E)

int timerCounter;	//counting the number of cycles for use by the timers
int dividerCounter;
using namespace std;
bitset<8> F; //F = flags: F7 = Zero flag, F6 = Subtract Flag, F5 = Half Carry flag, F4 = Carry Flag, F3-F0 always 0
uint8 A = 0x01u, B, C, D, E, H, L;//A = accumulator 
uint16 SP = 0xFFFEu;
uint16 PC = 0x0100u;
uint8 *cartROM;
unsigned int clockM = 0;
unsigned int clockT = 0;
bool interruptEnabled = true;
bool skipPCIncrement = false;

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
	A = 0x01u;
	F = 0xB0u;
	B = 0x00u;
	C = 0x13u;
	D = 0x00u;
	E = 0xD8u;
	H = 0x01u;
	L = 0x4Du;
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

void handleInterrupts(){
	if (interruptEnabled){
		if (IF > 0){	//if there are any flags set in IF
			for (int i = 0; i < 5; i++){	//go from bit 0 to bit 5, in order of priority
				if ( ((IF & (1 << i)) > 0) && 
					 ((IE & (1 << i)) > 0) ){	//if the specific interrupt flag is set and it is enabled
					serviceInterrupt(i);
				}
			}
		}
	}
}

void updateTimer(int cycles){
	if ((TMC & 0x0004u) > 0x0000u){	//Bit 2 of TMC indicates an enabled timer
		timerCounter -= cycles;
		if (timerCounter <= 0){
			switch (TMC & 0x0003u){	//reset timerCounter
			case 0x00:
				timerCounter = 1024;
				break;
			case 0x01:
				timerCounter = 16;
				break;
			case 0x02:
				timerCounter = 64;
				break;
			case 0x03:
				timerCounter = 256;
				break;
			}
			if (TIMA == 255){	//overflow occurs, reset TIMA and interrupt
				m->writeByte(0xFF05, TMA);
				requestInterrupt(TIMER);
			}
			else{	//otherwise increment TIMA
				m->writeByte(0xFF05, TIMA + 1);
			}
		}
	}

	//Divider Register
	dividerCounter += cycles;
	if (dividerCounter >= 255){
		dividerCounter = 0;
		m->incrementDIV();
	}
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
		int lastOP = OP(m->readByte(PC++));
//		fout << std::endl;
		if (lastOP == -1){
			printf("error");
			fout.close();
		}
		updateTimer(lastOP);
		g->Step(lastOP);
		handleInterrupts();
		cycles += lastOP;
	}
	g->Update();
}

//Adds reg to the accumulator (A)
void ADD(uint8* reg){
	uint8 val = A + (*reg);
	//cast to int should allow correct checking
	F.set(FLAG_HC, (((int)A) & 0xF) + ((*reg) & 0xF) > 0xF);
	F.set(FLAG_C, (((int)A) + (*reg)) > 0xFFu);
	A = val;
	F.set(FLAG_ZERO, A == 0);
	F.set(FLAG_SUB, false);
}

//Adds reg + carry flag to accumulator
void ADC(uint8* reg){
	uint8 cVal = ((F.test(FLAG_C)) ? 0x01u : 0x00u);
	uint8 val = A + (*reg) + cVal;
	F.set(FLAG_HC, (((int)A) & 0xF) + ((*reg) & 0xF) + cVal > 0xF);
	F.set(FLAG_C, (int)A + (*reg) + cVal > 0xFF);
	A = val;
	F.set(FLAG_ZERO, A == 0);
	F.set(FLAG_SUB, false);
}

//Subtracts reg from accumulator
void SUB(uint8* reg){
	uint8 val = A - (*reg);
	F.set(FLAG_C, A < (*reg));
	F.set(FLAG_HC, (A & 0xF) < ((*reg) & 0xF) );
	A = val;
	F.set(FLAG_ZERO, A == 0);
	F.set(FLAG_SUB);
}

//Subtracts reg+1 from accumulator
void SBC(uint8* reg){
	uint8 cVal = ((F.test(FLAG_C)) ? 0x01u : 0x00u);
	uint8 val = A - ((*reg) + cVal);
	F.set(FLAG_C, A < ((*reg) + cVal) );
	F.set(FLAG_HC, (A & 0xF) < ( ((*reg) & 0xF) + cVal) );
	A = val;
	F.set(FLAG_ZERO, A == 0);
	F.set(FLAG_SUB);
}

//Adds HL and 16 bit register and stores in HL.
void ADDHL(uint16 reg){
	uint32 temp = R_HL + reg; 
	F.set(FLAG_HC, (R_HL & 0xFFFu) > (temp & 0xFFFu));
	F.set(FLAG_C, (temp > 0xFFFFu));
	L = temp & 0xFFu;
	H = temp >> 8 & 0xFFu;
	F.set(FLAG_SUB, false);
}

void INC(uint8* reg){
	(*reg) += 1;
	F.set(FLAG_ZERO, (*reg) == 0);
	F.set(FLAG_SUB, false);
	F.set(FLAG_HC, ((*reg) & 0xFu) == 0);
}

int OP(uint8 code){
	uint16 nn = 0;
	uint32 temp = 0;
	uint8 temp2 = 0;
	int signedTemp = 0;
	switch (code){
	case 0x00u: //NOP (do nothing)
		return 4;
	case 0x01u: //LD BC, nn Load a 16-bit immediate nn into BC
		C = m->readByte(PC++);
		B = m->readByte(PC++);
		return 12;
	case 0x02u: //LD (BC), A Load A into (BC)
		m->writeByte(R_BC, A);
		return 8;
	case 0x03u: //INC BC Increment BC
		temp = R_BC + 1;
		B = temp >> 8 & 0xFFu;
		C = temp & 0xFFu;
		return 8;
	case 0x04u: //INC B Increment B. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&B);
		return 4;
	case 0x05u: //DEC B Decrement B. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		B -= 1;
		F.set(FLAG_ZERO, B == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (B & 0xFu) == 0xFu);
		return 4;
	case 0x06u: //LD B,n Load an 8-bit immediate n into B
		B = m->readByte(PC++);
		return 8;
	case 0x07u: //RLCA Rotate A left. Flags: set Z if zero, N and H reset, C contains the old bit 7
//		F.set(FLAG_C, A > 0x7Fu);
//		A = ((A << 1) & 0xFFu) | (A >> 7);
//		F.set(FLAG_ZERO, A == 0);
//		F.set(FLAG_SUB, false);
//		F.set(FLAG_HC, false);
		RLC(&A);
		return 4;
	case 0x08u: //LD (nn),SP Load value at SP into 16-bit immediate address (nn)
		nn = m->readByte(PC++);
		nn |= (m->readByte(PC++) << 8);
		m->writeByte(nn, SP & 0xFFu);
		m->writeByte(nn + 1, SP >> 8);
		return 20;
	case 0x09u: //ADD HL,BC Add BC to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		ADDHL(R_BC);
		return 8;
	case 0x0Au: //LD A,(BC) load value at (BC) into A
		A = m->readByte(R_BC);
		return 8;
	case 0x0Bu: //DEC BC Decrement BC
		temp = R_BC - 1;
		B = temp >> 8;
		C = temp & 0xFFu;
		return 8;
	case 0x0Cu: //INC C Increment C. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&C);
		return 4;
	case 0x0Du: //DEC C Decrement C. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		C -= 1;
		F.set(FLAG_ZERO, C == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (C & 0xFu) == 0xFu);
		return 4;
	case 0x0Eu: //LD C,n Load an 8-bit immediate n into C
		C = m->readByte(PC++);
		return 8;
	case 0x0Fu: //RRCA Rotate A right. Flags:Z - set if result is zero; N,H - reset; C - contains old bit 0;
//		F.set(FLAG_C, A & 0x1u);
//		A = (A >> 1) | ((A & 0x1u) << 7);
//		F.set(FLAG_ZERO, A == 0);
//		F.set(FLAG_SUB, false);
//		F.set(FLAG_HC, false);
		RRC(&A);
		return 4;
	case 0x10u: //STOP Halt CPU and LCD display until button pressed. Check for additional 0x00u after the opcode
		handleStop();
		return 4;
	case 0x11u: //LD DE,nn Load a 16-bit immediate nn into DE
		E = m->readByte(PC++);
		D = m->readByte(PC++);
		return 12;
	case 0x12u: //LD (DE),A Load value at A into (DE)
		m->writeByte(R_DE, A);
		return 8;
	case 0x13u: //INC DE Increment DE
		temp = R_DE + 1;
		D = temp >> 8 & 0xFFu;
		E = temp & 0xFFu;
		return 8;
	case 0x14u: //INC D Increment D. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&D);
		return 4;
	case 0x15u: //DEC D Decrement D. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		D -= 1;
		F.set(FLAG_ZERO, D == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (D & 0xFu) == 0xFu);
		return 4;
	case 0x16u: //LD D,n Load an 8-bit immediate n into D
		D = m->readByte(PC++);
		return 8;
	case 0x17u: //RLA Rotate A left through Carry Flag. Flags: Z - set if result is zero; N,H - reset; C - contains old bit 7
//		temp = (F[FLAG_C]) ? 1 : 0; //old carry to go to A's bit one
//		F.set(FLAG_C, A > 0x7Fu);
//		A = ((A << 1) & 0xFFu) | (temp & 0x1u);
//		F.set(FLAG_ZERO, A == 0);
//		F.set(FLAG_SUB, false);
//		F.set(FLAG_HC, false);
		RL(&A);
		return 4;
	case 0x18u: //JR n Jump to PC+n where n is an 8-bit immediate
		PC += ((_int8)m->readByte(PC++));
		return 12;
	case 0x19u: //ADD HL,DE Add DE to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		ADDHL(R_DE);
		return 8;
	case 0x1Au: //LD A,(DE) Load value at (DE) to A
		A = m->readByte(R_DE);
		return 8;
	case 0x1Bu: //DEC DE Decrement DE
		temp = R_DE - 1;
		D = temp >> 8;
		E = temp & 0xFFu;
		return 8;
	case 0x1Cu: //INC E Increment E. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&E);
		return 4;
	case 0x1Du: //DEC E Decrement E. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		E -= 1;
		F.set(FLAG_ZERO, E == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (E & 0xFu) == 0xFu);
		return 4;
	case 0x1Eu: //LD E,n Load an 8-bit immediate n into E
		E = m->readByte(PC++);
		return 8;
	case 0x1Fu: //RRA Rotate A right through Carry flag. Flags:Z - set if result is zero; N,H - reset; C - contains old bit 0
//		temp = (F[FLAG_C]) ? 0x80u : 0; //old carry to go to A's bit seven
//		F.set(FLAG_C, A & 0x01u);
//		A = ((A >> 1) & 0x7Fu) | temp;
//		F.set(FLAG_ZERO, A == 0);
//		F.set(FLAG_SUB, false);
//		F.set(FLAG_HC, false);
		RR(&A);
		return 4;
	case 0x20u: //JR NZ,n Jump to PC+n if Z flag == 0
		if (!F[FLAG_ZERO]){
			PC += ((_int8)m->readByte(PC++));
			return 12;
		}
		else{
			PC++;
			return 8;
		}
	case 0x21u: //LD HL,nn Load a 16-bit immediate nn into HL
		L = m->readByte(PC++);
		H = m->readByte(PC++);
		return 12;
	case 0x22u: //LDI (HL),A Load value at A into (HL), increment HL
		m->writeByte(R_HL, A);
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		return 8;
	case 0x23u: //INC HL Increment HL
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		return 8;
	case 0x24u: //INC H Increment H. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&H);
		return 4;
	case 0x25u: //DEC H Decrement H. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		H -= 1;
		F.set(FLAG_ZERO, H == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (H & 0xFu) == 0xFu);
		return 4;
	case 0x26u: //LD H,n Load an 8-bit immediate n into H
		H = m->readByte(PC++);
		return 8;
	case 0x27u: //DAA Decimal adjust A. Flags:Z - set if A is zero; H - reset; C - set or reset, depending on operation
		if (!F[FLAG_SUB]){
			if (F[FLAG_C] || A > 0x99u){
				A = (A + 0x60u) & 0xFFu;
				F.set(FLAG_C, true);
			}
			if (F[FLAG_HC] || (A & 0x0Fu) > 0x09u){
				A = (A + 0x06u) & 0xFFu;
				F.set(FLAG_HC, false);
			}
		}
		else if (F[FLAG_C] && F[FLAG_HC]){
			A = (A = 0x9Au) & 0xFFu;
			F.set(FLAG_HC, false);
		}
		else if (F[FLAG_C]){
			A = (A + 0xA0u) & 0xFFu;
		}
		else if (F[FLAG_HC]){
			A = (A + 0xFAu) & 0xFFu;
			F.set(FLAG_HC, false);
		}
		F.set(FLAG_ZERO, A == 0);
		return 4;
	case 0x28u: //JR Z,n Jump to PC+n if Z flag == 1
		if (F[FLAG_ZERO]){
			PC += ((_int8) m->readByte(PC++));
			return 12;
		}
		else{
			PC++;
			return 8;
		}
	case 0x29u: //ADD HL,HL Add HL to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		ADDHL(R_HL);
		return 8;
	case 0x2Au: //LDI A,(HL) Load value at (HL) into A, increment HL
		A = m->readByte(R_HL);
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		return 8;
	case 0x2Bu: //DEC HL Decrement HL
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		return 8;
	case 0x2Cu: //INC L Increment L. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		INC(&L);
		return 4;
	case 0x2Du: //DEC L Decrement L. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		L -= 1;
		F.set(FLAG_ZERO, L == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (L & 0xFu) == 0xFu);
		return 4;
	case 0x2Eu: //LD L,n Load an 8-bit immediate n into L
		L = m->readByte(PC++);
		return 8;
	case 0x2Fu: //CPL Compliment A (flip all bits). Flags:N,H - set
		A = ~A;
		F.set(FLAG_HC, true);
		F.set(FLAG_SUB, true);
		return 4;
	case 0x30u: //JR NC,n Jump to PC+n if C flag == 0
		if (!F[FLAG_C]){
			PC += ((_int8)m->readByte(PC++));
			return 12;
		}
		else{
			PC++;
			return 8;
		}
	case 0x31u: //LD SP,nn Load a 16-bit immediate nn into SP
		//SP = m->readByte(PC++) | (m->readByte(PC++) << 8);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		SP = nn;
		return 12;
	case 0x32u: //LDD (HL),A Put A into (HL), decrement HL
		m->writeByte(R_HL, A);
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		return 8;
	case 0x33u: //INC SP Increment SP
		SP++;
		return 8;
	case 0x34u: //INC (HL) Increment (HL). Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
	{
		uint8 n = m->readByte(R_HL);
		INC(&n);
		m->writeByte(R_HL, n);
	}
		return 12;
	case 0x35u: //DEC (HL) Decrement (HL). Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		m->writeByte(R_HL, m->readByte(R_HL) - 1);
		F.set(FLAG_ZERO, m->readByte(R_HL) == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (m->readByte(R_HL) & 0xFu) == 0xFu);
		return 12;
	case 0x36u: //LD (HL),n Load an 8-bit immediate n into (HL)
		m->writeByte(R_HL, m->readByte(PC++));
		return 12;
	case 0x37u: //SCF Set carry flag. Flags:N,H - reset; C - set.
		F.set(FLAG_C, true);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		return 4;
	case 0x38u: //JR C,n Jump to PC+n if C flag == 1
		if (F[FLAG_C]){
			PC += ((_int8)m->readByte(PC++));
			return 12;
		}
		else{
			PC++;
			return 8;
		}
	case 0x39u: //ADD HL,SP Add SP to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		ADDHL(SP);
		return 8;
	case 0x3Au: //LDD A,(HL) Put value at (HL) into A, decrement HL
		A = m->readByte(R_HL);
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		return 8;
	case 0x3Bu: //DEC SP Decrement SP
		SP--;
		return 8;
	case 0x3Cu: //INC A Increment A. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		A += 1;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (A & 0xFu) == 0);
		return 4;
	case 0x3Du: //DEC A Decrement A. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		A -= 1;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (A & 0xFu) == 0xFu);
		return 4;
	case 0x3Eu: //LD A,n Load 8-bit immediate n into A
		A = m->readByte(PC++);
		return 8;
	case 0x3Fu: //CCF Complement the carry flag.Flags:N,H- reset; C-complemented
		F.set(FLAG_C, !F[FLAG_C]);
		F.reset(FLAG_SUB);
		F.reset(FLAG_HC);
		return 4;
	case 0x40u: //LD B,B Load value at B into B (do nothing)
		return 4;
	case 0x41u: //LD B,C Load value at C into B
		B = C;
		return 4;
	case 0x42u: //LD B,D Load value at D into B
		B = D;
		return 4;
	case 0x43u: //LD B,E Load value at E into B
		B = E;
		return 4;
	case 0x44u: //LD B,H Load value at H into B
		B = H;
		return 4;
	case 0x45u: //LD B,L Load value at L into B
		B = L;
		return 4;
	case 0x46u: //LD B,(HL) Load value at (HL) into B
		B = m->readByte(R_HL);
		return 8;
	case 0x47u: //LD B,A Load value at A into B
		B = A;
		return 4;
	case 0x48u: //LD C,B Load value at B into C
		C = B;
		return 4;
	case 0x49u: //LD C,C Load value at C into C (do nothing)
		return 4;
	case 0x4A4u: //LD C,D Load value at D into C
		C = D;
		return 4;
	case 0x4Bu: //LD C,E Load value at E into C
		C = E;
		return 4;
	case 0x4Cu: //LD C,H Load value at H into C
		C = H;
		return 4;
	case 0x4Du: //LD C,L Load value at L into C
		C = L;
		return 4;
	case 0x4Eu: //LD C,(HL) Load value at (HL) into C
		C = m->readByte(R_HL);
		return 8;
	case 0x4Fu: //LD C,A Load value at A into C
		C = A;
		return 4;
	case 0x50u: //LD D,B Load value at B into D
		D = B;
		return 4;
	case 0x51u: //LD D,C Load value at C into D
		D = C;
		return 4;
	case 0x52u: //LD D,D Load value at D into D (do nothing)
		return 4;
	case 0x53u: //LD D,E Load value at E into D
		D = E;
		return 4;
	case 0x54u: //LD D,H Load value at H into D
		D = H;
		return 4;
	case 0x55u: //LD D,L Load value at L into D
		D = L;
		return 4;
	case 0x56u: //LD D,(HL) Load value at (HL) into D
		D = m->readByte(R_HL);
		return 8;
	case 0x57u: //LD D,A Load value at A into D
		D = A;
		return 4;
	case 0x58u: //LD E,B Load value at B into E
		E = B;
		return 4;
	case 0x59u: //LD E,C Load value at C into E
		E = C;
		return 4;
	case 0x5Au: //LD E,D Load value at D into E
		E = D;
		return 4;
	case 0x5Bu: //LD E,E Load value at E into E (do nothing)
		return 4;
	case 0x5Cu: //LD E,H Load value at H into E
		E = H;
		return 4;
	case 0x5Du: //LD E,L Load value at L into E
		E = L;
		return 4;
	case 0x5Eu: //LD E,(HL) Load value at (HL) into E
		E = m->readByte(R_HL);
		return 8;
	case 0x5Fu: //LD E,A Load value at A into E
		E = A;
		return 4;
	case 0x60u: //LD H,B Load value at B into H
		H = B;
		return 4;
	case 0x61u: //LD H,C Load value at C into H
		H = C;
		return 4;
	case 0x62u: //LD H,D Load value at D into H
		H = D;
		return 4;
	case 0x63u: //LD H,E Load value at E into H
		H = E;
		return 4;
	case 0x64u: //LD H,H Load value at H into H (do nothing)
		return 4;
	case 0x65u: //LD H,L Load value at L into H
		H = L;
		return 4;
	case 0x66u: //LD H,(HL) Load value at (HL) into H
		H = m->readByte(R_HL);
		return 8;
	case 0x67u: //LD H,A Load value at A into H
		H = A;
		return 4;
	case 0x68u: //LD L,B Load value at B into L
		L = B;
		return 4;
	case 0x69u: //LD L,C Load value at C into L
		L = C;
		return 4;
	case 0x6Au: //LD L,D Load value at D into L
		L = D;
		return 4;
	case 0x6Bu: //LD L,E Load value at E into L
		L = E;
		return 4;
	case 0x6Cu: //LD L,H Load value at H into L
		L = H;
		return 4;
	case 0x6Du: //LD L,L Load value at L into L (do nothing)
		return 4;
	case 0x6Eu: //LD L,(HL) Load value at (HL) into L
		L = m->readByte(R_HL);
		return 8;
	case 0x6Fu: //LD L,A Load value at A into L
		L = A;
		return 4;
	case 0x70u: //LD (HL),B Load value at B into (HL)
		m->writeByte(R_HL, B);
		return 8;
	case 0x71u: //LD (HL),C Load value at C into (HL)
		m->writeByte(R_HL, C);
		return 8;
	case 0x72u: //LD (HL),D Load value at D into (HL)
		m->writeByte(R_HL, D);
		return 8;
	case 0x73u: //LD (HL),E Load value at E into (HL)
		m->writeByte(R_HL, E);
		return 8;
	case 0x74u: //LD (HL),H Load value at H into (HL)
		m->writeByte(R_HL, H);
		return 8;
	case 0x75u: //LD (HL),L Load value at L into (HL)
		m->writeByte(R_HL, L);
		return 8;
	case 0x76u: //HALT Power down CPU until an interrupt occurs.
		handleHalt();
		return 4;
	case 0x77u: //LD (HL),A Load value at A into (HL)
		m->writeByte(R_HL, A);
		return 8;
	case 0x78u: //LD A,B Load value of B into A
		A = B;
		return 4;
	case 0x79u: //LD A,C Load value of C into A
		A = C;
		return 4;
	case 0x7Au: //LD A,D Load value of D into A
		A = D;
		return 4;
	case 0x7Bu: //LD A,E Load value of E into A
		A = E;
		return 4;
	case 0x7Cu: //LD A,H Load value of H into A
		A = H;
		return 4;
	case 0x7Du: //LD A,L Load value of L into A
		A = L;
		return 4;
	case 0x7Eu: //LD A,(HL) Load value of (HL) into A
		A = m->readByte(R_HL);
		return 8;
	case 0x7Fu: //LD A,A Load value of A into A (do nothing)
		return 4;
	case 0x80u: //ADD A,B Add B to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&B);
		return 4;
	case 0x81u: //ADD A,C Add C to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&C);
		return 4;
	case 0x82u: //ADD A,D Add D to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&D);
		return 4;
	case 0x83u: //ADD A,E Add E to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&E);
		return 4;
	case 0x84u: //ADD A,H Add H to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&H);
		return 4;
	case 0x85u: //ADD A,L Add L to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&L);
		return 4;
	case 0x86u: //ADD A,(HL) Add (HL) to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp2 = m->readByte(R_HL);
		ADD(&temp2);
		return 8;
	case 0x87u: //ADD A,A Add A to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADD(&A);
		return 4;
	case 0x88u: //ADC A,B Add B + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&B);
		return 4;
	case 0x89u: //ADC A,C Add C + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&C);
		return 4;
	case 0x8Au: //ADC A,D Add D + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&D);
		return 4;
	case 0x8Bu: //ADC A,E Add E + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&E);
		return 4;
	case 0x8Cu: //ADC A,H Add H + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&H);
		return 4;
	case 0x8Du: //ADC A,L Add L + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&L);
		return 4;
	case 0x8Eu: //ADC A,(HL) Add (HL) + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp2 = m->readByte(R_HL);
		ADC(&temp2);
		return 8;
	case 0x8Fu: //ADC A,A Add A + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		ADC(&A);
		return 4;
	case 0x90u: //SUB B Subtract B from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&B);
		return 4;
	case 0x91u: //SUB C Subtract C from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&C);
		return 4;
	case 0x92u: //SUB D Subtract D from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&D);
		return 4;
	case 0x93u: //SUB E Subtract E from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&E);
		return 4;
	case 0x94u: //SUB H Subtract H from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&H);
		return 4;
	case 0x95u: //SUB L Subtract L from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&L);
		return 4;
	case 0x96u: //SUB (HL) Subtract (HL) from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp2 = m->readByte(R_HL);
		SUB(&temp2);
		return 8;
	case 0x97u: //SUB A Subtract A from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SUB(&A);
		return 4;
	case 0x98u: //SBC B Subtract B plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&B);
		return 4;
	case 0x99u: //SBC C Subtract C plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&C);
		return 4;
	case 0x9Au: //SBC D Subtract D plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&D);
		return 4;
	case 0x9Bu: //SBC E Subtract E plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&E);
		return 4;
	case 0x9Cu: //SBC H Subtract H plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&H);
		return 4;
	case 0x9Du: //SBC L Subtract L plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&L);
		return 4;
	case 0x9Eu: //SBC A Subtract (HL) plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp2 = m->readByte(R_HL);
		SBC(&temp2);
		return 8;
	case 0x9Fu: //SBC A Subtract A plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		SBC(&A);
		return 4;
	case 0xA0u: //AND B Logical AND B and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= B;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA1u: //AND C Logical AND C and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= C;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA2u: //AND D Logical AND D and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= D;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA3u: //AND E Logical AND E and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= E;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA4u: //AND H Logical AND H and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= H;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA5u: //AND L Logical AND L and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= L;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA6u: //AND (HL) Logical AND (HL) and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xA7u: //AND A Logical AND A and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA8u: //XOR B Logical XOR B and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= B;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xA9u: //XOR C Logical XOR C and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= C;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xAAu: //XOR D Logical XOR D and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= D;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xABu: //XOR E Logical XOR E and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= E;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xACu: //XOR H Logical XOR H and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= H;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xADu: //XOR L Logical XOR L and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= L;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xAEu: //XOR (HL) Logical XOR (HL) and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xAFu: //XOR A Logical XOR A and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A = 0;
		F.set(FLAG_ZERO);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB0u: //OR B Logical OR B and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= B;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB1u: //OR C Logical OR C and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= C;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB2u: //OR D Logical OR D and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= D;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB3u: //OR E Logical OR E and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= E;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB4u: //OR H Logical OR H and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= H;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB5u: //OR L Logical OR L and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= L;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB6u: //OR (HL) Logical OR (HL) and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xB7u: //OR A Logical OR A and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 4;
	case 0xB8u: //CP B Compare A and B. Flags: Z - set if A == B; N - Set; H - set if no borrow from bit 4; C - Set if A < B; 
		signedTemp = A - B;
		F.set(FLAG_HC, (signedTemp & 0xFu) > (A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xB9u: //CP C Compare A and C. Flags: Z - set if A == C; N - Set; H - set if no borrow from bit 4; C - Set if A < C; 
		signedTemp = A - C;
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xBAu: //CP D Compare A and D. Flags: Z - set if A == D; N - Set; H - set if no borrow from bit 4; C - Set if A < D;  
		signedTemp = A - D;
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xBBu: //CP E Compare A and E. Flags: Z - set if A == E; N - Set; H - set if no borrow from bit 4; C - Set if A < E;  
		signedTemp = A - E;
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xBCu: //CP H Compare A and H. Flags: Z - set if A == H; N - Set; H - set if no borrow from bit 4; C - Set if A < H;  
		signedTemp = A - H;
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xBDu: //CP L Compare A and L. Flags: Z - set if A == L; N - Set; H - set if no borrow from bit 4; C - Set if A < L;  
		signedTemp = A - L;
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 4;
	case 0xBEu: //CP (HL) Compare A and (HL). Flags: Z - set if A == (HL); N - Set; H - set if no borrow from bit 4; C - Set if A < (HL);  
		signedTemp = A - m->readByte(R_HL);
		F.set(FLAG_HC, (signedTemp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 8;
	case 0xBFu: //CP A Compare A and A. Flags: Z - set if A == A; N - Set; H - set if no borrow from bit 4; C - Set if A < A; 
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.set(FLAG_ZERO);
		F.set(FLAG_SUB);
		return 4;
	case 0xC0u: //RET NZ Return if Z flag == 0
		if (!F[FLAG_ZERO]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
			return 20;
		}
		return 8;
	case 0xC1u: //POP BC, Pop 16-bits off of the stack into BC, increment SP twice
		C = m->readByte(SP);
		B = m->readByte(SP + 1);
		SP += 2;
		return 12;
	case 0xC2u: //JP NZ,nn Jump to address given by 16-bit immediate nn if Z flag == 0
		if (!F[FLAG_ZERO]){
			//postfix in C++ is undefined, PC may not increment between readByte calls postfix only is guaranteed to occur after the line
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8); 
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
			return 16;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xC3u: //JP nn Jump to address given by 16-bit immediate nn. LS byte first
		//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		if (nn == 0xDEF8){
			//debugging
			PC = nn;
		}
		PC = nn;
		return 16;
	case 0xC4u: //CALL NZ,nn Call nn if Z flag == 0
		if (!F[FLAG_ZERO]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
			return 24;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xC5u: //PUSH BC Push BC onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, B);
		SP -= 1;
		m->writeByte(SP, C);
		return 16;
	case 0xC6u: //ADD A,n Add 8-bit immediate to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(PC++);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		return 8;
	case 0xC7u: //RST 00H Push PC onto the stack, jump to 0000
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0;
		return 16;
	case 0xC8u: //RET Z Return if Z flag == 1
		if (F[FLAG_ZERO]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
			return 20;
		}
		return 8;
	case 0xC9u: //RET Pop two bytes from stack and jump to the address given by them
		PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
		SP += 2;
		return 16;
	case 0xCAu: //JP Z,nn Jump to address given by 16-bit immediate nn if Z flag == 1
		if (F[FLAG_ZERO]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
			return 16;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xCBu: //CB prefix, call CB(OPCODE) where OPCODE is the next 8-bits after CB
		temp2 = m->readByte(PC++);
		CB(temp2);
		
		//All CB operations take 8 cycles, except for those ending in 6 and E, which take 16 cycles. CB itself takes 4 cycles.
		if ((temp2 & 0x0F) == 0x06 || (temp2 & 0x0F) == 0x0E){
			return 20;
		}
		else{
			return 12;
		}
	case 0xCCu: //CALL Z,nn Call nn if Z flag == 1
		if (F[FLAG_ZERO]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
			return 24;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xCDu: //CALL nn Push address of next instruction onto the stack and then jump to nn where nn is a 16-bit immediate (LS byte first)
		temp = m->readByte(PC++);
		temp |= (m->readByte(PC++) << 8);
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = temp & 0xFFFFu;
		return 24;
	case 0xCEu: //ADC A,n Add 8-bit immediate n + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(PC++) + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		return 8;
	case 0xCFu: //RST 08H Push PC onto the stack, jump to 0x0008u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x8u;
		return 16;
	case 0xD0u: //RET NC return if C flag == 0
		if (!F[FLAG_C]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
			return 20;
		}
		return 8;
	case 0xD1u: //POP DE, Pop 16-bits off of the stack into DE, increment SP twice
		E = m->readByte(SP);
		D = m->readByte(SP + 1);
		SP += 2;
		return 12;
	case 0xD2u: //JP NC,nn Jump to address given by 16-bit immediate nn if C flag == 0
		if (!F[FLAG_C]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
			return 16;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xD3u: //Illegal opcode, halt execution (0xD3u)
		printf("Illegal OP: 0xD3u at PC: %u", PC);
		return -1;
	case 0xD4u: //CALL NC,nn Call nn if C flag == 0
		if (!F[FLAG_C]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
			return 24;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xD5u: //PUSH DE Push DE onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, D);
		SP -= 1;
		m->writeByte(SP, E);
		return 16;
	case 0xD6u: //SUB n Subtract 8-bit immediate n from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
	{
		uint8 n = m->readByte(PC++);
		F.set(FLAG_HC, (A & 0xFu) < (n & 0xFu));
		F.set(FLAG_C, A < n);
		A = (A-n) & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, true);
	}
		return 8;
	case 0xD7u: //RST 10H Push PC onto the stack, jump to 0x0010u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x10u;
		return 16;
	case 0xD8u: //RET C Return if C flag == 1
		if (F[FLAG_C]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
			return 20;
		}
		return 8;
	case 0xD9u: //RETI Pop two bytes off the stack and jump to that address, enable interrupts
		PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
		SP += 2;
		interruptEnabled = true;
		return 16;
	case 0xDAu: //JP C,nn Jump to address given by 16-bit immediate if C flag == 1
		if (F[FLAG_C]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
			return 16;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xDBu: //Illegal opcode, halt execution (0xDBu)
		printf("Illegal OP: 0xDBu at PC: %u", PC);
		return -1;
	case 0xDCu: //CALL C,nn Call nn if C flag == 1
		if (F[FLAG_C]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
			return 24;
		}
		else{
			PC += 2;
			return 12;
		}
	case 0xDDu: //Illegal opcode, halt execution (0xDDu)
		printf("Illegal OP: 0xDDu at PC: %u", PC);
		return -1;
	case 0xDEu: //SBC n Subtract 8-bit immediate n plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		signedTemp = A - m->readByte(PC++) - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (signedTemp & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		A = signedTemp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, true);
		return 8;
	case 0xDFu: //RST 18H Push PC onto the stack, jump to 0x0018u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x18u;
		return 16;
	case 0xE0: //LDH n,A Load A into address given by $FF00 + n where n is an 8-bit immediate
		m->writeByte(0xFF00u + m->readByte(PC++), A);
		return 12;
	case 0xE1u: //POP HL, Pop 16-bits off of the stack into HL, increment SP twice
		L = m->readByte(SP);
		H = m->readByte(SP + 1);
		SP += 2;
		return 12;
	case 0xE2u: //LD (C),A Load A into address $FF00 + C
		m->writeByte(0xFF00u + C, A);
		return 8;
	case 0xE3u: //Illegal opcode, halt execution (0xE3u)
		printf("Illegal OP: 0xE3u at PC: %u", PC);
		return -1;
	case 0xE4u: //Illegal opcode, halt execution (0xE4u)
		printf("Illegal OP: 0xE4u at PC: %u", PC);
		return -1;
	case 0xE5u: //PUSH HL Push HL onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, H);
		SP -= 1;
		m->writeByte(SP, L);
		return 16;
	case 0xE6u: //AND n Logical AND 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xE7u: //RST 20H Push PC onto the stack, jump to 0x0020u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x20u;
		return 16;
	case 0xE8u: //ADD SP,n Add 8-bit immediate to SP.Flags:Z - reset; N - reset; H,C - set or reset according to operation
	{
		_int8 n = (_int8)m->readByte(PC++);
		uint16 val = (SP + n) & 0xFFFF;
		F.reset();
		unsigned int test = SP + n;
		F.set(FLAG_HC, ((SP & 0xF) + (n & 0xF)) > 0xF);
		F.set(FLAG_C, test > 0xFFFF);
		SP = val;
	}
		return 16;
	case 0xE9u: //JP HL Jump to address in HL.
		PC = R_HL;
		return 4;
	case 0xEAu: //LD (nn),A Load value at A into 16-bit immediate address (nn)
		//m->writeByte(m->readByte(m->readByte(PC) | (m->readByte(PC+1) << 8)), A);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		m->writeByte(nn, A);
		return 16;
	case 0xEBu: //Illegal opcode, halt execution (0xEBu)
		printf("Illegal OP: 0xEBu at PC: %u", PC);
		return -1;
	case 0xECu: //Illegal opcode, halt execution (0xECu)
		printf("Illegal OP: 0xECu at PC: %u", PC);
		return -1;
	case 0xEDu: //Illegal opcode, halt execution (0xEDu)
		printf("Illegal OP: 0xEDu at PC: %u", PC);
		return -1;
	case 0xEEu: //XOR n Logical XOR 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xEFu: //RST 28H Push PC onto the stack, jump to 0x0028u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x28u;
		return 16;
	case 0xF0u: //LDH A,(n) Load value at $FF00 + n into A where n is an 8-bit immediate
		A = m->readByte(0xFF00u + m->readByte(PC++));
		return 12;
	case 0xF1u: //POP AF, Pop 16-bits off of the stack into AF, increment SP twice
		temp = m->readByte(SP);
		F.set(FLAG_ZERO, temp > 0x7Fu);
		F.set(FLAG_SUB, (temp & 0x40u) == 0x40u);
		F.set(FLAG_HC, (temp & 0x20u) == 0x20u);
		F.set(FLAG_C, (temp & 0x10u) == 0x10u);
		A = m->readByte(SP + 1);
		SP += 2;
		return 12;
	case 0xF2u: //LD A,(C) Load value at address $FF00 + C into A
		A = m->readByte(0xFF00u + C);
		return 8;
	case 0xF3u: //DI Disable interrupts after this instruction is executed
		interruptEnabled = false;
		return 4;
	case 0xF4u: //Illegal opcode, halt execution (0xF4u)
		printf("Illegal OP: 0xF4u at PC: %u", PC);
		return -1;
	case 0xF5u: //PUSH AF Push AF onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, A);
		SP -= 1;
		m->writeByte(SP, ((F[FLAG_ZERO]) ? 0x80u : 0) | ((F[FLAG_SUB]) ? 0x40u : 0) | ((F[FLAG_HC]) ? 0x20u : 0) | ((F[FLAG_C]) ? 0x10u : 0));
		return 16;
	case 0xF6u: //OR n Logical OR 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		return 8;
	case 0xF7u: //RST 30H Push PC onto the stack, jump to 0x0030u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x30u;
		return 16;
	case 0xF8u: //LDHL SP,n Load value SP + n into HL where n is an 8-bit immediate. flags: Z,N - reset; H,C set according to operation
	{
		_int8 n = (_int8)m->readByte(PC++);
		nn = (n + SP) & 0xFFFFu;
		L = nn & 0xFFu;
		H = (nn >> 8) & 0xFFu;

		unsigned int test = n + SP;
		F.reset();
		F.set(FLAG_C, test > 0xFFFF);
		F.set(FLAG_HC, ((SP & 0xF) + (n & 0xF)) > 0xF);
	}
		return 12;
	case 0xF9u: //LD SP,HL Load value at HL into SP
		SP = R_HL;
		return 8;
	case 0xFAu: //LD A,(nn) Load value of 16-bit immediate address (nn) into A
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		A = m->readByte(nn);
		return 16;
	case 0xFBu: //EI Enable interrupts after this instruction is executed
		interruptEnabled = true;
		return 4;
	case 0xFCu: //Illegal opcode, halt execution (0xFCu)
		printf("Illegal OP: 0xFCu at PC: %u", PC);
		return -1;
	case 0xFDu: //Illegal opcode, halt execution (0xFDu)
		printf("Illegal OP: 0xFDu at PC: %u", PC);
		return -1;
	case 0xFEu: //CP n Compare A and 8-bit immediate n. Flags: Z - set if A == n; N - Set; H - set if no borrow from bit 4; C - Set if A < n; 
		signedTemp = A - m->readByte(PC++);
		F.set(FLAG_HC, (signedTemp & 0xFu) > (A & 0xFu));
		F.set(FLAG_C, signedTemp < 0);
		F.set(FLAG_ZERO, signedTemp == 0);
		F.set(FLAG_SUB);
		return 8;
	case 0xFFu: //RST 38H, Push PC onto the stack, jump to 0x0038u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x38u;
		return 16;
	default:
		return -1; //not reachable, just here to suppress warnings
	}
}

//each operation has two variants:
//one that directly modifies the register 
//the other is for modifying contents of memory where a value is returned, not a pointer

void RLC(uint8* reg){
	F.reset();
	if ((*reg & 0x80u) != 0){
		F.set(FLAG_C);
		*reg <<= 1;
		*reg |= 0x1u;
	}
	else{
		*reg <<= 1;
	}
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 RLC(uint8 reg){
	F.reset();
	if ((reg & 0x80u) != 0){
		F.set(FLAG_C);
		reg <<= 1;
		reg |= 0x1u;
	}
	else{
		reg <<= 1;
	}
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void RRC(uint8* reg){
	F.reset();
	if ((*reg & 0x1u) != 0){
		F.set(FLAG_C);
		*reg >>= 1;
		*reg |= 0x80u;
	}
	else{
		*reg >>= 1;
	}
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 RRC(uint8 reg){
	F.reset();
	if ((reg & 0x1u) != 0){
		F.set(FLAG_C);
		reg >>= 1;
		reg |= 0x80u;
	}
	else{
		reg >>= 1;
	}
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void RL(uint8* reg){
	uint8 carry = F[FLAG_C];
	F.reset();
	if ((*reg & 0x80u) != 0){
		F.set(FLAG_C);
	}
	*reg <<= 1;
	*reg |= carry;
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 RL(uint8 reg){
	uint8 carry = F[FLAG_C];
	F.reset();
	if ((reg & 0x80u) != 0){
		F.set(FLAG_C);
	}
	reg <<= 1;
	reg |= carry;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void RR(uint8* reg){
	uint8 carry = F[FLAG_C] ? 0x80u : 0x0u;
	F.reset();
	if ((*reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	*reg >>= 1;
	*reg |= carry;
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 RR(uint8 reg){
	uint8 carry = F[FLAG_C] ? 0x80u : 0x0u;
	F.reset();
	if ((reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	reg >>= 1;
	reg |= carry;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void SLA(uint8* reg){	//Shift Left through carry, set LSB to 0
	F.reset();
	if ((*reg & 0x80u) != 0){
		F.set(FLAG_C);
	}
	*reg <<= 1;
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 SLA(uint8 reg){	//Shift Left through carry, set LSB to 0
	F.reset();
	if ((reg & 0x80u) != 0){
		F.set(FLAG_C);
	}
	reg <<= 1;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void SRA(uint8* reg){	//Shift right through carry, MSB doesn't change
	F.reset();
	if ((*reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	_int8 MSB = *reg & 0x80u;
	*reg >>= 1;
	*reg |= MSB;
	F.set(FLAG_ZERO, (*reg) == 0);
}
uint8 SRA(uint8 reg){	//Shift right through carry, MSB doesn't change
	F.reset();
	if ((reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	_int8 MSB = reg & 0x80u;
	reg >>= 1;
	reg |= MSB;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void SRL(uint8* reg){	//Shift right through carry, MSB set to 0
	F.reset();
	if ((*reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	*reg >>= 1;
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 SRL(uint8 reg){	//Shift right through carry, MSB set to 0
	F.reset();
	if ((reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	reg >>= 1;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void SWAP(uint8* reg){	//Swap the upper and lower halves of the register
	F.reset();
	_int8 lower = (*reg & 0xF0u) >> 4;	//new lower-half, old upper-half
	_int8 upper = (*reg & 0x0Fu) << 4;
	*reg = lower + upper;
	F.set(FLAG_ZERO, *reg == 0);
}
uint8 SWAP(uint8 reg){	//Swap the upper and lower halves of the register
	F.reset();
	_int8 lower = (reg & 0xF0u) >> 4;	//new lower-half, old upper-half
	_int8 upper = (reg & 0x0Fu) << 4;
	reg = lower + upper;
	F.set(FLAG_ZERO, reg == 0);
	return reg;
}

void BIT(int n, uint8* reg){	//set Z flag if bit n of register is 0; C is unaffected
	F.set(FLAG_ZERO, (*reg & (0x1u << n)) == 0);
	F.set(FLAG_HC);
	F.reset(FLAG_SUB);
}
void BIT(int n, uint8 reg){	//set Z flag if bit n of register is 0; C is unaffected
	F.set(FLAG_ZERO, (reg & (0x1u << n)) == 0);
	F.set(FLAG_HC);
	F.reset(FLAG_SUB);
}

void RES(int n, uint8* reg){	//set bit n of register to 0
	*reg &= (~(0x1u << n));
}
uint8 RES(int n, uint8 reg){	//set bit n of register to 0
	reg &= (~(0x1u << n));
	return reg;
}

void SET(int n, uint8* reg){
	*reg |= (0x1u << n);
}
uint8 SET(int n, uint8 reg){
	reg |= (0x1u << n);
	return reg;
}


void CB(uint8 code){
	switch (code){
	case 0x0u:	//RLC B
		RLC(&B);
		break;
	case 0x1u:	//RLC C
		RLC(&C);
		break;
	case 0x2u:	//RLC D
		RLC(&D);
		break;
	case 0x3u:	//RLC E
		RLC(&E);
		break;
	case 0x4u:	//RLC H
		RLC(&H);
		break;
	case 0x5u:	//RLC L
		RLC(&L);
		break;
	case 0x6u:	//RLC (HL)
		m->writeByte(R_HL,RLC(m->readByte(R_HL)));
		break;
	case 0x7u:	//RLC A
		RLC(&A);
		break;
	case 0x8u:	//RRC B
		RRC(&B);
		break;
	case 0x9u:	//RRC C
		RRC(&C);
		break;
	case 0xAu:	//RRC D
		RRC(&D);
		break;
	case 0xBu:	//RRC E
		RRC(&E);
		break;
	case 0xCu:	//RRC H
		RRC(&H);
		break;
	case 0xDu:	//RRC L
		RRC(&L);
		break;
	case 0xEu:	//RRC (HL)
		m->writeByte(R_HL, RRC(m->readByte(R_HL)));
		break;
	case 0xFu:	//RRC A
		RRC(&A);
		break;
	case 0x10u:	//RL B
		RL(&B);
		break;
	case 0x11u:	//RL C
		RL(&C);
		break;
	case 0x12u:	//RL D
		RL(&D);
		break;
	case 0x13u:	//RL E
		RL(&E);
		break;
	case 0x14u:	//RL H
		RL(&H);
		break;
	case 0x15u:	//RL L
		RL(&L);
		break;
	case 0x16u:	//RL (HL)
		m->writeByte(R_HL, RL(m->readByte(R_HL)));
		break;
	case 0x17u:	//RL A
		RL(&A);
		break;
	case 0x18u:	//RR B
		RR(&B);
		break;
	case 0x19u:	//RR C
		RR(&C);
		break;
	case 0x1Au:	//RR D
		RR(&D);
		break;
	case 0x1Bu:	//RR E
		RR(&E);
		break;
	case 0x1Cu:	//RR H
		RR(&H);
		break;
	case 0x1Du:	//RR L
		RR(&L);
		break;
	case 0x1Eu:	//RR (HL)
		m->writeByte(R_HL, RR(m->readByte(R_HL)));
		break;
	case 0x1Fu:	//RR A
		RR(&A);
		break;
	case 0x20u:	//SLA B
		SLA(&B);
		break;
	case 0x21u:	//SLA C
		SLA(&C);
		break;
	case 0x22u:	//SLA D
		SLA(&D);
		break;
	case 0x23u:	//SLA E
		SLA(&E);
		break;
	case 0x24u:	//SLA H
		SLA(&H);
		break;
	case 0x25u:	//SLA L
		SLA(&L);
		break;
	case 0x26u:	//SLA (HL)
		m->writeByte(R_HL, SLA(m->readByte(R_HL)));
		break;
	case 0x27u:	//SLA A
		SLA(&A);
		break;
	case 0x28u:	//SRA B
		SRA(&B);
		break;
	case 0x29u:	//SRA C
		SRA(&C);
		break;
	case 0x2Au:	//SRA D
		SRA(&D);
		break;
	case 0x2Bu:	//SRA E
		SRA(&E);
		break;
	case 0x2Cu:	//SRA H
		SRA(&H);
		break;
	case 0x2Du:	//SRA L
		SRA(&L);
		break;
	case 0x2Eu:	//SRA (HL)
		m->writeByte(R_HL, SRA(m->readByte(R_HL)));
		break;
	case 0x2Fu:	//SRA A
		SRA(&A);
		break;
	case 0x30u:	//SWAP B
		SWAP(&B);
		break;
	case 0x31u:	//SWAP C
		SWAP(&C);
		break;
	case 0x32u:	//SWAP D
		SWAP(&D);
		break;
	case 0x33u:	//SWAP E
		SWAP(&E);
		break;
	case 0x34u:	//SWAP H
		SWAP(&H);
		break;
	case 0x35u:	//SWAP L
		SWAP(&L);
		break;
	case 0x36u:	//SWAP (HL)
		m->writeByte(R_HL, SWAP(m->readByte(R_HL)));
		break;
	case 0x37u:	//SWAP A
		SWAP(&A);
		break;
	case 0x38u:	//SRL B
		SRL(&B);
		break;
	case 0x39u:	//SRL C
		SRL(&C);
		break;
	case 0x3Au:	//SRL D
		SRL(&D);
		break;
	case 0x3Bu:	//SRL E
		SRL(&E);
		break;
	case 0x3Cu:	//SRL H
		SRL(&H);
		break;
	case 0x3Du:	//SRL L
		SRL(&L);
		break;
	case 0x3Eu:	//SRL (HL)
		m->writeByte(R_HL, SRL(m->readByte(R_HL)));
		break;
	case 0x3Fu:	//SRL A
		SRL(&A);
		break;
	case 0x40u:	//BIT 0,B
		BIT(0, &B);
		break;
	case 0x41u:	//BIT 0,C
		BIT(0, &C);
		break;
	case 0x42u:	//BIT 0,D
		BIT(0, &D);
		break;
	case 0x43u:	//BIT 0,E
		BIT(0, &E);
		break;
	case 0x44u:	//BIT 0,H
		BIT(0, &H);
		break;
	case 0x45u:	//BIT 0,L
		BIT(0, &L);
		break;
	case 0x46u:	//BIT 0,(HL)
		BIT(0, m->readByte(R_HL));
		break;
	case 0x47u:	//BIT 0,A
		BIT(0, &A);
		break;
	case 0x48u:	//BIT 1,B
		BIT(1, &B);
		break;
	case 0x49u:	//BIT 1,C
		BIT(1, &C);
		break;
	case 0x4Au:	//BIT 1,D
		BIT(1, &D);
		break;
	case 0x4Bu:	//BIT 1,E
		BIT(1, &E);
		break;
	case 0x4Cu:	//BIT 1,H
		BIT(1, &H);
		break;
	case 0x4Du:	//BIT 1,L
		BIT(1, &L);
		break;
	case 0x4Eu:	//BIT 1,(HL)
		BIT(1, m->readByte(R_HL));
		break;
	case 0x4Fu:	//BIT 1,A
		BIT(1, &A);
		break;
	case 0x50u:	//BIT 2,B
		BIT(2, &B);
		break;
	case 0x51u:	//BIT 2,C
		BIT(2, &C);
		break;
	case 0x52u:	//BIT 2,D
		BIT(2, &D);
		break;
	case 0x53u:	//BIT 2,E
		BIT(2, &E);
		break;
	case 0x54u:	//BIT 2,H
		BIT(2, &H);
		break;
	case 0x55u:	//BIT 2,L
		BIT(2, &L);
		break;
	case 0x56u:	//BIT 2,(HL)
		BIT(2, m->readByte(R_HL));
		break;
	case 0x57u:	//BIT 2,A
		BIT(2, &A);
		break;
	case 0x58u:	//BIT 3,B
		BIT(3, &B);
		break;
	case 0x59u:	//BIT 3,C
		BIT(3, &C);
		break;
	case 0x5Au:	//BIT 3,D
		BIT(3, &D);
		break;
	case 0x5Bu:	//BIT 3,E
		BIT(3, &E);
		break;
	case 0x5Cu:	//BIT 3,H
		BIT(3, &H);
		break;
	case 0x5Du:	//BIT 3,L
		BIT(3, &L);
		break;
	case 0x5Eu:	//BIT 3,(HL)
		BIT(3, m->readByte(R_HL));
		break;
	case 0x5Fu:	//BIT 3,A
		BIT(3, &A);
		break;
	case 0x60u:	//BIT 4,B
		BIT(4, &B);
		break;
	case 0x61u:	//BIT 4,C
		BIT(4, &C);
		break;
	case 0x62u:	//BIT 4,D
		BIT(4, &D);
		break;
	case 0x63u:	//BIT 4,E
		BIT(4, &E);
		break;
	case 0x64u:	//BIT 4,H
		BIT(4, &H);
		break;
	case 0x65u:	//BIT 4,L
		BIT(4, &L);
		break;
	case 0x66u:	//BIT 4,(HL)
		BIT(4, m->readByte(R_HL));
		break;
	case 0x67u:	//BIT 4,A
		BIT(4, &A);
		break;
	case 0x68u:	//BIT 5,B
		BIT(5, &B);
		break;
	case 0x69u:	//BIT 5,C
		BIT(5, &C);
		break;
	case 0x6Au:	//BIT 5,D
		BIT(5, &D);
		break;
	case 0x6Bu:	//BIT 5,E
		BIT(5, &E);
		break;
	case 0x6Cu:	//BIT 5,H
		BIT(5, &H);
		break;
	case 0x6Du:	//BIT 5,L
		BIT(5, &L);
		break;
	case 0x6Eu:	//BIT 5,(HL)
		BIT(5, m->readByte(R_HL));
		break;
	case 0x6Fu:	//BIT 5,A
		BIT(5, &A);
		break;
	case 0x70u:	//BIT 6,B
		BIT(6, &B);
		break;
	case 0x71u:	//BIT 6,C
		BIT(6, &C);
		break;
	case 0x72u:	//BIT 6,D
		BIT(6, &D);
		break;
	case 0x73u:	//BIT 6,E
		BIT(6, &E);
		break;
	case 0x74u:	//BIT 6,H
		BIT(6, &H);
		break;
	case 0x75u:	//BIT 6,L
		BIT(6, &L);
		break;
	case 0x76u:	//BIT 6,(HL)
		BIT(6, m->readByte(R_HL));
		break;
	case 0x77u:	//BIT 6,A
		BIT(6, &A);
		break;
	case 0x78u:	//BIT 7,B
		BIT(7, &B);
		break;
	case 0x79u:	//BIT 7,C
		BIT(7, &C);
		break;
	case 0x7Au:	//BIT 7,D
		BIT(7, &D);
		break;
	case 0x7Bu:	//BIT 7,E
		BIT(7, &E);
		break;
	case 0x7Cu:	//BIT 7,H
		BIT(7, &H);
		break;
	case 0x7Du:	//BIT 7,L
		BIT(7, &L);
		break;
	case 0x7Eu:	//BIT 7,(HL)
		BIT(7, m->readByte(R_HL));
		break;
	case 0x7Fu:	//BIT 7,A
		BIT(7, &A);
		break;
	case 0x80u:	//RES 0,B
		RES(0, &B);
		break;
	case 0x81u:	//RES 0,C
		RES(0, &C);
		break;
	case 0x82u:	//RES 0,D
		RES(0, &D);
		break;
	case 0x83u:	//RES 0,E
		RES(0, &E);
		break;
	case 0x84u:	//RES 0,H
		RES(0, &H);
		break;
	case 0x85u:	//RES 0,L
		RES(0, &L);
		break;
	case 0x86u:	//RES 0,(HL)
		m->writeByte(R_HL, RES(0, m->readByte(R_HL)));
		break;
	case 0x87u:	//RES 0,A
		RES(0, &A);
		break;
	case 0x88u:	//RES 1,B
		RES(1, &B);
		break;
	case 0x89u:	//RES 1,C
		RES(1, &C);
		break;
	case 0x8Au:	//RES 1,D
		RES(1, &D);
		break;
	case 0x8Bu:	//RES 1,E
		RES(1, &E);
		break;
	case 0x8Cu:	//RES 1,H
		RES(1, &H);
		break;
	case 0x8Du:	//RES 1,L
		RES(1, &L);
		break;
	case 0x8Eu:	//RES 1,(HL)
		m->writeByte(R_HL, RES(1, m->readByte(R_HL)));
		break;
	case 0x8Fu:	//RES 1,A
		RES(1, &A);
		break;
	case 0x90u:	//RES 2,B
		RES(2, &B);
		break;
	case 0x91u:	//RES 2,C
		RES(2, &C);
		break;
	case 0x92u:	//RES 2,D
		RES(2, &D);
		break;
	case 0x93u:	//RES 2,E
		RES(2, &E);
		break;
	case 0x94u:	//RES 2,H
		RES(2, &H);
		break;
	case 0x95u:	//RES 2,L
		RES(2, &L);
		break;
	case 0x96u:	//RES 2,(HL)
		m->writeByte(R_HL, RES(2, m->readByte(R_HL)));
		break;
	case 0x97u:	//RES 2,A
		RES(2, &A);
		break;
	case 0x98u:	//RES 3,B
		RES(3, &B);
		break;
	case 0x99u:	//RES 3,C
		RES(3, &C);
		break;
	case 0x9Au:	//RES 3,D
		RES(3, &D);
		break;
	case 0x9Bu:	//RES 3,E
		RES(3, &E);
		break;
	case 0x9Cu:	//RES 3,H
		RES(3, &H);
		break;
	case 0x9Du:	//RES 3,L
		RES(3, &L);
		break;
	case 0x9Eu:	//RES 3,(HL)
		m->writeByte(R_HL, RES(3, m->readByte(R_HL)));
		break;
	case 0x9Fu:	//RES 3,A
		RES(3, &A);
		break;
	case 0xA0u:	//RES 4,B
		RES(4, &B);
		break;
	case 0xA1u:	//RES 4,C
		RES(4, &C);
		break;
	case 0xA2u:	//RES 4,D
		RES(4, &D);
		break;
	case 0xA3u:	//RES 4,E
		RES(4, &E);
		break;
	case 0xA4u:	//RES 4,H
		RES(4, &H);
		break;
	case 0xA5u:	//RES 4,L
		RES(4, &L);
		break;
	case 0xA6u:	//RES 4,(HL)
		m->writeByte(R_HL, RES(4, m->readByte(R_HL)));
		break;
	case 0xA7u:	//RES 4,A
		RES(4, &A);
		break;
	case 0xA8u:	//RES 5,B
		RES(5, &B);
		break;
	case 0xA9u:	//RES 5,C
		RES(5, &C);
		break;
	case 0xAAu:	//RES 5,D
		RES(5, &D);
		break;
	case 0xABu:	//RES 5,E
		RES(5, &E);
		break;
	case 0xACu:	//RES 5,H
		RES(5, &H);
		break;
	case 0xADu:	//RES 5,L
		RES(5, &L);
		break;
	case 0xAEu:	//RES 5,(HL)
		m->writeByte(R_HL, RES(5, m->readByte(R_HL)));
		break;
	case 0xAFu:	//RES 5,A
		RES(5, &A);
		break;
	case 0xB0u:	//RES 6,B
		RES(6, &B);
		break;
	case 0xB1u:	//RES 6,C
		RES(6, &C);
		break;
	case 0xB2u:	//RES 6,D
		RES(6, &D);
		break;
	case 0xB3u:	//RES 6,E
		RES(6, &E);
		break;
	case 0xB4u:	//RES 6,H
		RES(6, &H);
		break;
	case 0xB5u:	//RES 6,L
		RES(6, &L);
		break;
	case 0xB6u:	//RES 6,(HL)
		m->writeByte(R_HL, RES(6, m->readByte(R_HL)));
		break;
	case 0xB7u:	//RES 6,A
		RES(6, &A);
		break;
	case 0xB8u:	//RES 7,B
		RES(7, &B);
		break;
	case 0xB9u:	//RES 7,C
		RES(7, &C);
		break;
	case 0xBAu:	//RES 7,D
		RES(7, &D);
		break;
	case 0xBBu:	//RES 7,E
		RES(7, &E);
		break;
	case 0xBCu:	//RES 7,H
		RES(7, &H);
		break;
	case 0xBDu:	//RES 7,L
		RES(7, &L);
		break;
	case 0xBEu:	//RES 7,(HL)
		m->writeByte(R_HL, RES(7, m->readByte(R_HL)));
		break;
	case 0xBFu:	//RES 7,A
		RES(7, &A);
		break;
	case 0xC0u:	//SET 0,B
		SET(0, &B);
		break;
	case 0xC1u:	//SET 0,C
		SET(0, &C);
		break;
	case 0xC2u:	//SET 0,D
		SET(0, &D);
		break;
	case 0xC3u:	//SET 0,E
		SET(0, &E);
		break;
	case 0xC4u:	//SET 0,H
		SET(0, &H);
		break;
	case 0xC5u:	//SET 0,L
		SET(0, &L);
		break;
	case 0xC6u:	//SET 0,(HL)
		m->writeByte(R_HL, SET(0, m->readByte(R_HL)));
		break;
	case 0xC7u:	//SET 0,A
		SET(0, &A);
		break;
	case 0xC8u:	//SET 1,B
		SET(1, &B);
		break;
	case 0xC9u:	//SET 1,C
		SET(1, &C);
		break;
	case 0xCAu:	//SET 1,D
		SET(1, &D);
		break;
	case 0xCBu:	//SET 1,E
		SET(1, &E);
		break;
	case 0xCCu:	//SET 1,H
		SET(1, &H);
		break;
	case 0xCDu:	//SET 1,L
		SET(1, &L);
		break;
	case 0xCEu:	//SET 1,(HL)
		m->writeByte(R_HL, SET(1, m->readByte(R_HL)));
		break;
	case 0xCFu:	//SET 1,A
		SET(1, &A);
		break;
	case 0xD0u:	//SET 2,B
		SET(2, &B);
		break;
	case 0xD1u:	//SET 2,C
		SET(2, &C);
		break;
	case 0xD2u:	//SET 2,D
		SET(2, &D);
		break;
	case 0xD3u:	//SET 2,E
		SET(2, &E);
		break;
	case 0xD4u:	//SET 2,H
		SET(2, &H);
		break;
	case 0xD5u:	//SET 2,L
		SET(2, &L);
		break;
	case 0xD6u:	//SET 2,(HL)
		m->writeByte(R_HL, SET(2, m->readByte(R_HL)));
		break;
	case 0xD7u:	//SET 2,A
		SET(2, &A);
		break;
	case 0xD8u:	//SET 3,B
		SET(3, &B);
		break;
	case 0xD9u:	//SET 3,C
		SET(3, &C);
		break;
	case 0xDAu:	//SET 3,D
		SET(3, &D);
		break;
	case 0xDBu:	//SET 3,E
		SET(3, &E);
		break;
	case 0xDCu:	//SET 3,H
		SET(3, &H);
		break;
	case 0xDDu:	//SET 3,L
		SET(3, &L);
		break;
	case 0xDEu:	//SET 3,(HL)
		m->writeByte(R_HL, SET(3, m->readByte(R_HL)));
		break;
	case 0xDFu:	//SET 3,A
		SET(3, &A);
		break;
	case 0xE0u:	//SET 4,B
		SET(4, &B);
		break;
	case 0xE1u:	//SET 4,C
		SET(4, &C);
		break;
	case 0xE2u:	//SET 4,D
		SET(4, &D);
		break;
	case 0xE3u:	//SET 4,E
		SET(4, &E);
		break;
	case 0xE4u:	//SET 4,H
		SET(4, &H);
		break;
	case 0xE5u:	//SET 4,L
		SET(4, &L);
		break;
	case 0xE6u:	//SET 4,(HL)
		m->writeByte(R_HL, SET(4, m->readByte(R_HL)));
		break;
	case 0xE7u:	//SET 4,A
		SET(4, &A);
		break;
	case 0xE8u:	//SET 5,B
		SET(5, &B);
		break;
	case 0xE9u:	//SET 5,C
		SET(5, &C);
		break;
	case 0xEAu:	//SET 5,D
		SET(5, &D);
		break;
	case 0xEBu:	//SET 5,E
		SET(5, &E);
		break;
	case 0xECu:	//SET 5,H
		SET(5, &H);
		break;
	case 0xEDu:	//SET 5,L
		SET(5, &L);
		break;
	case 0xEEu:	//SET 5,(HL)
		m->writeByte(R_HL, SET(5, m->readByte(R_HL)));
		break;
	case 0xEFu:	//SET 5,A
		SET(5, &A);
		break;
	case 0xF0u:	//SET 6,B
		SET(6, &B);
		break;
	case 0xF1u:	//SET 6,C
		SET(6, &C);
		break;
	case 0xF2u:	//SET 6,D
		SET(6, &D);
		break;
	case 0xF3u:	//SET 6,E
		SET(6, &E);
		break;
	case 0xF4u:	//SET 6,H
		SET(6, &H);
		break;
	case 0xF5u:	//SET 6,L
		SET(6, &L);
		break;
	case 0xF6u:	//SET 6,(HL)
		m->writeByte(R_HL, SET(6, m->readByte(R_HL)));
		break;
	case 0xF7u:	//SET 6,A
		SET(6, &A);
		break;
	case 0xF8u:	//SET 7,B
		SET(7, &B);
		break;
	case 0xF9u:	//SET 7,C
		SET(7, &C);
		break;
	case 0xFAu:	//SET 7,D
		SET(7, &D);
		break;
	case 0xFBu:	//SET 7,E
		SET(7, &E);
		break;
	case 0xFCu:	//SET 7,H
		SET(7, &H);
		break;
	case 0xFDu:	//SET 7,L
		SET(7, &L);
		break;
	case 0xFEu:	//SET 7,(HL)
		m->writeByte(R_HL, SET(7, m->readByte(R_HL)));
		break;
	case 0xFFu:	//SET 7,A
		SET(7, &A);
		break;
	}
}

void handleStop(){

}

void handleHalt(){

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