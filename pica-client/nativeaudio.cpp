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
#include "nativeaudio.h"

#include <QtGlobal>

// Per platform factories, defined next to the backends themselves.
#ifdef Q_OS_WIN
NativeAudioBackend *pica_create_wasapi_backend();
QList<MediaDeviceInfo> pica_enumerate_wasapi(enum MediaDeviceStreamDirection dir);
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
NativeAudioBackend *pica_create_coreaudio_backend();
QList<MediaDeviceInfo> pica_enumerate_coreaudio(enum MediaDeviceStreamDirection dir);
#endif

NativeAudioBackend::~NativeAudioBackend()
{
}

bool NativeAudioBackend::isAvailable(const QString &api)
{
#ifdef Q_OS_WIN
	if (api == QLatin1String("wasapi"))
		return true;
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	if (api == QLatin1String("coreaudio"))
		return true;
#endif
	Q_UNUSED(api)
	return false;
}

NativeAudioBackend *NativeAudioBackend::create(const QString &api)
{
#ifdef Q_OS_WIN
	if (api == QLatin1String("wasapi"))
		return pica_create_wasapi_backend();
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	if (api == QLatin1String("coreaudio"))
		return pica_create_coreaudio_backend();
#endif
	Q_UNUSED(api)
	return nullptr;
}

QList<MediaDeviceInfo> NativeAudioBackend::enumerate(const QString &api, enum MediaDeviceStreamDirection dir)
{
#ifdef Q_OS_WIN
	if (api == QLatin1String("wasapi"))
		return pica_enumerate_wasapi(dir);
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	if (api == QLatin1String("coreaudio"))
		return pica_enumerate_coreaudio(dir);
#endif
	Q_UNUSED(api)
	Q_UNUSED(dir)
	return QList<MediaDeviceInfo>();
}
