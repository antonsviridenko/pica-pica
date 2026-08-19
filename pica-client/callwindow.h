
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
#ifdef HAVE_VAAPI
#include "vaapi.h"
#endif

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

	void closeEvent(QCloseEvent *e);

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

