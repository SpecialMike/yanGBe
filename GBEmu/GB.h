#ifndef GB_H
#define GB_H
#pragma once

#include "MMU.h"
#include "GPU.h"
#include "CPU.h"

class OptionFrame;

class GB
{

public:
	GB(const char* filePath);
	~GB();

	void ButtonDown(int key);
	void ButtonUp(int key);
	void GetCartInfo();
	void LoadRam(const char* filePath);
	void LoadState(const char* filePath);
	void OpenROM(const char* file);
	void SaveRam(const char* filepath);
	void SaveState(const char* filePath);
	void SetOptionFrame(OptionFrame* op);
	void UpdateToVBlank();

	bool isProcessing;
	MMU* m;
	GPU* g;
	CPU* c;
	OptionFrame* options;

protected:

private:
	uint8* cartROM;
	unsigned char stateSignature[15];
	unsigned char ramSignature[15];
	char romName[15];

};

#endif