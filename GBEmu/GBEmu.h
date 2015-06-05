#ifndef GBEMU_H
#define GBEMU_H

#include "GPU.h"
#include "MMU.h"
#include "CPU.h"
#include <SDL.h>
#include "wx\wx.h"
#include "GB.h"

#define IE m->readByte(0xFFFF)
#define IF m->readByte(0xFF0F)
#define TIMA m->readByte(0xFF05)
#define TMA m->readByte(0xFF06)
#define TMC m->readByte(0xFF07)
#define DIV m->readByte(0xFF04)

#define LCDC m->readByte(0xFF40u)
#define STAT m->readByte(0xFF41u)
#define SCY m->readByte(0xFF42u)
#define SCX m->readByte(0xFF43u)
#define LY m->readByte(0xFF44u)
#define LYC m->readByte(0xFF45u)
#define BGP m->readByte(0xFF47u)
#define WY m->readByte(0xFF4Au)
#define WX m->readByte(0xFF4Bu)

#define uint8 unsigned _int8
#define uint16 unsigned _int16
#define uint32 unsigned _int32

class MainFrame;
class ImagePanel;

class MainApp : public wxApp{

public:
	virtual bool OnInit();
	virtual int OnExit();
	virtual void Resize(int newWidth, int newHeight);
	GB* g;
	MainFrame* frame;
	void Draw();
	void KeyUp(int key);
	void KeyDown(int key);
	void Update(wxTimerEvent& event);
	ImagePanel* panel;
protected:
	DECLARE_EVENT_TABLE()
};
DECLARE_APP(MainApp)

#endif