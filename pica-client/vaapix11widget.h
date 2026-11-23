/*
	(c) Copyright  2012 - 2026 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef VAAPIX11WIDGET_H
#define VAAPIX11WIDGET_H

#ifdef HAVE_VAAPI_X11

#include "vaapi.h"

#include <QWidget>
#include <QString>

// Draws decoded frames by handing the surface to the driver with the widget's
// own X window as the target - vaPutSurface() - rather than going through
// OpenGL at all. The picture never leaves the GPU; the driver does the colour
// conversion and the scaling.
//
// This is the path for an X11 session, where Qt's OpenGL contexts come from
// GLX and cannot import the DMA-BUF handles a surface is exported as. Since it
// touches no OpenGL, it does not care which of the two Qt picked.
//
// The cost of drawing outside Qt is that Qt must be kept away from this
// widget's area: it is given a native window of its own, Qt is told not to
// paint or clear it (WA_PaintOnScreen, WA_NoSystemBackground, a null paint
// engine), and anything drawn over it - a menu, a tooltip - will fight with
// the video underneath.
class VaapiX11Widget : public QWidget, public VaapiRenderWidget
{
	Q_OBJECT
public:
	explicit VaapiX11Widget(QWidget *parent = nullptr);
	~VaapiX11Widget() override;

	virtual QWidget *widget() override { return this; }
	virtual bool isWorking() const override { return m_working; }

public slots:
	virtual void setFrame(AVFramePtr frame) override;
	virtual void clearFrame() override;

signals:
	// Raised once if a frame cannot be put on screen, carrying the reason.
	// The widget stops trying after that.
	void renderingFailed(QString message);

protected:
	// Nothing about this widget is painted by Qt, so it has no paint engine to
	// offer. Returning one would have Qt draw over the video.
	QPaintEngine *paintEngine() const override { return nullptr; }

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	// Puts the current frame on the window, letterboxed to keep its aspect
	// ratio. Returns false if the driver would not draw it.
	bool putFrame();

	// Paints the whole window black, so that the bars beside a letterboxed
	// picture are not left holding whatever was there before.
	void clearWindow();

	void reportFailure(const QString &message);

	AVFramePtr m_frame;
	bool m_working;
	bool m_reportedFailure;
	bool m_describedSurface;
	bool m_everDrew;

	// Putting a surface on a window can fail for reasons that pass - the
	// window not being mapped yet at the moment the first frame arrives, most
	// obviously - so unlike an EGL import, which either works or never will,
	// this one is worth retrying. Only a long run of failures means the path
	// is really unusable.
	int m_failuresInARow;
	static const int kMaxFailuresInARow = 30;
};

#endif // HAVE_VAAPI_X11

#endif // VAAPIX11WIDGET_H
