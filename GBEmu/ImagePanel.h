#pragma once
#include "wx\wx.h"

class ImagePanel : public wxPanel
{
	wxImage image;

public:
	unsigned char* data;
	ImagePanel(wxFrame* parent);
	~ImagePanel();

	void setData(unsigned char* d);

	void paintEvent(wxPaintEvent& event);
	void paintNow();
	void render(wxDC& dc);
	void OnSize(wxSizeEvent& event);
private:
	int w, h;

protected:
	DECLARE_EVENT_TABLE()
};

