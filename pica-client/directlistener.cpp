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
#include "directlistener.h"
#include "directc2csettings.h"
#include "../PICA_client.h"
#include "../PICA_netconf.h"
#include <QByteArray>
#include <QList>
#include <vector>

// How soon a lost mapping is tried again, in seconds.
static const int kUpnpRetryInterval = 300;

DirectListener::DirectListener(QObject *parent)
	: QObject(parent), upnp(nullptr), upnpPort(0), upnpLost(false), refreshTimer(nullptr)
{
}

DirectListener::~DirectListener()
{
	// release() is expected to have run on this object's own thread already;
	// this is only for a mapping left behind when it did not
	PICA_upnp_free(upnp);
}

bool DirectListener::discoverUpnp()
{
	char addr[64];
	int ret;

	ret = PICA_upnp_discover(PICA_UPNP_DISCOVER_TIMEOUT, &upnp);

	if (ret != PICA_UPNP_OK)
	{
		upnp = nullptr;
		emit statusMsg(tr("UPnP: %1").arg(PICA_upnp_strerror(ret)), false);
		return false;
	}

	// An external address that is private - the router is behind the
	// provider's NAT - would lead nowhere; better not to forward anything
	// than to announce it.
	ret = PICA_upnp_external_addr(upnp, addr, sizeof(addr));

	if (ret != PICA_UPNP_OK)
	{
		emit statusMsg(tr("UPnP: %1").arg(PICA_upnp_strerror(ret)), false);
		dropUpnp();
		return false;
	}

	upnpAddr = QString::fromLatin1(addr);

	return true;
}

void DirectListener::dropUpnp()
{
	PICA_upnp_free(upnp);
	upnp = nullptr;
	upnpAddr.clear();
	upnpPort = 0;
}

void DirectListener::setup(quintptr accp, int addrConfig, bool useUpnp, int localPort, QString addrList)
{
	const struct PICA_acc *acc = (const struct PICA_acc*)accp;
	struct PICA_listener *l = nullptr;
	bool manual = addrConfig == DirectC2CSettings::AddrConfigManual;
	QList<DirectC2CSettings::Addr> addrs = DirectC2CSettings::parseAddrs(addrList);
	// In automatic configuration a port that cannot be had is not worth a
	// word: the next one is tried. The port of the manual one is the user's
	// choice and is used as it is.
	int firstPort = manual ? localPort : (int)DirectC2CSettings::DefaultPort;
	int tries = manual ? 1 : (int)PortTries;
	bool mapped = false;
	int port = 0;
	int i, ret;

	if (useUpnp)
		discoverUpnp();

	for (i = 0; i < tries && firstPort + i <= 65535; i++)
	{
		port = firstPort + i;

		if (PICA_new_listener(acc, port, &l) != PICA_OK)
		{
			l = nullptr;
			continue;
		}

		if (!upnp)
			break;

		ret = PICA_upnp_map(upnp, port, PICA_UPNP_LEASE_DEFAULT, "Pica Pica");

		if (ret == PICA_UPNP_OK)
		{
			mapped = true;
			break;
		}

		// somebody else behind the router has this port number forwarded
		if (ret == PICA_UPNP_ERRCONFLICT && i + 1 < tries)
		{
			PICA_close_listener(l);
			l = nullptr;
			continue;
		}

		emit statusMsg(tr("UPnP: failed to forward port %1 - %2").arg(port).arg(PICA_upnp_strerror(ret)), false);
		dropUpnp();
		break;
	}

	if (!l)
	{
		if (manual)
			emit statusMsg(tr("Failed to open port %1 for incoming direct connections").arg(firstPort), true);
		else
			emit statusMsg(tr("Failed to open any of the ports %1 - %2 for incoming direct connections")
			               .arg(firstPort).arg(firstPort + tries - 1), true);

		dropUpnp();
		emit ready(0, QString(), 0);
		return;
	}

	if (!mapped)
		dropUpnp();

	if (manual)
	{
		QList<QByteArray> hosts;
		std::vector<const char *> hostPtrs;
		std::vector<int> ports;
		bool hasUpnpEntry = false;

		foreach (const DirectC2CSettings::Addr &a, addrs)
		{
			hosts.append(a.upnp ? QByteArray() : a.host.toUtf8());
			ports.push_back(a.upnp ? port : a.port);
			hasUpnpEntry = hasUpnpEntry || a.upnp;
		}

		// A mapping with nowhere to announce it goes at the end.
		if (mapped && !hasUpnpEntry)
		{
			hosts.append(QByteArray());
			ports.push_back(port);
			addrs.append(DirectC2CSettings::Addr {QString(), port, true});
		}

		for (int j = 0; j < hosts.size(); j++)
			hostPtrs.push_back(addrs[j].upnp ? nullptr : hosts[j].constData());

		// An empty list would make the listener fall back to automatic
		// configuration, announcing addresses the user did not choose.
		if (hostPtrs.empty())
		{
			emit statusMsg(tr("No addresses are configured for incoming direct connections"), true);
			PICA_close_listener(l);
			dropUpnp();
			emit ready(0, QString(), 0);
			return;
		}

		ret = PICA_listener_set_addrlist(l, hostPtrs.data(), ports.data(), (int)hostPtrs.size());

		if (ret != PICA_OK)
		{
			emit statusMsg(tr("Unable to resolve the addresses configured for incoming direct connections"), true);
			PICA_close_listener(l);
			dropUpnp();
			emit ready(0, QString(), 0);
			return;
		}
	}

	if (mapped)
	{
		upnpPort = port;
		upnpLost = false;
		scheduleRefresh();

		emit statusMsg(tr("Port %1 is forwarded by the UPnP router, external address %2").arg(port).arg(upnpAddr), false);
	}

	emit statusMsg(tr("Listening for incoming direct connections on port %1").arg(port), false);

	emit ready((quintptr)l, mapped ? upnpAddr : QString(), mapped ? port : 0);
}

void DirectListener::scheduleRefresh()
{
	unsigned int lease = PICA_upnp_lease(upnp);
	int secs;

	if (!refreshTimer)
	{
		refreshTimer = new QTimer(this);
		refreshTimer->setSingleShot(true);
		connect(refreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));
	}

	// Renewed at half the lease the router granted. A permanent mapping is
	// checked just as often: the router forgets it when it reboots.
	if (upnpLost)
		secs = kUpnpRetryInterval;
	else if (lease == 0)
		secs = PICA_UPNP_LEASE_DEFAULT / 2;
	else
		secs = (int)(lease / 2);

	if (secs < 30)
		secs = 30;

	refreshTimer->start(secs * 1000);
}

void DirectListener::refresh()
{
	char addr[64];
	int ret;

	if (!upnp)
		return;

	if (PICA_upnp_mapped(upnp))
		ret = PICA_upnp_refresh(upnp);
	else
		ret = PICA_upnp_map(upnp, upnpPort, PICA_UPNP_LEASE_DEFAULT, "Pica Pica");

	// The router's external address can change on a renewal of its own
	// lease with the provider, and is read again every time.
	if (ret == PICA_UPNP_OK)
		ret = PICA_upnp_external_addr(upnp, addr, sizeof(addr));

	if (ret == PICA_UPNP_OK)
	{
		QString newAddr = QString::fromLatin1(addr);

		if (upnpLost || newAddr != upnpAddr)
		{
			upnpAddr = newAddr;
			emit upnpChanged(upnpAddr, upnpPort);
		}

		upnpLost = false;
	}
	else if (!upnpLost)
	{
		upnpLost = true;
		emit upnpChanged(QString(), 0);
		emit statusMsg(tr("UPnP: the forwarding of port %1 was lost - %2").arg(upnpPort).arg(PICA_upnp_strerror(ret)), false);
	}

	scheduleRefresh();
}

void DirectListener::release()
{
	if (refreshTimer)
		refreshTimer->stop();

	dropUpnp();
}
