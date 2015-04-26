#ifndef GPU_H
#define GPU_H

#include <SDL.h>

#pragma once
	void drawBGLine();
class GPU
{
private:
	void updateLine();
public:
	GPU();
	~GPU();
	void Reset();
	void Update();
	void Step(int cycles);
};

#endif