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
	EVT_KEY_DOWN(OptionFrame::KeyDown)
	EVT_CLOSE(OptionFrame::OnClose)
END_EVENT_TABLE()

OptionFrame::OptionFrame(const wxSize& size) : wxFrame(NULL, wxID_PROPERTIES, _T("Options"), wxDefaultPosition, size)
{
	panel = new wxPanel(this, -1);

	wxBoxSizer* hbox = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* flex = new wxFlexGridSizer(2, 4, 9, 25);

	buttons[btn_A] = new wxToggleButton(panel, btn_A, _T("A"));
	buttons[btn_B] = new wxToggleButton(panel, btn_B, _T("B"));
	buttons[btn_START] = new wxToggleButton(panel, btn_START, _T("START"));
	buttons[btn_SELECT] = new wxToggleButton(panel, btn_SELECT, _T("SELECT"));
	buttons[btn_UP] = new wxToggleButton(panel, btn_UP, _T("UP"));
	buttons[btn_DOWN] = new wxToggleButton(panel, btn_DOWN, _T("DOWN"));
	buttons[btn_LEFT] = new wxToggleButton(panel, btn_LEFT, _T("LEFT"));
	buttons[btn_RIGHT] = new wxToggleButton(panel, btn_RIGHT, _T("RIGHT"));

	text = new wxStaticText(panel, wxID_ANY, "");

	for (int i = 0; i < 8; i++)
		flex->Add(buttons[i]);

	hbox->Add(flex, 1, wxALL | wxEXPAND, 15);
	hbox->Add(text);
	panel->SetSizer(hbox);

	this->ToggleWindowStyle(wxTAB_TRAVERSAL);
	panel->ToggleWindowStyle(wxTAB_TRAVERSAL);
	panel->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(OptionFrame::KeyDown), NULL, this);

	buttonAssignments[0] = 'A';
	buttonAssignments[1] = 'B';
	buttonAssignments[2] = WXK_RETURN;
	buttonAssignments[3] = '\\';
	buttonAssignments[4] = WXK_UP;
	buttonAssignments[5] = WXK_DOWN;
	buttonAssignments[6] = WXK_LEFT;
	buttonAssignments[7] = WXK_RIGHT;

	Center();
}


OptionFrame::~OptionFrame()
{

}

void OptionFrame::ButtonPressed(wxCommandEvent& evt){
	bool pressed = false;
	for (int i = 0; i < 8; i++){
		if (i != evt.GetId())
			buttons[i]->SetValue(false);
		if (!pressed && buttons[i]->GetValue())
			pressed = true;
	}	

	if (pressed)
		text->SetLabelText("Press the button to assign to " + buttons[evt.GetId()]->GetLabelText() + ".");
	else
		text->SetLabelText("");
}

void OptionFrame::KeyDown(wxKeyEvent& evt){
	//find which button is toggled
	int toggledButton;
	for (toggledButton = 0; !buttons[toggledButton]->GetValue(); toggledButton++);

	buttonAssignments[toggledButton] = evt.GetKeyCode();

	buttons[toggledButton]->SetValue(false);
	text->SetLabelText("");
}

void OptionFrame::OnClose(wxCloseEvent&){
	this->Hide();
}