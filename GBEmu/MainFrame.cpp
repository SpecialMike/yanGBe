#include "stdafx.h"
#include "MainFrame.h"
#include "GB.h"
#include "OptionFrame.h"

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(wxID_OPEN, MainFrame::OnOpen)
	EVT_MENU(wxID_EXIT, MainFrame::OnExit)
	EVT_MENU(1, MainFrame::LoadState)
	EVT_MENU(wxID_SAVE, MainFrame::SaveState)
	EVT_MENU(2, MainFrame::ShowOptions)
END_EVENT_TABLE()

MainFrame::MainFrame(const wxChar* title, wxPoint pos, wxSize size, GB*& gb)
	: wxFrame((wxFrame*)nullptr, -1, title, pos, size)
{
	options = new OptionFrame(wxSize(500, 200));

	menuBar = new wxMenuBar();

	fileMenu = new wxMenu();
	fileMenu->Append(wxID_OPEN, _T("&Open"));
	fileMenu->Append(wxID_EXIT, _T("&Exit"));
	fileMenu->Append(wxID_SAVE, _T("&Save State"));
	fileMenu->Append(1, _T("&Load State"));
	menuBar->Append(fileMenu, _T("File"));
	
	toolMenu = new wxMenu();
	toolMenu->Append(2, _T("O&ptions"));
	menuBar->Append(toolMenu, _T("Tools"));

	stateChangeRequested = false;
	SetMenuBar(menuBar);
	g = &gb;
}

MainFrame::~MainFrame()
{
}

void MainFrame::OnOpen(wxCommandEvent&){
	wxFileDialog* OpenDialog = new wxFileDialog(this, _T("Choose a ROM"), _T(""), wxEmptyString, _T("*.*"), wxFD_OPEN);
	if (OpenDialog->ShowModal() == wxID_OK){
		try{
			(*g) = new GB(OpenDialog->GetPath().c_str().AsChar());
			(*g)->SetOptionFrame(options);
		}
		catch (const char* ex){
			wxMessageDialog dialog(NULL, ex, _T(""), wxICON_ERROR);
			dialog.ShowModal();
		}
	}
	OpenDialog->Close();
}

void MainFrame::OnExit(wxCommandEvent&){
	this->Close();
}

void MainFrame::LoadState(wxCommandEvent&){
	stateChangeRequested = true;
	if (*g == nullptr)
		return;
	wxFileDialog* OpenDialog = new wxFileDialog(NULL, _T("Choose a Save State"), _T(""), wxEmptyString, _T("*.*"), wxFD_OPEN);
	if (OpenDialog->ShowModal() == wxID_OK){
		try{
			(*g)->LoadState(OpenDialog->GetPath().c_str().AsChar());
		}
		catch (const char* ex){
			wxMessageDialog dialog(NULL, ex, _T(""), wxICON_ERROR);
			dialog.ShowModal();
		}
	}
	OpenDialog->Close();
	stateChangeRequested = false;
}

void MainFrame::SaveState(wxCommandEvent&){
	stateChangeRequested = true;
	if (*g == nullptr)
		return;
	wxFileDialog* SaveDialog = new wxFileDialog(NULL, _T("Choose a Save State"), _T(""), wxEmptyString, _T("*.*"), wxFD_SAVE);
	if (SaveDialog->ShowModal() == wxID_OK){
		(*g)->SaveState(SaveDialog->GetPath().c_str().AsChar());
	}
	SaveDialog->Close();
	stateChangeRequested = false;
}

void MainFrame::ShowOptions(wxCommandEvent&){
	options->Show();
}