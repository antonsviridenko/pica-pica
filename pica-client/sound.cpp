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
#include "sound.h"

#ifndef WIN32

#include <QProcess>

#else

//link with winmm.lib
#include <windows.h>
#include <mmsystem.h>

#endif


bool Sound::is_muted = false;

Sound::Sound()
{
}

void Sound::mute(bool yes)
{
	is_muted = yes;
}

void Sound::play(const QString sndfile)
{
	if (is_muted)
		return;

#if defined(__gnu_linux__)
	QProcess::execute("aplay -q " + sndfile);
#endif
#if defined(WIN32)
	sndPlaySound(sndfile.toLatin1().constData(), SND_ASYNC | SND_NODEFAULT);
#endif
#if defined(__APPLE__)
	QProcess::execute("/usr/bin/afplay " + sndfile);
#endif
}
