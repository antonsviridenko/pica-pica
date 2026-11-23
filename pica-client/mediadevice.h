#ifndef MEDIADEVICE_H
#define MEDIADEVICE_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>

enum MediaDeviceStreamDirection
{
	CAPTURE,
	PLAYBACK
};

struct MediaDeviceInfo
{
	QString device;
	QString humanReadable;
	int index;
};

// Which call a device is currently allowed to work for. A call is identified
// by the pair of its caller's and its callee's id.
//
// The loops of AudioDevice and VideoDevice are started through their thread's
// event queue and stopped by a direct call from another thread, so the two can
// arrive in either order. A plain stop flag that the loop clears on entry
// loses a stop that came first, and the loop then runs with nobody left to
// stop it. Instead the owner opens the session before it queues the start and
// closes it to stop, and a loop only runs while the session that is open is
// the one it was configured for. All of it is safe to call from any thread.
class MediaCallSession
{
public:
	MediaCallSession() : m_open(false) {}

	void open(const QByteArray &caller_id, const QByteArray &callee_id)
	{
		QMutexLocker locker(&m_mutex);
		m_caller_id = caller_id;
		m_callee_id = callee_id;
		m_open = true;
	}

	void close()
	{
		QMutexLocker locker(&m_mutex);
		m_open = false;
	}

	bool isOpenFor(const QByteArray &caller_id, const QByteArray &callee_id) const
	{
		QMutexLocker locker(&m_mutex);
		return m_open && m_caller_id == caller_id && m_callee_id == callee_id;
	}

private:
	mutable QMutex m_mutex;
	bool m_open;
	QByteArray m_caller_id;
	QByteArray m_callee_id;
};

class MediaDevice
{
public:
	MediaDevice();
	virtual ~MediaDevice();
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection) = 0;
	virtual void Capture() = 0;
	virtual void Play() = 0;
	virtual void Close() = 0;
};

#endif // MEDIADEVICE_H
