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
#ifndef DIRECTC2CSETTINGS_H
#define DIRECTC2CSETTINGS_H

#include "settings.h"
#include <QString>
#include <QStringList>
#include <QList>

// The settings of the "Direct connections" tab, read and written in one place
// because the settings dialog and SkyNet both need them, and because of the
// single address earlier versions stored instead of a list.
struct DirectC2CSettings
{
	// direct_c2c.addr_config
	enum AddrConfig
	{
		// interface addresses, then the UPnP mapping; the port is looked for
		// starting at DefaultPort
		AddrConfigAuto = 0,
		// the list below, listening on localPort
		AddrConfigManual = 1
	};

	enum
	{
		DefaultPort = 2299
	};

	// One announced address. The UPnP entry stands for the external address
	// of the UPnP port mapping, which is only known at run time.
	struct Addr
	{
		QString host;
		int port;
		bool upnp;
	};

	// PICA_directc2c_config
	int state;
	bool upnp;
	int addrConfig;
	int localPort;
	QList<Addr> addrs;

	void load(Settings &st)
	{
		state = st.loadValue("direct_c2c.state", 1).toInt();
		upnp = st.loadValue("direct_c2c.upnp_enabled", 1).toBool();
		localPort = st.loadValue("direct_c2c.local_port", (int)DefaultPort).toInt();
		addrConfig = st.loadValue("direct_c2c.addr_config", -1).toInt();
		addrs = parseAddrs(st.loadValue("direct_c2c.addr_list", QString()).toString());

		if (addrConfig == AddrConfigAuto || addrConfig == AddrConfigManual)
			return;

		// Never stored: either a fresh installation, or one from before the
		// address list, whose single public address - "autoconfigure"
		// unless the user typed one in - becomes a manual list of one.
		QString oldAddr = st.loadValue("direct_c2c.public_addr", "autoconfigure").toString();

		addrConfig = AddrConfigAuto;

		if (!oldAddr.isEmpty() && !oldAddr.contains("autoconfigure"))
		{
			Addr a;

			a.host = oldAddr;
			a.port = st.loadValue("direct_c2c.public_port", localPort).toInt();
			a.upnp = false;

			addrConfig = AddrConfigManual;
			addrs.clear();
			addrs.append(a);
		}
	}

	// Whether a listener set up for o would be set up the same way for these
	// settings: the port and the list only matter in manual configuration.
	bool sameListener(const DirectC2CSettings &o) const
	{
		if (upnp != o.upnp || addrConfig != o.addrConfig)
			return false;

		if (addrConfig != AddrConfigManual)
			return true;

		return localPort == o.localPort && formatAddrs(addrs) == formatAddrs(o.addrs);
	}

	void store(Settings &st) const
	{
		st.storeValue("direct_c2c.state", QString::number(state));
		st.storeValue("direct_c2c.upnp_enabled", upnp ? "1" : "0");
		st.storeValue("direct_c2c.addr_config", QString::number(addrConfig));
		st.storeValue("direct_c2c.local_port", QString::number(localPort));
		st.storeValue("direct_c2c.addr_list", formatAddrs(addrs));
	}

	// "host:port;host:port;upnp"
	static QList<Addr> parseAddrs(const QString &s)
	{
		QList<Addr> l;

		foreach (const QString &item, s.split(';', QString::SkipEmptyParts))
		{
			Addr a;
			QString entry = item.trimmed();
			int colon = entry.lastIndexOf(':');

			a.upnp = entry == "upnp";
			a.port = 0;

			if (!a.upnp)
			{
				if (colon <= 0)
					continue;

				a.host = entry.left(colon);
				a.port = entry.mid(colon + 1).toInt();

				if (a.port <= 0 || a.port > 65535)
					continue;
			}

			l.append(a);
		}

		return l;
	}

	static QString formatAddrs(const QList<Addr> &l)
	{
		QStringList items;

		foreach (const Addr &a, l)
			items.append(a.upnp ? QString("upnp") : QString("%1:%2").arg(a.host).arg(a.port));

		return items.join(';');
	}
};

#endif // DIRECTC2CSETTINGS_H
