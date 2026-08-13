
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
#include "vaapivideowidget.h"
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
private:
	QByteArray m_peer_id;
	bool is_incoming;
	QPushButton *pbAccept;
	QPushButton *pbCall;
	QPushButton *pbHang;
	QLabel *lbTimer;
	// Displays the remote side's video. Stays hidden until the first frame
	// arrives, so an audio-only call keeps the compact window it had before.
	QLabel *lbVideo;
#ifdef HAVE_VAAPI
	// Used instead of lbVideo when frames arrive still living in GPU memory;
	// only one of the two is ever visible.
	VaapiVideoWidget *videoGpu;
#endif
	QTimer *callTimer;
	int callElapsedSeconds;

	void closeEvent(QCloseEvent *e);

public slots:
	void call_started();
	void call_ended();
	void showRemoteFrame(QImage frame);
#ifdef HAVE_VAAPI
	void showRemoteHwFrame(AVFramePtr frame);
#endif
private slots:
	void call();
	void accept();
	void hang();
	void update_call_timer();
};

#endif

