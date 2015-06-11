#include "stdafx.h"
#include "ImagePanel.h"

BEGIN_EVENT_TABLE(ImagePanel, wxPanel)
	EVT_PAINT(ImagePanel::paintEvent)
	EVT_SIZE(ImagePanel::OnSize)
END_EVENT_TABLE()

ImagePanel::ImagePanel(wxFrame* parent) : wxPanel(parent)
{
	data = nullptr;
	image.Create(160, 144, false);
	image.Clear(0);
	this->ToggleWindowStyle(wxTAB_TRAVERSAL);
	w = -1;
	h = -1;
}

ImagePanel::~ImagePanel()
{
	image.Destroy();
}

void ImagePanel::setData(unsigned char* d){
	image.SetData(d, true);
}

void ImagePanel::paintEvent(wxPaintEvent&){
	wxPaintDC dc(this);
	render(dc);
}

void ImagePanel::paintNow(){
	wxClientDC dc(this);
	render(dc);
}

void ImagePanel::render(wxDC& dc){
	wxBitmap resized = wxBitmap(image.Scale(w, h));
	dc.DrawBitmap(resized, 0, 0, false);
}

void ImagePanel::OnSize(wxSizeEvent& event){
	wxClientDC dc(this);
	dc.GetSize(&w, &h);
	Refresh();
	event.Skip();
}