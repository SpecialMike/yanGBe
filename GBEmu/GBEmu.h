#ifndef GBEMU_H
#define GBEMU_H

#include "GPU.h"
#include "MMU.h"
#include "CPU.h"
#include "wx\wx.h"
#include "GB.h"

#define IE m->ReadByte(0xFFFF)
#define IF m->ReadByte(0xFF0F)
#define TIMA m->ReadByte(0xFF05)
#define TMA m->ReadByte(0xFF06)
#define TMC m->ReadByte(0xFF07)
#define DIV m->ReadByte(0xFF04)

#define LCDC m->ReadByte(0xFF40u)
#define STAT m->ReadByte(0xFF41u)
#define SCY m->ReadByte(0xFF42u)
#define SCX m->ReadByte(0xFF43u)
#define LY m->ReadByte(0xFF44u)
#define LYC m->ReadByte(0xFF45u)
#define BGP m->ReadByte(0xFF47u)
#define WY m->ReadByte(0xFF4Au)
#define WX m->ReadByte(0xFF4Bu)

#define uint8 unsigned _int8
#define uint16 unsigned _int16
#define uint32 unsigned _int32

class MainFrame;
class ImagePanel;

class MainApp : public wxApp{

public:
	void Draw();
	void KeyDown(wxKeyEvent &evt);
	void KeyUp(wxKeyEvent &evt);
	virtual bool OnInit();
	virtual int OnExit();
	virtual void Resize(int newWidth, int newHeight);
	void Update(wxTimerEvent& event);

	MainFrame* frame;
	GB* g;
	ImagePanel* panel;

protected:
	DECLARE_EVENT_TABLE()

private:
	unsigned long long lastUpdate;
};
DECLARE_APP(MainApp)

#endif