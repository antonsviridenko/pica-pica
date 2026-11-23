
/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
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
#ifndef CALLWINDOW_H
#define CALLWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QSize>
#include "videoscaler.h"
#ifdef HAVE_VAAPI
#include "vaapi.h"
#endif

class QEvent;

class CallWindow : public QWidget
{
	Q_OBJECT
public:
	explicit CallWindow(QByteArray peer_id, bool incoming);
signals:
	void start_call_pressed();
	void accept_call_pressed();
	void hang_call_pressed();
	void callwindow_closed(CallWindow *sender_window);

	// The area the remote video has to be drawn in, whenever it changes. The
	// decoder produces frames at this size directly rather than the window
	// resizing them after the fact - see VideoScaler for why that is worth
	// routing across threads for.
	void video_area_changed(QSize area);
#ifdef HAVE_VAAPI
	// Drawing frames straight out of GPU memory did not work on this system.
	// The call controller answers by having them decoded into system memory
	// instead, which the label below can show.
	void video_rendering_failed();
#endif
private:
	QByteArray m_peer_id;
	bool is_incoming;
	QPushButton *pbAccept;
	QPushButton *pbCall;
	QPushButton *pbHang;
	QLabel *lbTimer;
	// How the call's media is being carried, and under which ciphersuite.
	// Hidden until that is known.
	QLabel *lbTransport;
	// Displays the remote side's video. Stays hidden until the first frame
	// arrives, so an audio-only call keeps the compact window it had before.
	QLabel *lbVideo;
#ifdef HAVE_VAAPI
	// Used instead of lbVideo when frames arrive still living in GPU memory;
	// only one of the two is ever visible. Which of the two renderers this is
	// depends on the session - see VaapiRenderWidget::create().
	VaapiRenderWidget *videoGpu;
#endif
	QTimer *callTimer;
	int callElapsedSeconds;

	// The last frame received, kept so that the picture can follow a resize
	// instead of staying as it was until the next frame arrives.
	QImage lastRemoteFrame;

	// Resizes a frame that does not already match the video area. Normally
	// does nothing: the decoder is told what size to produce, and only the
	// frames in flight across a resize arrive at another size.
	VideoScaler videoScaler;

	// Whether the video area has been given room yet, see showRemoteFrame().
	bool videoAreaOpened;

	// Draws lastRemoteFrame at the size the video area is now.
	void updateRemoteFrame();

	// Tells the decoder what size to produce frames at, if that has changed.
	void reportVideoArea();

	// Enlarges the window to fit the first frame's own resolution, once. An
	// audio call keeps the compact window it had before, so when video does
	// turn up there is no room for it; without this it would appear in
	// whatever few pixels the layout could spare.
	void openVideoArea(QSize frame);

	void closeEvent(QCloseEvent *e);

	// Watches lbVideo for its own resizes, which is where the video area's
	// size is really decided. Doing this from the window's resizeEvent()
	// instead would read the label's geometry before the layout has had a
	// chance to update it, and would miss the resizes the layout causes by
	// itself - lbTransport appearing shortens the video area without the
	// window changing size at all.
	bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
	void call_started();
	void call_ended();
	// direct_udp tells whether the media is being carried over a direct
	// mediac2c connection or over the c2c connection the call was set up on.
	void setMediaTransport(bool direct_udp, QString ciphersuitename);
	void showRemoteFrame(QImage frame);
#ifdef HAVE_VAAPI
	void showRemoteHwFrame(AVFramePtr frame);
#endif
private slots:
	void call();
	void accept();
	void hang();
	void update_call_timer();
#ifdef HAVE_VAAPI
	void gpu_rendering_failed(QString message);
#endif
};

#endif

