#include "sound.h"

#ifndef WIN32

#include <QProcess>

#else

//link with winmm.lib
#include <Mmsystem.h>
#include <windows.h>

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

#ifndef WIN32
    QProcess::execute("aplay -q " + sndfile);
#else
    sndPlaySound(sndfile.toAscii().constData(), SND_ASYNC | SND_NODEFAULT);
#endif
}
