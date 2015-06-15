#pragma once
#include "wx\wx.h"
#include "wx/tglbtn.h"
#include <string>

class OptionFrame :	public wxFrame
{
public:
	enum buttonID{
		btn_A, btn_B, btn_START, btn_SELECT, btn_UP, btn_DOWN, btn_LEFT, btn_RIGHT
	};

	OptionFrame(const wxSize& size);
	~OptionFrame();

	void ButtonPressed(wxCommandEvent& event);
	void KeyDown(wxKeyEvent& event);

	wxToggleButton* buttons[8];
	int buttonAssignments[8];
	wxPanel* panel;

protected:
	DECLARE_EVENT_TABLE()

private:
	wxStaticText* text;

};

