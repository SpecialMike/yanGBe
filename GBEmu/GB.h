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
private:

};

#endif