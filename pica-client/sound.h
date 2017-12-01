#ifndef SOUND_H
#define SOUND_H
#include <QString>

class Sound
{
public:
	Sound();

	static void play(const QString sndfile);
	static void mute(bool yes);

private:
	static bool is_muted;
};

#endif // SOUND_H
