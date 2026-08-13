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
#ifndef VAAPIVIDEOWIDGET_H
#define VAAPIVIDEOWIDGET_H

#ifdef HAVE_VAAPI

#include "vaapi.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QString>

// Draws frames that are still sitting in GPU memory as VA surfaces, without
// bringing them back through system memory on the way.
//
// The surface is exported as DMA-BUF file descriptors, imported into EGL as
// images, and bound to OpenGL textures - one for the luma plane and one for
// the interleaved chroma plane of an NV12 frame - which a fragment shader
// then converts to RGB while drawing. Nothing copies the picture; the GPU
// keeps it where the decoder left it.
//
// The alternative, vaPutSurface() onto an X11 Drawable, was not an option
// here: the client runs on Wayland, where there is no such drawable, and
// Qt5X11Extras is not installed either. EGL works on both Wayland and X11.
class VaapiVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
	Q_OBJECT
public:
	explicit VaapiVideoWidget(QWidget *parent = nullptr);
	~VaapiVideoWidget() override;

	// Whether the last frame could actually be drawn. False means the EGL
	// import failed and nothing is being shown, so a caller can fall back to
	// the software path rather than leave a blank widget.
	bool isWorking() const { return m_working; }

public slots:
	// Takes the frame to draw next, replacing any previous one that has not
	// been drawn yet - for live video the newest frame is the one that
	// matters, which is the same policy the decode queue follows.
	void setFrame(AVFramePtr frame);

	// Drops the current frame and clears the widget.
	void clearFrame();

signals:
	// Raised once if the frame cannot be imported for drawing, carrying the
	// reason. The widget stops trying after that.
	void renderingFailed(QString message);

protected:
	void initializeGL() override;
	void paintGL() override;
	void resizeGL(int w, int h) override;

private:
	// Builds the two textures for the current frame out of the surface's
	// DMA-BUF handles. Returns false and leaves nothing bound on failure.
	bool importFrame();

	// Releases the EGL images and the file descriptors the export handed
	// over. Not closing those descriptors leaks them once per frame, which
	// exhausts the process's descriptors within minutes.
	void releaseImports();

	AVFramePtr m_frame;
	QOpenGLShaderProgram *m_program;
	unsigned int m_textures[2];
	void *m_images[2];
	int m_dmabufFds[4];
	int m_numFds;
	int m_frameWidth;
	int m_frameHeight;
	bool m_working;
	bool m_reportedFailure;
	bool m_describedSurface;
};

#endif // HAVE_VAAPI

#endif // VAAPIVIDEOWIDGET_H
