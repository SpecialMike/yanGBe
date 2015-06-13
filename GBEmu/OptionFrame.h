#pragma once
#include "wx\wx.h"
#include "wx/tglbtn.h"
#include <string>

static const std::string buttons[8] = { "A", "B", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT" };
class OptionFrame :	public wxFrame
{
public:
	OptionFrame(const wxSize& size);
	~OptionFrame();

	void ButtonPressed(wxCommandEvent& event);

protected:
	DECLARE_EVENT_TABLE()

private:
	enum buttonID{
		btn_A, btn_B, btn_START, btn_SELECT, btn_UP, btn_DOWN, btn_LEFT, btn_RIGHT
	};

	wxToggleButton* A;
	wxToggleButton* B;
	wxToggleButton* START;
	wxToggleButton* SELECT;
	wxToggleButton* UP;
	wxToggleButton* DOWN;
	wxToggleButton* LEFT;
	wxToggleButton* RIGHT;
	wxStaticText* text;

};

