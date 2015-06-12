#pragma once
#ifndef CPU_H
#define CPU_H

class GPU;
class MMU;

#define uint8 unsigned _int8
#define uint16 unsigned _int16

class CPU
{

public:
	enum interrupts{
		VBLANK = 0, LCD, TIMER, SERIAL, JOYPAD
	};

	CPU();
	~CPU();

	void LoadState(std::ifstream& fin);
	bool HandleInterrupts();
	void RequestInterrupt(interrupts i);
	void SaveState(std::ofstream& fout);
	void SetMMU(MMU* mem);
	void SetGPU(GPU* gpu);
	int Update();
	void UpdateTimer(int cycles);

protected:

private:
	void CB(uint8 code);
	void HandleHalt();
	void HandleStop();
	int OP(uint8 code);
	void ServiceInterrupt(int bit);

	int dividerCounter;
	GPU* g;
	bool interruptEnabled;
	MMU* m;
	uint16 PC = 0x0100u;
	uint16 SP = 0xFFFEu;
	int timerCounter;
	int timerPeriod;

};

#endif