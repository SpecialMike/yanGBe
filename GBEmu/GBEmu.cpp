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
	EVT_KEY_DOWN(MainApp::KeyDown)
	EVT_KEY_UP(MainApp::KeyUp)
	EVT_IDLE(MainApp::Update)
END_EVENT_TABLE()

bool MainApp::OnInit(){
	g = nullptr;
	wxInitAllImageHandlers();
	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	//Size is weird, 160x144 for the screen, 20 height for the tool bar and then some random amount.
	frame = new MainFrame(wxT("yanGBe"), wxDefaultPosition, wxSize(160 + 16, 144 + 20 + 37), g);
	panel = new ImagePanel(frame);

	sizer->Add(panel, 1, wxEXPAND);
	frame->SetSizer(sizer);

	frame->Show();
	panel->SetFocus();

	watch = new wxStopWatch();
	
	lastUpdate = 0;
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
	g->ButtonUp(evt.GetKeyCode());
}

void MainApp::KeyDown(wxKeyEvent &evt){
	if (g == nullptr)
		return;
	g->ButtonDown(evt.GetKeyCode());
}

void MainApp::Update(wxIdleEvent& event){
	wxLongLong thisUpdate = watch->TimeInMicro();
	if (thisUpdate - lastUpdate < 16750){	// 1/59.7 = 16750 microseconds
		event.RequestMore();
	}
	else{
		if (frame->stateChangeRequested)
			return;
		if (g == nullptr)
			return;
		double FPS = (1 / (double)(thisUpdate.ToLong() - lastUpdate.ToLong())) * 1000000;
		lastUpdate = thisUpdate;
		string fpsString = to_string(CalculateFPS(FPS));
		fpsString.resize(4);
		frame->SetTitle("yanGBe " + fpsString);

		g->UpdateToVBlank();
		panel->SetData(&g->g->data[0][0][0]);
		panel->PaintNow();

		event.RequestMore();
	}
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

//returns a moving average of the last FPS_SAMPLES updates
double MainApp::CalculateFPS(double lastFPS){
	fpsSum -= fpsList[fpsIndex];
	fpsSum += lastFPS;
	fpsList[fpsIndex] = lastFPS;
	fpsIndex = (fpsIndex + 1) % FPS_SAMPLES;

	return (fpsSum / FPS_SAMPLES);
}