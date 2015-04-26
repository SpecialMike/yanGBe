#include "stdafx.h"
#include "GPU.h"
#include "GBEmu.h"



unsigned _int32* data = new unsigned _int32[160 * 144];
SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
int mode = 0;
int modeClock = 0;
int scanlineCounter = 456;	//456 cycles to draw a scanline
int line = 0;
unsigned int GPUticks[] = {
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
unsigned _int32 colors[4] = { 0xFFFFFFu, 0xC0C0C0u, 0x808080u, 0x000000u };

GPU::GPU()
{
}


GPU::~GPU()
{
	delete[] data;
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void GPU::Reset(){
	if (SDL_Init(SDL_INIT_VIDEO) != 0){
		std::cout << SDL_GetError() << std::endl;
		return;
	}

	window = SDL_CreateWindow("GBEmu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 160, 144, 0);
	renderer = SDL_CreateRenderer(window, -1, 0);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, 160, 144);
	memset(data, 0x000000u, 160 * 144 * sizeof(unsigned _int32));
	return;
}

void GPU::Update(){
	SDL_UpdateTexture(texture, NULL, data, 160 * sizeof(unsigned _int32));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void SetLCDStatus(){

}

void GPU::Step(int cycles){
	SetLCDStatus();
	if ( (LCDC & (1 << 7)) > 0 ){	//is the LCD enabled?
		scanlineCounter -= cycles;
	}
	else{
		return;
	}

	if (scanlineCounter <= 0){	//done with this scanline, on to the next;
		scanlineCounter = 456;
		m->writeByte(0xFF44, LY + 1);
		line = LY;

		if (line == 144){
			requestInterrupt(VBLANK);
		}

		if (line < 153){	//VBLANK period is over, reset line to 0
			m->writeByte(0xFF44, 0);
		}
	}
}

unsigned int mapPalette(int colorNumber, int mode){
	return colors[colorNumber];
}

void drawBGLine(){
	bool useWindow = false;
	unsigned _int8 yPos, xPos;
	unsigned _int16 rowPos = 0, colPos;
	unsigned _int16 tileAddress;
	unsigned _int16 bgAddress = 0;

	if ((LCDC & 0x20u) && (WY <= LY)){
		yPos = LY - WY;

		if (LCDC & (1 << 6)){
			bgAddress = 0x1C00u;
		}
		else{
			bgAddress = 0x1800u;
		}
		useWindow = true;
	}
	else{
		yPos = SCY + LY;

		if (LCDC & (1 << 3)){
			bgAddress = 0x1C00u;
		}
		else{
			bgAddress = 0x1800u;
		}
	}

	rowPos = ((unsigned _int8)(yPos/8))*32; //TODO: calculate row position

	for (int i = 0; i < 160; i++){
		//draw each of the 160 pixels in the scanline
		unsigned _int8 tileLine, data1, data2;
		int colorBit, colorNumber;
		unsigned _int32 color;

		if (useWindow){	//if we are drawing a window pixel, adjust xPos
			unsigned _int8 rWX = WX - 7;
			if (i >= rWX)
				xPos = i - rWX;
		}
		else
			xPos = i + SCX;	//we are on the ith pixel after the scrollX register value

		colPos = (xPos / 8);

		//figure out which tile we are using
		if (LCDC & (1 << 4)){	//tile data at 0x8800u
			unsigned _int8 tilenumber;
			tileAddress = 0x0u;
			tilenumber = m->readByte(bgAddress + rowPos + colPos);
			tileAddress = (tilenumber * 16);
		}
		else{
			signed _int8 tilenumber;	//tile data at 0x8000u, numbers are signed
			tileAddress = 0x800u;
			tilenumber = m->readByte(bgAddress + rowPos + colPos);
			tileAddress += ((tilenumber+128) * 16);
		}

		tileLine = (yPos % 8) * 2;

		data1 = m->readByte(tileAddress + line);
		data2 = m->readByte(tileAddress + line + 1);

		colorBit = ((xPos % 8) - 7) * -1;
		colorNumber = (data2 & (1 << colorBit)) ? 0x2u : 0;
		colorNumber |= (data1 & (1 << colorBit)) ? 1 : 0;

		color = mapPalette(colorNumber, 0);

		data[LY*160 + i] = color;
	}
}

void drawSpriteLine(){

}

void GPU::updateLine(){
	if (LCDC & 0x1u){
		drawBGLine();
	}
	if (LCDC & 0x2u){
		drawSpriteLine();
	}
}