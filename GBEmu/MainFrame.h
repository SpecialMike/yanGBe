#include "wx\wx.h"

class GB;

class MainFrame : public wxFrame
{

public:
	MainFrame(const wxChar* title, wxPoint pos, wxSize size, GB*& g);
	~MainFrame();

	void OnOpen(wxCommandEvent& event);
	void OnExit(wxCommandEvent& event);
	void LoadState(wxCommandEvent& event);
	void SaveState(wxCommandEvent& event);

	bool stateChangeRequested;

protected:
	DECLARE_EVENT_TABLE()

private:
	GB** g;
	wxMenuBar* menuBar;
	wxMenu* fileMenu;

};
