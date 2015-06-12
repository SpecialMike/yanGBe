#pragma once
#include "wx\wx.h"

class ImagePanel : public wxPanel
{

public:
	ImagePanel(wxFrame* parent);
	~ImagePanel();

	void OnSize(wxSizeEvent& event);
	void PaintEvent(wxPaintEvent& event);
	void PaintNow();
	void Render(wxDC& dc);
	void SetData(unsigned char* d);

	unsigned char* data;
	wxImage image;

protected:
	DECLARE_EVENT_TABLE()

private:
	int w, h;

};

