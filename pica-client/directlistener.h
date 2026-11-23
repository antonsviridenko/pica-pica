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
#ifndef DIRECTLISTENER_H
#define DIRECTLISTENER_H

#include <QObject>
#include <QString>
#include <QTimer>

struct PICA_upnp_ctx;

// Opens the listener for incoming direct connections and keeps its UPnP port
// mapping up for as long as the client runs.
//
// Talking to a UPnP router blocks - discovery for two seconds, every request
// for as long as the router takes to answer - so this lives on a thread of
// its own instead of SkyNet's, which carries the media of calls. The
// listener it opens is handed over to SkyNet and used on SkyNet's thread
// only; what changes about the mapping later on is reported with
// upnpChanged(), for SkyNet to pass on to the listener.
class DirectListener : public QObject
{
	Q_OBJECT
public:
	// Ports tried in automatic configuration, from DirectC2CSettings::DefaultPort
	// on, until one can be opened for TCP and UDP and - with UPnP - forwarded.
	enum
	{
		PortTries = 16
	};

	explicit DirectListener(QObject *parent = 0);
	~DirectListener();

public slots:
	// Opens the listener as the "Direct connections" settings say and reports
	// the outcome with ready(). addrList is DirectC2CSettings' stored form.
	void setup(quintptr acc, int addrConfig, bool useUpnp, int localPort, QString addrList);

	// Deletes the port mapping. Called once, at shutdown.
	void release();

signals:
	// listener is a struct PICA_listener *, 0 if none could be opened.
	// upnpAddr is empty when no port mapping was made.
	void ready(quintptr listener, QString upnpAddr, int upnpPort);

	// The external address or port of the mapping changed - the router got a
	// new address from the provider - or the mapping was lost, in which case
	// upnpAddr is empty.
	void upnpChanged(QString upnpAddr, int upnpPort);

	void statusMsg(QString msg, bool isCritical);

private slots:
	void refresh();

private:
	struct PICA_upnp_ctx *upnp;
	QString upnpAddr;
	// the port the mapping is for, the same number as the listening port
	int upnpPort;
	bool upnpLost;
	QTimer *refreshTimer;

	bool discoverUpnp();
	void dropUpnp();
	void scheduleRefresh();
};

#endif // DIRECTLISTENER_H
