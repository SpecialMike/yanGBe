#ifndef GB_H
#define GB_H
#pragma once

#include "MMU.h"
#include "GPU.h"
#include "CPU.h"

class GB
{

public:
	GB(const char* filePath);
	~GB();

	void ButtonDown(int key);
	void ButtonUp(int key);
	void GetCartInfo();
	void LoadState(const char* filePath);
	void OpenROM(const char* file);
	void SaveState(const char* filePath);
	void UpdateToVBlank();

	bool isProcessing;
	MMU* m;
	GPU* g;
	CPU* c;

protected:

private:
	uint8* cartROM;
	unsigned char signature[15];
	char romName[15];

};

#endif