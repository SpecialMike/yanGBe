//Will be the entry-point, though it currently contains all the CPU logic in it as well for now.

#include "stdafx.h"
#include "GBEmu.h"
#include "GPU.h"

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
int ticks[] = {
	4, 12, 8, 8, 4, 4, 8, 4, 20, 8, 8, 8, 4, 4, 8, 4,
	4, 12, 8, 8, 4, 4, 8, 4, 12, 8, 8, 8, 4, 4, 8, 4,
	8, 12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
	8, 12, 8, 8, 12, 12, 12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
	8, 12, 12, 16, 12, 16, 8, 16, 8, 16, 12, 0, 12, 24, 8, 16,
	8, 12, 12, 4, 12, 16, 8, 16, 8, 16, 12, 4, 12, 4, 8, 16,
	12, 12, 8, 4, 4, 16, 8, 16, 16, 4, 16, 4, 4, 4, 8, 16,
	12, 12, 8, 4, 4, 16, 8, 16, 12, 8, 16, 4, 0, 4, 8, 16 
};

void handleStop();
void handleHalt();
void OP(uint8 code);
void CB(uint8 code);
bool openROM(char* file);
void getCartInfo();
GPU* g = new GPU();
MMU* m;


int main(int argc, char* argv[]){
	if (argc != 2)
		return 1;
	if (!openROM(argv[1])){
		return 1;
	}
	getCartInfo();
	bool done = false;
	m = new MMU(MMU::MBC1, 1, 1, cartROM);
	SDL_Event event;
	g->Reset();

	while (!done){
		SDL_WaitEvent(&event);

		switch (event.type){
			case SDL_QUIT:
				done = true;
		}
		g->Update();
	}

	delete g;
	delete m;
	return 0;
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


void MainLoop(){
	const int cyclesPerUpdate = 69905;
	int cycles = 0;

	while (cycles < cyclesPerUpdate){
		uint8 lastOP = m->readByte(PC++);
		OP(lastOP);
		updateTimer(ticks[lastOP]);
		g->Step(ticks[lastOP]);
		handleInterrupts();
	}
	g->Update();
}

void OP(uint8 code){
	uint16 nn = 0;
	uint32 temp = 0;
	uint8 temp2 = 0;
	switch (code){
	case 0: //NOP (do nothing)
		break;
	case 1: //LD BC, nn Load a 16-bit immediate nn into BC
		C = m->readByte(PC++);
		B = m->readByte(PC++);
		break;
	case 2: //LD (BC), A Load A into (BC)
		m->writeByte(R_BC, A);
		break;
	case 3: //INC BC Increment BC
		temp = R_BC + 1;
		B = temp >> 8 & 0xFFu;
		C = temp & 0xFFu;
		break;
	case 4: //INC B Increment B. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		B += 1;
		F.set(FLAG_ZERO, B == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (B & 0xFu) == 0);
		break;
	case 5: //DEC B Decrement B. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		B -= 1;
		F.set(FLAG_ZERO, B == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (B & 0xFu) == 0xFu);
		break;
	case 6: //LD B,n Load an 8-bit immediate n into B
		B = m->readByte(PC++);
		break;
	case 7: //RLCA Rotate A left. Flags: set Z if zero, N and H reset, C contains the old bit 7
		F.set(FLAG_C, A > 0x7Fu);
		A = ((A << 1) & 0xFFu) | (A >> 7);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		break;
	case 8: //LD (nn),SP Load value at SP into 16-bit immediate address (nn)
		nn = m->readByte(PC++);
		nn |= (m->readByte(PC++) << 8);
		m->writeByte(nn, SP & 0xFFu);
		m->writeByte(nn + 1, SP >> 8);
		break;
	case 9: //ADD HL,BC Add BC to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		temp = R_HL + R_BC;
		F.set(FLAG_HC, (R_HL & 0xFFFu) > (temp & 0xFFFu));
		F.set(FLAG_C, (temp > 0xFFFFu));
		L = temp & 0xFFu;
		H = temp >> 8 & 0xFFu;
		F.set(FLAG_SUB, false);
		break;
	case 10: //LD A,(BC) load value at (BC) into A
		A = m->readByte(R_BC);
		break;
	case 11: //DEC BC Decrement BC
		temp = R_BC - 1;
		B = temp >> 8;
		C = temp & 0xFFu;
		break;
	case 12: //INC C Increment C. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		C += 1;
		F.set(FLAG_ZERO, C == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (C & 0xFu) == 0);
		break;
	case 13: //DEC C Decrement C. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		C -= 1;
		F.set(FLAG_ZERO, C == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (C & 0xFu) == 0xFu);
		break;
	case 14: //LD C,n Load an 8-bit immediate n into C
		C = m->readByte(PC++);
		break;
	case 15: //RRCA Rotate A right. Flags:Z - set if result is zero; N,H - reset; C - contains old bit 0;
		F.set(FLAG_C, A & 0x1u);
		A = (A >> 1) | ((A & 0x1u) << 7);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		break;
	case 16: //STOP Halt CPU and LCD display until button pressed. Check for additional 0x00u after the opcode
		handleStop();
		break;
	case 17: //LD DE,nn Load a 16-bit immediate nn into DE
		E = m->readByte(PC++);
		D = m->readByte(PC++);
		break;
	case 18: //LD (DE),A Load value at A into (DE)
		m->writeByte(R_DE, A);
		break;
	case 19: //INC DE Increment DE
		temp = R_DE + 1;
		D = temp >> 8 & 0xFFu;
		E = temp & 0xFFu;
		break;
	case 20: //INC D Increment D. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		D += 1;
		F.set(FLAG_ZERO, D == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (D & 0xFu) == 0);
		break;
	case 21: //DEC D Decrement D. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		D -= 1;
		F.set(FLAG_ZERO, D == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (D & 0xFu) == 0xFu);
		break;
	case 22: //LD D,n Load an 8-bit immediate n into D
		D = m->readByte(PC++);
		break;
	case 23: //RLA Rotate A left through Carry Flag. Flags: Z - set if result is zero; N,H - reset; C - contains old bit 7
		temp = (F[FLAG_C]) ? 1 : 0; //old carry to go to A's bit one
		F.set(FLAG_C, A > 0x7Fu);
		A = ((A << 1) & 0xFFu) | (temp & 0x1u);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		break;
	case 24: //JR n Jump to PC+n where n is an 8-bit immediate
		PC += (1 + m->readByte(PC++));
		break;
	case 25: //ADD HL,DE Add DE to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		temp = R_HL + R_DE;
		F.set(FLAG_HC, (R_HL & 0xFFFu) > (temp & 0xFFFu));
		F.set(FLAG_C, (temp > 0xFFFFu));
		L = temp & 0xFFu;
		H = temp >> 8 & 0xFFu;
		F.set(FLAG_SUB, false);
		break;
	case 26: //LD A,(DE) Load value at (DE) to A
		A = m->readByte(R_DE);
		break;
	case 27: //DEC DE Decrement DE
		temp = R_DE - 1;
		D = temp >> 8;
		E = temp & 0xFFu;
		break;
	case 28: //INC E Increment E. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		E += 1;
		F.set(FLAG_ZERO, E == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (E & 0xFu) == 0);
		break;
	case 29: //DEC E Decrement E. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		E -= 1;
		F.set(FLAG_ZERO, E == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (E & 0xFu) == 0xFu);
		break;
	case 30: //LD E,n Load an 8-bit immediate n into E
		E = m->readByte(PC++);
		break;
	case 31: //RRA Rotate A right through Carry flag. Flags:Z - set if result is zero; N,H - reset; C - contains old bit 0
		temp = (F[FLAG_C]) ? 0x80u : 0; //old carry to go to A's bit seven
		F.set(FLAG_C, A & 0x01u);
		A = ((A >> 1) & 0x7Fu) | temp;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		break;
	case 32: //JR NZ,n Jump to PC+n if Z flag == 0
		if (!F[FLAG_ZERO]){
			PC += (1 + m->readByte(PC++));
		}
		else{
			PC++;
		}
		break;
	case 33: //LD HL,nn Load a 16-bit immediate nn into HL
		L = m->readByte(PC++);
		H = m->readByte(PC++);
		break;
	case 34: //LDI (HL),A Load value at A into (HL), increment HL
		m->writeByte(R_HL, A);
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		break;
	case 35: //INC HL Increment HL
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		break;
	case 36: //INC H Increment H. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		H += 1;
		F.set(FLAG_ZERO, H == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (H & 0xFu) == 0);
		break;
	case 37: //DEC H Decrement H. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		H -= 1;
		F.set(FLAG_ZERO, H == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (H & 0xFu) == 0xFu);
		break;
	case 38: //LD H,n Load an 8-bit immediate n into H
		H = m->readByte(PC++);
		break;
	case 39: //DAA Decimal adjust A. Flags:Z - set if A is zero; H - reset; C - set or reset, depending on operation
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
		break;
	case 40: //JR Z,n Jump to PC+n if Z flag == 1
		if (F[FLAG_ZERO]){
			PC += (1 + m->readByte(PC++));
		}
		else{
			PC++;
		}
		break;
	case 41: //ADD HL,HL Add HL to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		temp = R_HL + R_HL;
		F.set(FLAG_HC, (R_HL & 0xFFFu) > (temp & 0xFFFu));
		F.set(FLAG_C, (temp > 0xFFFFu));
		L = temp & 0xFFu;
		H = temp >> 8 & 0xFFu;
		F.set(FLAG_SUB, false);
		break;
	case 42: //LDI A,(HL) Load value at (HL) into A, increment HL
		A = m->readByte(R_DE);
		temp = R_HL + 1;
		H = temp >> 8 & 0xFFu;
		L = temp & 0xFFu;
		break;
	case 43: //DEC HL Decrement HL
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		break;
	case 44: //INC L Increment L. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		L += 1;
		F.set(FLAG_ZERO, L == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (L & 0xFu) == 0);
		break;
	case 45: //DEC L Decrement L. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		L -= 1;
		F.set(FLAG_ZERO, L == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (L & 0xFu) == 0xFu);
		break;
	case 46: //LD L,n Load an 8-bit immediate n into L
		L = m->readByte(PC++);
		break;
	case 47: //CPL Compliment A (flip all bits). Flags:N,H - set
		A = ~A;
		F.set(FLAG_HC, true);
		F.set(FLAG_SUB, true);
		break;
	case 48: //JR NC,n Jump to PC+n if C flag == 0
		if (!F[FLAG_C]){
			PC += (m->readByte(PC++));
		}
		else{
			PC++;
		}
		break;
	case 49: //LD SP,nn Load a 16-bit immediate nn into SP
		//SP = m->readByte(PC++) | (m->readByte(PC++) << 8);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		SP = nn;
		break;
	case 50: //LDD (HL),A Put A into (HL), decrement HL
		m->writeByte(R_HL, A);
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		break;
	case 51: //INC SP Increment SP
		SP++;
		break;
	case 52: //INC (HL) Increment (HL). Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		m->writeByte(R_HL, m->readByte(R_HL) + 1);
		F.set(FLAG_ZERO, m->readByte(R_HL) == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (m->readByte(R_HL) & 0xFu) == 0);
		break;
	case 53: //DEC (HL) Decrement (HL). Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		m->writeByte(m->readByte(R_HL), m->readByte(R_HL) - 1);
		F.set(FLAG_ZERO, m->readByte(R_HL) == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (m->readByte(R_HL) & 0xFu) == 0xFu);
		break;
	case 54: //LD (HL),n Load an 8-bit immediate n into (HL)
		m->writeByte(R_HL, m->readByte(PC++));
		break;
	case 55: //SCF Set carry flag. Flags:N,H - reset; C - set.
		F.set(FLAG_C, true);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, false);
		break;
	case 56: //JR C,n Jump to PC+n if C flag == 1
		if (F[FLAG_C]){
			PC += (m->readByte(PC++));
		}
		else{
			PC++;
		}
		break;
	case 57: //ADD HL,SP Add SP to HL. Flags:N - reset; H - set if carry from bit 11; C - set if carry from bit 15.
		temp = R_HL + SP;
		F.set(FLAG_HC, (R_HL & 0xFFFu) > (temp & 0xFFFu));
		F.set(FLAG_C, (temp > 0xFFFFu));
		L = temp & 0xFFu;
		H = temp >> 8 & 0xFFu;
		F.set(FLAG_SUB, false);
		break;
	case 58: //LDD A,(HL) Put value at (HL) into A, decrement HL
		A = m->readByte(R_HL);
		temp = R_DE - 1;
		H = temp >> 8;
		L = temp & 0xFFu;
		break;
	case 59: //DEC SP Decrement SP
		SP--;
		break;
	case 60: //INC A Increment A. Flags:Z - Set if result is zero; N - reset, H - set if carry from bit 3
		A += 1;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		F.set(FLAG_HC, (A & 0xFu) == 0);
		break;
	case 61: //DEC A Decrement A. Flags:Z - Set if result is zero; N - Set; H - set if no borrow from bit 4
		A -= 1;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, true);
		F.set(FLAG_HC, (A & 0xFu) == 0xFu);
		break;
	case 62: //LD A,n Load 8-bit immediate n into A
		A = m->readByte(PC++);
		break;
	case 63: //CCF Complement the carry flag.Flags:N,H- reset; C-complemented
		F.set(FLAG_C, !F[FLAG_C]);
		F.reset(FLAG_SUB);
		F.reset(FLAG_HC);
		break;
	case 64: //LD B,B Load value at B into B (do nothing)
		break;
	case 65: //LD B,C Load value at C into B
		B = C;
		break;
	case 66: //LD B,D Load value at D into B
		B = D;
		break;
	case 67: //LD B,E Load value at E into B
		B = E;
		break;
	case 68: //LD B,H Load value at H into B
		B = H;
		break;
	case 69: //LD B,L Load value at L into B
		B = L;
		break;
	case 70: //LD B,(HL) Load value at (HL) into B
		B = m->readByte(R_HL);
		break;
	case 71: //LD B,A Load value at A into B
		B = A;
		break;
	case 72: //LD C,B Load value at B into C
		C = B;
		break;
	case 73: //LD C,C Load value at C into C (do nothing)
		break;
	case 74: //LD C,D Load value at D into C
		C = D;
		break;
	case 75: //LD C,E Load value at E into C
		C = E;
		break;
	case 76: //LD C,H Load value at H into C
		C = H;
		break;
	case 77: //LD C,L Load value at L into C
		C = L;
		break;
	case 78: //LD C,(HL) Load value at (HL) into C
		C = m->readByte(R_HL);
		break;
	case 79: //LD C,A Load value at A into C
		C = A;
		break;
	case 80: //LD D,B Load value at B into D
		D = B;
		break;
	case 81: //LD D,C Load value at C into D
		D = C;
		break;
	case 82: //LD D,D Load value at D into D (do nothing)
		break;
	case 83: //LD D,E Load value at E into D
		D = E;
		break;
	case 84: //LD D,H Load value at H into D
		D = H;
		break;
	case 85: //LD D,L Load value at L into D
		D = L;
		break;
	case 86: //LD D,(HL) Load value at (HL) into D
		D = m->readByte(R_HL);
		break;
	case 87: //LD D,A Load value at A into D
		D = A;
		break;
	case 88: //LD E,B Load value at B into E
		E = B;
		break;
	case 89: //LD E,C Load value at C into E
		E = C;
		break;
	case 90: //LD E,D Load value at D into E
		E = D;
		break;
	case 91: //LD E,E Load value at E into E (do nothing)
		break;
	case 92: //LD E,H Load value at H into E
		E = H;
		break;
	case 93: //LD E,L Load value at L into E
		E = L;
		break;
	case 94: //LD E,(HL) Load value at (HL) into E
		E = m->readByte(R_HL);
		break;
	case 95: //LD E,A Load value at A into E
		E = A;
		break;
	case 96: //LD H,B Load value at B into H
		H = B;
		break;
	case 97: //LD H,C Load value at C into H
		H = C;
		break;
	case 98: //LD H,D Load value at D into H
		H = D;
		break;
	case 99: //LD H,E Load value at E into H
		H = E;
		break;
	case 100: //LD H,H Load value at H into H (do nothing)
		break;
	case 101: //LD H,L Load value at L into H
		H = L;
		break;
	case 102: //LD H,(HL) Load value at (HL) into H
		H = m->readByte(R_HL);
		break;
	case 103: //LD H,A Load value at A into H
		H = A;
		break;
	case 104: //LD L,B Load value at B into L
		L = B;
		break;
	case 105: //LD L,C Load value at C into L
		L = C;
		break;
	case 106: //LD L,D Load value at D into L
		L = D;
		break;
	case 107: //LD L,E Load value at E into L
		L = E;
		break;
	case 108: //LD L,H Load value at H into L
		L = H;
		break;
	case 109: //LD L,L Load value at L into L (do nothing)
		break;
	case 110: //LD L,(HL) Load value at (HL) into L
		L = m->readByte(R_HL);
		break;
	case 111: //LD L,A Load value at A into L
		L = A;
		break;
	case 112: //LD (HL),B Load value at B into (HL)
		m->writeByte(m->readByte(R_HL), B);
		break;
	case 113: //LD (HL),C Load value at C into (HL)
		m->writeByte(m->readByte(R_HL), C);
		break;
	case 114: //LD (HL),D Load value at D into (HL)
		m->writeByte(m->readByte(R_HL), D);
		break;
	case 115: //LD (HL),E Load value at E into (HL)
		m->writeByte(m->readByte(R_HL), E);
		break;
	case 116: //LD (HL),H Load value at H into (HL)
		m->writeByte(m->readByte(R_HL), H);
		break;
	case 117: //LD (HL),L Load value at L into (HL)
		m->writeByte(m->readByte(R_HL), L);
		break;
	case 118: //HALT Power down CPU until an interrupt occurs.
		handleHalt();
		break;
	case 119: //LD (HL),A Load value at A into (HL)
		m->writeByte(m->readByte(R_HL), A);
		break;
	case 120: //LD A,B Load value of B into A
		A = B;
		break;
	case 121: //LD A,C Load value of C into A
		A = C;
		break;
	case 122: //LD A,D Load value of D into A
		A = D;
		break;
	case 123: //LD A,E Load value of E into A
		A = E;
		break;
	case 124: //LD A,H Load value of H into A
		A = H;
		break;
	case 125: //LD A,L Load value of L into A
		A = L;
		break;
	case 126: //LD A,(HL) Load value of (HL) into A
		A = m->readByte(R_HL);
		break;
	case 127: //LD A,A Load value of A into A (do nothing)
		break;
	case 128: //ADD A,B Add B to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + B;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 129: //ADD A,C Add C to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + C;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 130: //ADD A,D Add D to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + D;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 131: //ADD A,E Add E to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + E;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 132: //ADD A,H Add H to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + H;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 133: //ADD A,L Add L to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + L;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 134: //ADD A,(HL) Add (HL) to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(R_HL);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 135: //ADD A,A Add A to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + A;
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 136: //ADC A,B Add B + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + B + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 137: //ADC A,C Add C + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + C + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 138: //ADC A,D Add D + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + D + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 139: //ADC A,E Add E + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + E + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 140: //ADC A,H Add H + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + H + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 141: //ADC A,L Add L + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + L + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 142: //ADC A,(HL) Add (HL) + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(R_HL) + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 143: //ADC A,A Add A + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + A + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 144: //SUB B Subtract B from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - B;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 145: //SUB C Subtract C from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - C;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 146: //SUB D Subtract D from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - D;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 147: //SUB E Subtract E from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - E;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 148: //SUB H Subtract H from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - H;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 149: //SUB L Subtract L from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - L;
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 150: //SUB (HL) Subtract (HL) from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - m->readByte(R_HL);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 151: //SUB A Subtract A from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		A = 0;
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.set(FLAG_ZERO, true);
		F.set(FLAG_SUB, true);
		break;
	case 152: //SBC B Subtract B plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - B - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 153: //SBC C Subtract C plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - C - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 154: //SBC D Subtract D plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - D - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 155: //SBC E Subtract E plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - E - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 156: //SBC H Subtract H plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - H - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 157: //SBC L Subtract L plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - L - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 158: //SBC A Subtract (HL) plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - m->readByte(R_HL) - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 159: //SBC A Subtract A plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		if (F[FLAG_C]){
			F.reset(FLAG_ZERO);
			F.set(FLAG_SUB, true);
			F.set(FLAG_HC, true);
			F.set(FLAG_C, true);
			A = 0xFFu;
		}
		else{
			F.reset(FLAG_HC);
			F.reset(FLAG_C);
			F.set(FLAG_SUB);
			F.set(FLAG_ZERO);
			A = 0;
		}
		break;
	case 160: //AND B Logical AND B and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= B;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 161: //AND C Logical AND C and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= C;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 162: //AND D Logical AND D and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= D;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 163: //AND E Logical AND E and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= E;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 164: //AND H Logical AND H and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= H;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 165: //AND L Logical AND L and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= L;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 166: //AND (HL) Logical AND (HL) and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 167: //AND A Logical AND A and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 168: //XOR B Logical XOR B and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= B;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 169: //XOR C Logical XOR C and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= C;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 170: //XOR D Logical XOR D and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= D;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 171: //XOR E Logical XOR E and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= E;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 172: //XOR H Logical XOR H and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= H;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 173: //XOR L Logical XOR L and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= L;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 174: //XOR (HL) Logical XOR (HL) and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 175: //XOR A Logical XOR A and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A = 0;
		F.set(FLAG_ZERO);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 176: //OR B Logical OR B and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= B;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 177: //OR C Logical OR C and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= C;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 178: //OR D Logical OR D and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= D;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 179: //OR E Logical OR E and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= E;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 180: //OR H Logical OR H and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= H;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 181: //OR L Logical OR L and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= L;
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 182: //OR (HL) Logical OR (HL) and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= m->readByte(R_HL);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 183: //OR A Logical OR A and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 184: //CP B Compare A and B. Flags: Z - set if A == B; N - Set; H - set if no borrow from bit 4; C - Set if A < B; 
		temp = A - B;
		F.set(FLAG_HC, (temp & 0xFu) > (A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 185: //CP C Compare A and C. Flags: Z - set if A == C; N - Set; H - set if no borrow from bit 4; C - Set if A < C; 
		temp = A - C;
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 186: //CP D Compare A and D. Flags: Z - set if A == D; N - Set; H - set if no borrow from bit 4; C - Set if A < D;  
		temp = A - D;
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 187: //CP E Compare A and E. Flags: Z - set if A == E; N - Set; H - set if no borrow from bit 4; C - Set if A < E;  
		temp = A - E;
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 188: //CP H Compare A and H. Flags: Z - set if A == H; N - Set; H - set if no borrow from bit 4; C - Set if A < H;  
		temp = A - H;
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 189: //CP L Compare A and L. Flags: Z - set if A == L; N - Set; H - set if no borrow from bit 4; C - Set if A < L;  
		temp = A - L;
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 190: //CP (HL) Compare A and (HL). Flags: Z - set if A == (HL); N - Set; H - set if no borrow from bit 4; C - Set if A < (HL);  
		temp = A - m->readByte(R_HL);
		F.set(FLAG_HC, (temp & 0xFu) >(A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 191: //CP A Compare A and A. Flags: Z - set if A == A; N - Set; H - set if no borrow from bit 4; C - Set if A < A; 
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.set(FLAG_ZERO);
		F.set(FLAG_SUB);
		break;
	case 192: //RET NZ Return if Z flag == 0
		if (!F[FLAG_ZERO]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
		}
		break;
	case 193: //POP BC, Pop 16-bits off of the stack into BC, increment SP twice
		C = m->readByte(SP);
		B = m->readByte(SP + 1);
		SP += 2;
		break;
	case 194: //JP NZ,nn Jump to address given by 16-bit immediate nn if Z flag == 0
		if (!F[FLAG_ZERO]){
			//postfix in C++ is undefined, PC may not increment between readByte calls postfix only is guaranteed to occur after the line
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8); 
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
		}
		else{
			PC += 2;
		}
		break;
	case 195: //JP nn Jump to address given by 16-bit immediate nn. LS byte first
		//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		PC = nn;
		break;
	case 196: //CALL NZ,nn Call nn if Z flag == 0
		if (!F[FLAG_ZERO]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
		}
		else{
			PC += 2;
		}
		break;
	case 197: //PUSH BC Push BC onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, B);
		SP -= 1;
		m->writeByte(SP, C);
		break;
	case 198: //ADD A,n Add 8-bit immediate to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(PC++);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 199: //RST 00H Push PC onto the stack, jump to 0000
		SP -= 1;
		m->writeByte(m->readByte(SP), PC >> 8);
		SP -= 1;
		m->writeByte(m->readByte(SP), PC & 0xFFu);
		PC = 0;
		break;
	case 200: //RET Z Return if Z flag == 1
		if (F[FLAG_ZERO]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
		}
		break;
	case 201: //RET Pop two bytes from stack and jump to the address given by them
		PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
		SP += 2;
		break;
	case 202: //JP Z,nn Jump to address given by 16-bit immediate nn if Z flag == 1
		if (F[FLAG_ZERO]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
		}
		else{
			PC += 2;
		}
		break;
	case 203: //CB prefix, call CB(OPCODE) where OPCODE is the next 8-bits after CB
		CB(m->readByte(PC++));
		break;
	case 204: //CALL Z,nn Call nn if Z flag == 1
		if (F[FLAG_ZERO]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
		}
		else{
			PC += 2;
		}
		break;
	case 205: //CALL nn Push address of next instruction onto the stack and then jump to nn where nn is a 16-bit immediate (LS byte first)
		temp = m->readByte(PC++);
		temp |= (m->readByte(PC++) << 8);
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = temp & 0xFFFFu;
		break;
	case 206: //ADC A,n Add 8-bit immediate n + Carry flag to A. Flags: Z - set if result is zero; N - Reset; H - Set if carry from bit 3; C - set if carry from bit 7
		temp = A + m->readByte(PC++) + ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (temp & 0xFu) < (A & 0xFu));
		F.set(FLAG_C, temp > 0xFFu);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB, false);
		break;
	case 207: //RST 08H Push PC onto the stack, jump to 0x0008u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x8u;
		break;
	case 208: //RET NC return if C flag == 0
		if (!F[FLAG_C]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
		}
		break;
	case 209: //POP DE, Pop 16-bits off of the stack into DE, increment SP twice
		E = m->readByte(SP);
		D = m->readByte(SP + 1);
		SP += 2;
		break;
	case 210: //JP NC,nn Jump to address given by 16-bit immediate nn if C flag == 0
		if (!F[FLAG_C]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
		}
		else{
			PC += 2;
		}
		break;
	case 211: //Illegal opcode, halt execution (0xD3u)
		printf("Illegal OP: 0xD3u at PC: %u", PC);
		break;
	case 212: //CALL NC,nn Call nn if C flag == 0
		if (!F[FLAG_C]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
		}
		else{
			PC += 2;
		}
		break;
	case 213: //PUSH DE Push DE onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, D);
		SP -= 1;
		m->writeByte(SP, E);
		break;
	case 214: //SUB n Subtract 8-bit immediate n from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - m->readByte(PC++);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 215: //RST 10H Push PC onto the stack, jump to 0x0010u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x10u;
		break;
	case 216: //RET C Return if C flag == 1
		if (F[FLAG_C]){
			PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
			SP += 2;
		}
		break;
	case 217: //RETI Pop two bytes off the stack and jump to that address, enable interrupts
		PC = (m->readByte(SP + 1) << 8) | m->readByte(SP);
		SP += 2;
		interruptEnabled = true;
		break;
	case 218: //JP C,nn Jump to address given by 16-bit immediate if C flag == 1
		if (F[FLAG_C]){
			//PC = m->readByte(PC++) | (m->readByte(PC++) << 8);
			nn = m->readByte(PC++);
			nn |= m->readByte(PC++) << 8;
			PC = nn;
		}
		else{
			PC += 2;
		}
		break;
	case 219: //Illegal opcode, halt execution (0xDBu)
		printf("Illegal OP: 0xDBu at PC: %u", PC);
		break;
	case 220: //CALL C,nn Call nn if C flag == 1
		if (F[FLAG_C]){
			temp = m->readByte(PC++);
			temp |= (m->readByte(PC++) << 8);
			SP -= 1;
			m->writeByte(SP, PC >> 8);
			SP -= 1;
			m->writeByte(SP, PC & 0xFFu);
			PC = temp & 0xFFFFu;
		}
		else{
			PC += 2;
		}
		break;
	case 221: //Illegal opcode, halt execution (0xDDu)
		printf("Illegal OP: 0xDDu at PC: %u", PC);
		break;
	case 222: //SBC n Subtract 8-bit immediate n plus carry flag from A. Flags: Z - set if result is zero; N - Set; H - Set if no borrow from bit 4; C- Set if no borrow.
		temp = A - m->readByte(PC++) - ((F[FLAG_C]) ? 1 : 0);
		F.set(FLAG_HC, (A & 0xFu) < (temp & 0xFu));
		F.set(FLAG_C, temp < 0);
		A = temp & 0xFFu;
		F.set(FLAG_ZERO, temp == 0);
		F.set(FLAG_SUB, true);
		break;
	case 223: //RST 18H Push PC onto the stack, jump to 0x0018u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x18u;
		break;
	case 224: //LDH n,A Load A into address given by $FF00 + n where n is an 8-bit immediate
		m->writeByte(m->readByte(0xFF00u + m->readByte(PC++)), A);
		break;
	case 225: //POP HL, Pop 16-bits off of the stack into HL, increment SP twice
		L = m->readByte(SP);
		H = m->readByte(SP + 1);
		SP += 2;
		break;
	case 226: //LD (C),A Load A into address $FF00 + C
		m->writeByte(m->readByte(0xFF00u + C), A);
		break;
	case 227: //Illegal opcode, halt execution (0xE3u)
		printf("Illegal OP: 0xE3u at PC: %u", PC);
		break;
	case 228: //Illegal opcode, halt execution (0xE4u)
		printf("Illegal OP: 0xE4u at PC: %u", PC);
		break;
	case 229: //PUSH HL Push HL onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, H);
		SP -= 1;
		m->writeByte(SP, L);
		break;
	case 230: //AND n Logical AND 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C - Reset; H - Set.
		A &= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 231: //RST 20H Push PC onto the stack, jump to 0x0020u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x20u;
		break;
	case 232: //ADD SP,n Add 8-bit immediate to SP.Flags:Z - reset; N - reset; H,C - set or reset according to operation
		temp2 = m->readByte(PC++);
		temp = SP + temp2;
		F.reset();
		F.set(FLAG_HC, ((SP ^ temp2 ^ (temp & 0xFFFFu)) & 0x10u) == 0x10u);
		F.set(FLAG_C, ((SP ^ temp2 ^ (temp & 0xFFFFu)) & 0x100u) == 0x100u);
		SP = temp & 0xFFFFu;
		break;
	case 233: //JP HL Jump to address in HL.
		PC = R_HL;
		break;
	case 234: //LD (nn),A Load value at A into 16-bit immediate address (nn)
		//m->writeByte(m->readByte(m->readByte(PC) | (m->readByte(PC+1) << 8)), A);
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		m->writeByte(nn, A);
		PC += 2;
		break;
	case 235: //Illegal opcode, halt execution (0xEBu)
		printf("Illegal OP: 0xEBu at PC: %u", PC);
		break;
	case 236: //Illegal opcode, halt execution (0xECu)
		printf("Illegal OP: 0xECu at PC: %u", PC);
		break;
	case 237: //Illegal opcode, halt execution (0xEDu)
		printf("Illegal OP: 0xEDu at PC: %u", PC);
		break;
	case 238: //XOR n Logical XOR 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A ^= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
		break;
	case 239: //RST 28H Push PC onto the stack, jump to 0x0028u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x28u;
		break;
	case 240: //LDH A,(n) Load value at $FF00 + n into A where n is an 8-bit immediate
		A = m->readByte(0xFF00u + m->readByte(PC++));
		break;
	case 241: //POP AF, Pop 16-bits off of the stack into AF, increment SP twice
		temp = m->readByte(SP);
		F.set(FLAG_ZERO, temp > 0x7Fu);
		F.set(FLAG_SUB, (temp & 0x40u) == 0x40u);
		F.set(FLAG_HC, (temp & 0x20u) == 0x20u);
		F.set(FLAG_C, (temp & 0x10u) == 0x10u);
		A = m->readByte(SP + 1);
		SP += 2;
		break;
	case 242: //LD A,(C) Load value at address $FF00 + C into A
		A = m->readByte(0xFF00u + C);
		break;
	case 243: //DI Disable interrupts after this instruction is executed
		interruptEnabled = false;
		break;
	case 244: //Illegal opcode, halt execution (0xF4u)
		printf("Illegal OP: 0xF4u at PC: %u", PC);
		break;
	case 245: //PUSH AF Push AF onto the stack, decrement SP twice
		SP -= 1;
		m->writeByte(SP, A);
		SP -= 1;
		m->writeByte(SP, ((F[FLAG_ZERO]) ? 0x80u : 0) | ((F[FLAG_SUB]) ? 0x40u : 0) | ((F[FLAG_HC]) ? 0x20u : 0) | ((F[FLAG_C]) ? 0x10u : 0));
		break;
	case 246: //OR n Logical OR 8-bit immediate n and A, result in A. Flags: Z - set if result is zero; N,C,H - Reset.
		A |= m->readByte(PC++);
		F.set(FLAG_ZERO, A == 0);
		F.reset(FLAG_HC);
		F.reset(FLAG_C);
		F.reset(FLAG_SUB);
break;
	case 247: //RST 30H Push PC onto the stack, jump to 0x0030u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x30u;
		break;
	case 248: //LDHL SP,n Load value SP + n into HL where n is an 8-bit immediate. flags: Z,N - reset; H,C set according to operation
		temp = m->readByte(PC++) + SP;
		L = temp & 0xFFu;
		H = (temp >> 8) & 0xFFu;
		temp = SP ^ temp ^ R_HL;
		F.reset();
		F.set(FLAG_C, (temp & 0x100u) == 0x100u);
		F.set(FLAG_HC, (temp & 0x10u) == 0x10u);
		break;
	case 249: //LD SP,HL Load value at HL into SP
		SP = R_HL;
		break;
	case 250: //LD A,(nn) Load value of 16-bit immediate address (nn) into A
		nn = m->readByte(PC++);
		nn |= m->readByte(PC++) << 8;
		A = m->readByte(nn);
		break;
	case 251: //EI Enable interrupts after this instruction is executed
		interruptEnabled = true;
		break;
	case 252: //Illegal opcode, halt execution (0xFCu)
		printf("Illegal OP: 0xFCu at PC: %u", PC);
		break;
	case 253: //Illegal opcode, halt execution (0xFDu)
		printf("Illegal OP: 0xFDu at PC: %u", PC);
		break;
	case 254: //CP n Compare A and 8-bit immediate n. Flags: Z - set if A == n; N - Set; H - set if no borrow from bit 4; C - Set if A < n; 
		temp = A - m->readByte(PC++);
		F.set(FLAG_HC, (temp & 0xFu) > (A & 0xFu));
		F.set(FLAG_C, temp < 0);
		F.set(FLAG_ZERO, A == 0);
		F.set(FLAG_SUB);
		break;
	case 255: //RST 38H, Push PC onto the stack, jump to 0x0038u
		SP -= 1;
		m->writeByte(SP, PC >> 8);
		SP -= 1;
		m->writeByte(SP, PC & 0xFFu);
		PC = 0x38u;
		break;

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
}
uint8 SRA(uint8 reg){	//Shift right through carry, MSB doesn't change
	F.reset();
	if ((reg & 0x1u) != 0){
		F.set(FLAG_C);
	}
	_int8 MSB = reg & 0x80u;
	reg >>= 1;
	reg |= MSB;
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
		m->writeByte(m->readByte(R_HL),RLC(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RRC(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RL(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RR(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SLA(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SRA(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SWAP(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SRL(m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(0, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(1, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(2, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(3, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(4, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(5, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(6, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), RES(7, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(0, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(1, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(2, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(3, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(4, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(5, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(6, m->readByte(R_HL)));
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
		m->writeByte(m->readByte(R_HL), SET(7, m->readByte(R_HL)));
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