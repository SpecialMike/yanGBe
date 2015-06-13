#include "stdafx.h"
#include "GPU.h"
#include "GBEmu.h"

int mode = 0;
int modeClock = 0;
int scanlineCounter = 456;	//456 cycles to draw a scanline

unsigned _int32 colors[4] = { 0xFFFFFFu, 0xC0C0C0u, 0x808080u, 0x000000u };
unsigned _int32 spriteColors[2][4] = { { 0xFFFFFFu, 0x808080u, 0x000000u, 0x000000u }, { 0xFFFFFFu, 0xC0C0C0u, 0x000000u, 0x000000u } };

GPU::GPU()
{
}

GPU::~GPU()
{
	for (int i = 0; i < 144; i++)
		delete[] &data[i];
	delete[] &data;
}

void GPU::Reset(MMU* mem, CPU* cpu){
	m = mem;
	c = cpu;

	return;
}

void GPU::Update(){

}

void GPU::SetLCDStatus(){
	uint8 currStatus = STAT;
	if (!((LCDC & (1 << 7)) > 0)){	//if the LCD is disabled, then reset the line counter, LY and change to mode 1
		scanlineCounter = 456;
		m->WriteByte(0xFF44u, 0);
		currStatus &= 0xFCu;
		currStatus |= 0x01u;
		m->WriteByte(0xFF41u, currStatus);
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
		c->RequestInterrupt(CPU::LCD);
	}

	if (LY == LYC){
		currStatus &= 0xFBu;	//Set the coincidence flag
		currStatus |= 0x04u;
		if ((currStatus & (1 << 6)) > 0)
			c->RequestInterrupt(CPU::LCD);
	}
	else{
		currStatus &= 0xFBu;
	}
	m->WriteByte(0xFF41, currStatus);
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
		uint8 line = LY;
		m->IncrementLY();

		if (line == 144){
			c->RequestInterrupt(CPU::VBLANK);
		}

		if (line > 153){	//VBLANK period is over, reset line to 0
			m->WriteByte(0xFF44, 0);
		}

		if (line < 144){	//the current line within the drawable space, so draw it
			uint8 currLCDC = LCDC;
			if ((currLCDC & 0x01u) > 0)
				DrawBGLine();
			if ((currLCDC & 0x02u) > 0)
				DrawSpriteLine();
		}
	}
}

uint8 GPU::mapPalette(uint8 colorNumber, uint8 palette){
	uint8 colorBits = palette & (0x03u << colorNumber * 2);
	colorBits >>= colorNumber * 2;
	return colorBits;
}

void GPU::DrawBGLine(){
	unsigned _int16 tileMapAddress;
	unsigned _int16 tileDataAddress;
	uint8 yPos;
	bool isUnsigned = true;
	bool drawWindow = false;	//is this line in the window
	uint8 LCDCVal = LCDC;
	uint8 WYVal = WY;
	uint8 LYVal = LY;
	uint8 SCXVal = SCX;
	uint8 WXVal = WX;
	uint8 BGPVal = BGP;	

	//Check if we are drawing the window
	if ((LCDCVal & (1 << 5)) > 0 && (WYVal < LYVal)){
		drawWindow = true;
	}

	if ((LCDCVal & (1 << 4)) > 0){	//is the tile data signed or not
		tileDataAddress = 0x8000u;
	}
	else{
		tileDataAddress = 0x8800u;
		isUnsigned = false;
	}

	if (drawWindow){	//determine the address for the background data
		if ((LCDCVal & (1 << 6)) > 0){
			tileMapAddress = 0x9C00u;
		}
		else{
			tileMapAddress = 0x9800u;
		}
	}
	else{
		if ((LCDCVal & (1 << 3)) > 0){
			tileMapAddress = 0x9C00u;
		}
		else{
			tileMapAddress = 0x9800u;
		}
	}

	if (drawWindow){
		yPos = LYVal - WYVal;	//what Y coordinate are we on (0-144)
	}
	else{
		yPos = SCY + LYVal;
	}

	//which row in the tile are we on
	uint16 tileRow = (((uint16)(yPos / 8)) * 32);
	int oldTileAddress = -1;

	uint8 upperData, lowerData;

	//draw the scanline
	for (int i = 0; i < 160; i++){
		uint8 xPos = i + SCXVal;
		if (drawWindow && i >= WXVal-7)
			xPos = i - (WXVal-7);

//		if ( (i + SCXVal) % 8 == 0){	//only read from the MMU when needed.
			uint16 tileCol = xPos / 8;
			uint16 tileAddress = tileMapAddress + tileRow + tileCol;
			_int8 tileNum;
			if (isUnsigned){
				tileNum = m->ReadVram(tileAddress);
			}
			else{
				tileNum = (_int8)m->ReadVram(tileAddress);
			}

			uint16 tileLocation = tileDataAddress;
			if (isUnsigned){
				tileLocation += (((uint8)tileNum) * 16);
			}
			else{
				tileLocation += ((tileNum + 128) * 16);
//		}

			uint8 line = yPos % 8;
			line *= 2;
			upperData = m->ReadVram(tileLocation + line);
			lowerData = m->ReadVram(tileLocation + line + 1);
		}

		int colorBit = xPos % 8;	//which bit are we looking at in the line's data
		colorBit -= 7;
		colorBit *= -1;

		uint8 color = ((lowerData & (1 << colorBit)) > 0) ? 0x2u : 0;	//get the color number
		color |= ((upperData & (1 << colorBit)) > 0) ? 0x1u : 0;
		uint8 palette = mapPalette(color, BGPVal);

		//data[i][LY] = palette;
		switch (palette){
		case 0x00u:
			data[LYVal - 1][i][0] = 0xFFu;
			data[LYVal - 1][i][1] = 0xFFu;
			data[LYVal - 1][i][2] = 0xFFu;
			break;
		case 0x01u:
			data[LYVal - 1][i][0] = 0xCCu;
			data[LYVal - 1][i][1] = 0xCCu;
			data[LYVal - 1][i][2] = 0xCCu;
			break;
		case 0x02u:
			data[LYVal - 1][i][0] = 0x77u;
			data[LYVal - 1][i][1] = 0x77u;
			data[LYVal - 1][i][2] = 0x77u;
			break;
		case 0x03u:
			data[LYVal - 1][i][0] = 0x00u;
			data[LYVal - 1][i][1] = 0x00u;
			data[LYVal - 1][i][2] = 0x00u;
			break;
		}
	}
}

//TODO: The number of sprites on one scanline is limited to 10
void GPU::DrawSpriteLine(){
	//check if we are using tall sprites
	bool tallSprites = ((LCDC & 0x4) > 0);
	uint8 LYVal = LY;
	uint8 BGPVal = BGP;

	//iterate through each sprite in OAM (0xFE00-0xFE9F)
	for (uint16 spriteAddress = 0xFE00; spriteAddress <= 0xFE9F; spriteAddress += 4){
		uint8 yCoord = m->ReadByte(spriteAddress);
		uint8 xCoord = m->ReadByte(spriteAddress + 1);
		if ((xCoord == 0) | (yCoord == 0)){	//sprite is hidden
			continue;
		}
		yCoord -= 16;
		xCoord -= 8;
		if (yCoord <= LYVal && (yCoord + (tallSprites ? 16 : 8)) > LYVal){	//check that the current scanline falls between the sprite's Y-coords
			uint8 patternNumber = m->ReadByte(spriteAddress + 2);
			if (tallSprites)
				patternNumber &= 0xFE;	//In tall mode, the LSB is always 0
			uint8 spriteFlags = m->ReadByte(spriteAddress + 3);
			bool xFlip = (spriteFlags & 0x20) > 0;
			bool yFlip = (spriteFlags & 0x40) > 0;
			bool priority = (spriteFlags & 0x80) > 0;
			uint8 rowDataUpper;
			uint8 rowDataLower;
			if (!yFlip){
				rowDataUpper = m->ReadByte(0x8000 + (patternNumber * 16) + ((LYVal - yCoord) * 2));
				rowDataLower = m->ReadByte(0x8000 + (patternNumber * 16) + ((LYVal - yCoord) * 2) + 1);
			}
			else{
				rowDataUpper = m->ReadByte(0x8000 + (patternNumber * 16) + ((7-(LYVal - yCoord)) * 2));
				rowDataLower = m->ReadByte(0x8000 + (patternNumber * 16) + ((7-(LYVal - yCoord)) * 2) + 1);
			}
			
			for (int x = 0; x < 8; x++){
				//Pixel is onscreen and it has priority or BG color is 0
				if ((xCoord + x >= 0) && ((xCoord + x) < 160) && (!priority || *data[LYVal-1][xCoord + x] == 0xFF)){
					//check if this pixel's color is 0
					int xTemp = x;
					if (xFlip)
						xTemp = 7 - x;
					xTemp -= 7;
					xTemp *= -1;
					uint8 color = ( (rowDataUpper & (1 << xTemp)) > 0) ? 0x2 : 0;
					color |= ( (rowDataLower & (1 << xTemp)) > 0) ? 0x1 : 0;
					if (color != 0){
						uint8 palette = mapPalette(color, BGPVal);
						switch (palette){
						case 0x00u:
							data[LYVal - 1][xCoord + x][0] = 0xFFu;
							data[LYVal - 1][xCoord + x][1] = 0xFFu;
							data[LYVal - 1][xCoord + x][2] = 0xFFu;
							break;
						case 0x01u:
							data[LYVal - 1][xCoord + x][0] = 0x77u;
							data[LYVal - 1][xCoord + x][1] = 0x77u;
							data[LYVal - 1][xCoord + x][2] = 0x77u;
							break;
						case 0x02u:
							data[LYVal - 1][xCoord + x][0] = 0xCCu;
							data[LYVal - 1][xCoord + x][1] = 0xCCu;
							data[LYVal - 1][xCoord + x][2] = 0xCCu;
							break;
						case 0x03u:
							data[LYVal - 1][xCoord + x][0] = 0x00u;
							data[LYVal - 1][xCoord + x][1] = 0x00u;
							data[LYVal - 1][xCoord + x][2] = 0x00u;
							break;
						}
					}
				}
			}
		}
	}
	
}

void GPU::UpdateLine(){
	if (LCDC & 0x1u){
		DrawBGLine();
	}
	if (LCDC & 0x2u){
		DrawSpriteLine();
	}
}