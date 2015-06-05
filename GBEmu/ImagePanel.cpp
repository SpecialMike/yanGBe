#include "stdafx.h"
#include "ImagePanel.h"

BEGIN_EVENT_TABLE(ImagePanel, wxPanel)
	EVT_PAINT(ImagePanel::paintEvent)
END_EVENT_TABLE()

ImagePanel::ImagePanel(wxFrame* parent) : wxPanel(parent)
{
	data = nullptr;
	image.Create(160, 144, false);
	image.Clear(0);
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
	dc.DrawBitmap(image, 0, 0, false);
}