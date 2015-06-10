#include "stdafx.h"
#include "GBEmu.h"
#include "GPU.h"
#include <sstream>
#include "wx/dcbuffer.h"
#include "wx/image.h"
#include "ImagePanel.h"
#include "MainFrame.h"

using namespace std;

//MainApp class

wxIMPLEMENT_APP_NO_MAIN(MainApp);

BEGIN_EVENT_TABLE(MainApp, wxApp)
	EVT_TIMER(wxID_EXECUTE, MainApp::Update)
	EVT_KEY_DOWN(MainApp::KeyDown)
	EVT_KEY_UP(MainApp::KeyUp)
END_EVENT_TABLE()

bool MainApp::OnInit(){
	g = nullptr;
	wxInitAllImageHandlers();
	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
	frame = new MainFrame(wxT("yanGBe"), wxDefaultPosition, wxSize(320, 240), g);
	panel = new ImagePanel(frame);

	sizer->Add(panel, 1, wxEXPAND);
	frame->SetSizer(sizer);

	frame->Show();
	panel->SetFocus();

	wxTimer* timer = new wxTimer(this, wxID_EXECUTE);
	timer->Start(16);
	return true;
}

int MainApp::OnExit(){
	return wxApp::OnExit();
}

void MainApp::Resize(int newWidth, int newHeight){

}

void MainApp::Draw(){

}

void MainApp::KeyUp(wxKeyEvent &evt){
	if (g == nullptr)
		return;
	g->buttonUp(evt.GetKeyCode());
}

void MainApp::KeyDown(wxKeyEvent &evt){
	if (g == nullptr)
		return;
	g->buttonDown(evt.GetKeyCode());
}

void MainApp::Update(wxTimerEvent& event){
	if (frame->stateChangeRequested)
		return;
	if (g == nullptr)
		return;
	g->UpdateToVBlank();
	panel->setData(&g->g->data[0][0][0]);
	panel->paintNow();
}

int main(int argc, char* argv[]){

	return wxEntry(argc, argv);
}

std::string HexDec2String(int hexIn) {
	char hexString[4 * sizeof(int) + 1];
	// returns decimal value of hex
	sprintf(hexString, "%X", hexIn);
	return std::string(hexString);
}
