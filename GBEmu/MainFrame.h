#include "wx\wx.h"

class GB;
class OptionFrame;

class MainFrame : public wxFrame
{

public:
	MainFrame(const wxChar* title, wxPoint pos, wxSize size, GB*& g);
	~MainFrame();

	void OnOpen(wxCommandEvent& event);
	void OnExit(wxCommandEvent& event);
	void LoadState(wxCommandEvent& event);
	void SaveState(wxCommandEvent& event);
	void ShowOptions(wxCommandEvent& event);

	bool stateChangeRequested;
	OptionFrame* options;

protected:
	DECLARE_EVENT_TABLE()

private:
	GB** g;
	wxMenuBar* menuBar;
	wxMenu* fileMenu;
	wxMenu* toolMenu;

};
