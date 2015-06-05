#include "stdafx.h"
#include "MainFrame.h"
#include "GB.h"

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(wxID_OPEN, MainFrame::OnOpen)
END_EVENT_TABLE()

MainFrame::MainFrame(const wxChar* title, wxPoint pos, wxSize size, GB*& gb)
	: wxFrame((wxFrame*)nullptr, -1, title, pos, size)
{
	menuBar = new wxMenuBar();

	fileMenu = new wxMenu();
	fileMenu->Append(wxID_OPEN, _T("&Open"));

	menuBar->Append(fileMenu, _T("File"));

	SetMenuBar(menuBar);
	g = &gb;
}

MainFrame::~MainFrame()
{
}

void MainFrame::OnOpen(wxCommandEvent&){
	wxFileDialog* OpenDialog = new wxFileDialog(this, _T("Choose a ROM"), _T(""), wxEmptyString, _T("*.*"), wxFD_OPEN);
	if (OpenDialog->ShowModal() == wxID_OK){
		(*g) = new GB(OpenDialog->GetPath().c_str().AsChar());
	}
	OpenDialog->Close();
}