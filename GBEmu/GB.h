#ifndef GB_H
#define GB_H
#pragma once

#include "MMU.h"
#include "GPU.h"
#include "CPU.h"

class GB
{
public:
	GB();
	GB(const char* filePath);
	~GB();
	MMU* m;
	GPU* g;
	CPU* c;
	void getCartInfo();
	void UpdateToVBlank();
	bool openROM(const char* file);
	void buttonUp(int key);
	void buttonDown(int key);
	void SaveState(const char* filePath);
	void LoadState(const char* filePath);
	bool isProcessing;
private:
	uint8* cartROM;

};

#endif