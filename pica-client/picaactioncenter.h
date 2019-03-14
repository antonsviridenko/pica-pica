/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef PICAACTIONCENTER_H
#define PICAACTIONCENTER_H

#include <QObject>
#include <QAction>

class PicaActionCenter : public QObject
{
	Q_OBJECT
public:
	explicit PicaActionCenter(QObject *parent = 0);

	QAction *AboutAct()
	{
		return aboutAct;
	};
	QAction *ExitAct()
	{
		return exitAct;
	};
	QAction *ShowMyIDAct()
	{
		return showmyidAct;
	};
	QAction *MuteSoundsAct()
	{
		return muteSoundsAct;
	};
	QAction *SettingsAct()
	{
		return settingsAct;
	};
	QAction *NodesAct()
	{
		return nodesAct;
	};

private:
	QAction *aboutAct;
	QAction *exitAct;
	QAction *showmyidAct;
	QAction *muteSoundsAct;
	QAction *settingsAct;
	QAction *nodesAct;

signals:

public slots:
private slots:
	void about();
	void exit();
	void showmyid();
	void muteSounds(bool yes);
	void showsettings();
	void shownodes();
};

#endif // PICAACTIONCENTER_H
