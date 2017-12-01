#include "sound.h"

#ifndef WIN32

#include <QProcess>

#else

//link with winmm.lib
#include <windows.h>
#include <Mmsystem.h>

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
	sndPlaySound(sndfile.toAscii().constData(), SND_ASYNC | SND_NODEFAULT);
#endif
#if defined(__APPLE__)
	QProcess::execute("/usr/bin/afplay " + sndfile);
#endif
}
