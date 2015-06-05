#pragma once
#ifndef CPU_H
#define CPU_H

class GPU;
class MMU;

#define uint8 unsigned _int8
#define uint16 unsigned _int16

class CPU
{
private:
	MMU* m;
	GPU* g;
	uint16 SP = 0xFFFEu;
	uint16 PC = 0x0100u;
	void serviceInterrupt(int bit);
	int OP(uint8 code);
	void CB(uint8 code);
	int timerCounter;
	int dividerCounter;
	bool interruptEnabled;
	void handleHalt();
	void handleStop();
public:
	CPU();
	~CPU();
	enum interrupts{
		VBLANK = 0, LCD, TIMER, SERIAL, JOYPAD
	};
	void requestInterrupt(interrupts i);
	int update();
	int timerPeriod;
	void setMMU(MMU* mem);
	void setGPU(GPU* gpu);
	void updateTimer(int cycles);
	bool handleInterrupts();
};

#endif