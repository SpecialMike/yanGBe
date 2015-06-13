#include "stdafx.h"
#include "OptionFrame.h"

BEGIN_EVENT_TABLE(OptionFrame, wxFrame)
	EVT_TOGGLEBUTTON(OptionFrame::btn_A,OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_B, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_START, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_SELECT, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_UP, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_DOWN, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_LEFT, OptionFrame::ButtonPressed)
	EVT_TOGGLEBUTTON(OptionFrame::btn_RIGHT, OptionFrame::ButtonPressed)
END_EVENT_TABLE()

OptionFrame::OptionFrame(const wxSize& size) : wxFrame(NULL, wxID_ANY, _T("Options"), wxDefaultPosition, size)
{
	wxPanel* panel = new wxPanel(this, -1);

	wxBoxSizer* hbox = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* flex = new wxFlexGridSizer(2, 4, 9, 25);

	A = new wxToggleButton(panel, btn_A, _T("A"));
	B = new wxToggleButton(panel, btn_B, _T("B"));
	START = new wxToggleButton(panel, btn_START, _T("START"));
	SELECT = new wxToggleButton(panel, btn_SELECT, _T("SELECT"));
	UP = new wxToggleButton(panel, btn_UP, _T("UP"));
	DOWN = new wxToggleButton(panel, btn_DOWN, _T("DOWN"));
	LEFT = new wxToggleButton(panel, btn_LEFT, _T("LEFT"));
	RIGHT = new wxToggleButton(panel, btn_RIGHT, _T("RIGHT"));
	text = new wxStaticText(panel, wxID_ANY, "");

	flex->Add(A);
	flex->Add(B);
	flex->Add(START);
	flex->Add(SELECT);
	flex->Add(UP);
	flex->Add(DOWN);
	flex->Add(LEFT);
	flex->Add(RIGHT);


	hbox->Add(flex, 1, wxALL | wxEXPAND, 15);
	hbox->Add(text);
	panel->SetSizer(hbox);
	Center();
}


OptionFrame::~OptionFrame()
{

}

void OptionFrame::ButtonPressed(wxCommandEvent& evt){
	if (evt.GetId() != btn_A)
		A->SetValue(false);
	if (evt.GetId() != btn_B)
		B->SetValue(false);
	if (evt.GetId() != btn_START)
		START->SetValue(false);
	if (evt.GetId() != btn_SELECT)
		SELECT->SetValue(false);
	if (evt.GetId() != btn_UP)
		UP->SetValue(false);
	if (evt.GetId() != btn_DOWN)
		DOWN->SetValue(false);
	if (evt.GetId() != btn_LEFT)
		LEFT->SetValue(false);
	if (evt.GetId() != btn_RIGHT)
		RIGHT->SetValue(false);

	bool pressed = A->GetValue() | B->GetValue() | START->GetValue() | SELECT->GetValue() | UP->GetValue() | DOWN->GetValue() | LEFT->GetValue() | RIGHT->GetValue();

	if (pressed)
		text->SetLabelText("Press the button to assign to " + buttons[evt.GetId()] + ".");
	else
		text->SetLabelText("");
}