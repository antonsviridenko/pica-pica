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
#pragma once

#include <QObject>
#include <QVector>
#include <QAtomicInt>
#include <QtGlobal>
#include <QString>

// Simple tone descriptor: frequency in Hz, duration in milliseconds.
struct Tone {
    double frequency; // Hz
    int duration_ms;  // milliseconds
    Tone(double f = 0.0, int d = 0): frequency(f), duration_ms(d) {}
};

class TonePlayer : public QObject
{
    Q_OBJECT
public:
    explicit TonePlayer(QObject *parent = nullptr);
    ~TonePlayer() override;

    // Replace the sequence to play. Call before play().
    void setSequence(const QVector<Tone> &sequence);

    // Stop playback early. This will cause play() to return.
    Q_INVOKABLE void stop();

    // Set the platform playback device name (e.g. "default" or "hw:0" on
    // ALSA, a CoreAudio/DirectSound device name on macOS/Windows). Default:
    // "default". Typically set from the "audio.playback_device" or
    // "audio.ring_device" setting (see SettingsDialog). Q_INVOKABLE so it
    // can be queued across threads via QMetaObject::invokeMethod(), the
    // same way play() is invoked when TonePlayer lives on its own QThread.
    Q_INVOKABLE void setDeviceName(const QString &deviceName);

    // Set playback parameters (optional before play)
    void setSampleRate(int sampleRate);
    void setVolume(double vol); // 0.0 .. 1.0

signals:
    void finishedPlaying();
    void errorOccured(const QString &err);

public slots:
    void play();

    // Standard telephony tone sequences. Each replaces the current sequence
    // and plays it to completion (blocking, like play()).
    void playCallInProgress();  // ringing tone: 440 Hz, 1.5 s on / 3 s off, 3 bursts
    void playUnreachable();     // special information tone: 950/1400/1800 Hz, 330 ms each
    void playBusy();            // busy tone: 440 Hz, 400 ms on / 400 ms off, 3 bursts
    void playClassicRingtone(); // electromechanical bell: warbling trill, 2 s on / 4 s off, 3 bursts

private:
    // Set the sequence, clear any pending stop request and play it.
    void playSequence(const QVector<Tone> &sequence);

    QVector<Tone> m_sequence;
    QAtomicInt m_abort;
    QString m_deviceName;
    int m_sampleRate;
    int m_channels; // always 1 (mono) by design
    double m_volume;
};
