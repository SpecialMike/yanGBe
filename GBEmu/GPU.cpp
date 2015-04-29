#include "stdafx.h"
#include "GPU.h"
#include "GBEmu.h"



uint16 data[160][144];
SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
int mode = 0;
int modeClock = 0;
int scanlineCounter = 456;	//456 cycles to draw a scanline

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
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR4444, SDL_TEXTUREACCESS_STATIC, 160, 144);
	//memset(data, 0x000000u, 160 * 144 * sizeof(unsigned _int32));
	return;
}

void GPU::Update(){
	SDL_UpdateTexture(texture, NULL, data, 144 * sizeof(uint16));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void SetLCDStatus(){
	uint8 currStatus = STAT;
	if (!((LCDC & (1 << 7)) > 0)){	//if the LCD is disabled, then reset the line counter, LY and change to mode 1
		scanlineCounter = 456;
		m->writeByte(0xFF44u, 0);
		currStatus &= 0xFCu;
		currStatus |= 0x01u;
		m->writeByte(0xFF41u, currStatus);
		return;
	}

	uint8 line = LY;
	uint8 currMode = currStatus & 0x3u;
	uint8 newMode = 0;
	bool modeInterruptEnabled = false;
	if (line >= 144){
		newMode = 0x01u;
		currStatus &= 0xFCu;
		currStatus |= newMode;
		modeInterruptEnabled = ((currStatus & (1 << 4)) > 0);
	}
	else if(scanlineCounter >= 376){
		newMode = 0x02u;
		currStatus &= 0xFCu;
		currStatus |= newMode;
		modeInterruptEnabled = ((currStatus & (1 << 5)) > 0);
	}
	else if (scanlineCounter >= 204){
		newMode = 0x03u;
		currStatus &= 0xFCu;
		currStatus |= newMode;

	}
	else{
		newMode = 0x00u;
		currStatus &= 0xFCu;
		modeInterruptEnabled = ((currStatus & (1 << 3)) > 0);
	}

	if (modeInterruptEnabled && (mode != newMode)){
		requestInterrupt(LCD);
	}

	if (LY == LYC){
		currStatus &= 0xFBu;	//Set the coincidence flag
		currStatus |= 0x04u;
		if ((currStatus & (1 << 6)) > 0)
			requestInterrupt(LCD);
	}
	else{
		currStatus &= 0xFBu;
	}
	m->writeByte(0xFF41, currStatus);
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
		m->incrementLY();
		uint8 line = LY;

		if (line == 144){
			requestInterrupt(VBLANK);
		}

		if (line > 153){	//VBLANK period is over, reset line to 0
			m->writeByte(0xFF44, 0);
		}

		if (line < 144){	//the current line within the drawable space, so draw it
			uint8 currLCDC = LCDC;
			if ((currLCDC & 0x01u) > 0)
				drawBGLine();
			if ((currLCDC & 0x02u) > 0)
				drawSpriteLine();
		}
	}
}

uint8 mapPalette(uint8 colorNumber, uint8 palette){
	uint8 colorBits = palette & (0x11u << colorNumber * 2);
	colorBits >>= colorNumber * 2;
	return colorBits;
}

void drawBGLine(){
	unsigned _int16 bgAddress;
	unsigned _int16 tileAddress;
	uint8 yPos;
	bool isUnsigned = true;
	bool drawWindow = false;

	//Check if we are drawing the window
	if ((LCDC & (1 << 5)) > 0 && (WY <= LY)){
		drawWindow = true;
	}

	if ((LCDC & (1 << 4)) > 0){	//is the tile data signed or not
		tileAddress = 0x8000u;
	}
	else{
		tileAddress = 0x8800u;
		isUnsigned = false;
	}

	if (drawWindow){	//determine the address for the background data
		if ((LCDC & (1 << 6)) > 0){
			bgAddress = 0x9C00u;
		}
		else{
			bgAddress = 0x9800u;
		}
	}
	else{
		if ((LCDC & (1 << 3)) > 0){
			bgAddress = 0x9C00u;
		}
		else{
			bgAddress = 0x9800u;
		}
	}

	if (drawWindow){
		yPos = LY - WY;
	}
	else{
		yPos = SCY - LY;
	}

	//which row in the tile are we on
	uint16 tileRow = (((uint16)(yPos / 8)) * 32);

	//draw the scanline
	for (int i = 0; i < 160; i++){
		uint8 xPos = i + SCX;
		if (drawWindow && i >= WX)
			xPos = i - WX;
		uint16 tileCol = xPos / 8;
		uint16 tileAddress = bgAddress + tileRow + tileCol;
		//made this an int because it can either be a signed or unsigned number, so this should prevent overflow
		_int16 tileNum;
		if (isUnsigned){
			tileNum = m->readByte(tileAddress);
		}
		else{
			tileNum = (_int16)m->readByte(tileAddress);
		}

		uint16 tileLocation = tileAddress;
		if (isUnsigned){
			tileLocation += (tileNum * 16);
		}
		else{
			tileLocation += ((tileNum + 128) * 16);
		}

		uint8 line = yPos % 8;
		line *= 2;
		uint8 upperData = m->readByte(tileLocation + line);
		uint8 lowerData = m->readByte(tileLocation + line + 1);

		int colorBit = xPos % 8;	//which bit are we looking at in the line's data
		colorBit -= 7;
		colorBit *= -1;

		uint8 color = ((lowerData & (1 << colorBit)) > 0) ? 0x2u : 0;	//get the color number
		color |= ((upperData & (1 << colorBit)) > 0) ? 0x1u : 0;
		uint8 palette = mapPalette(color, BGP);

		//data[i][LY] = palette;
		switch (palette){
		case 0x00u:
			data[i][LY] = 0xF000u;
			break;
		case 0x01u:
			data[i][LY] = 0xFCCCu;
			break;
		case 0x02u:
			data[i][LY] = 0xF777u;
			break;
		case 0x03u:
			data[i][LY] = 0xFFFFu;
			break;
		}
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