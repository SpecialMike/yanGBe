#ifndef GPU_H
#define GPU_H

#pragma once
#define uint8 unsigned _int8
class CPU;
class MMU;

class GPU
{
private:
	uint8 mapPalette(uint8 colorNumber, uint8 palette);
	void updateLine();
	MMU* m;
	CPU* c;
	void SetLCDStatus();
	void drawBGLine();
	void drawSpriteLine();
public:
	GPU();
	~GPU();
	void Reset(MMU* mem, CPU* cpu);
	void Update();
	void Step(int cycles);
	uint8 data[144][160][3];
};

#endif