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
#include "vaapivideowidget.h"

#ifdef HAVE_VAAPI

#include <QDebug>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <drm/drm_fourcc.h>

extern "C" {
#include <libavutil/pixfmt.h>
}

#include <va/va_drmcommon.h>


namespace
{
	// Extension entry points, resolved once at first use: they are not
	// linkable symbols, only reachable through eglGetProcAddress.
	PFNEGLCREATEIMAGEKHRPROC g_eglCreateImageKHR = nullptr;
	PFNEGLDESTROYIMAGEKHRPROC g_eglDestroyImageKHR = nullptr;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES = nullptr;
	bool g_resolved = false;

	bool resolveEglExtensions()
	{
		if (g_resolved)
			return g_eglCreateImageKHR && g_eglDestroyImageKHR && g_glEGLImageTargetTexture2DOES;

		g_resolved = true;
		g_eglCreateImageKHR =
			(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
		g_eglDestroyImageKHR =
			(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
		g_glEGLImageTargetTexture2DOES =
			(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

		return g_eglCreateImageKHR && g_eglDestroyImageKHR && g_glEGLImageTargetTexture2DOES;
	}

	const char *kVertexShader =
		"attribute highp vec2 vertex;\n"
		"attribute highp vec2 texcoord;\n"
		"varying highp vec2 uv;\n"
		"void main() {\n"
		"    uv = texcoord;\n"
		"    gl_Position = vec4(vertex, 0.0, 1.0);\n"
		"}\n";

	// NV12 arrives as a full resolution luma plane and a half resolution
	// plane of interleaved Cb/Cr. The BT.601 limited range coefficients below
	// match what the decoders in this pipeline produce for camera video.
	const char *kFragmentShader =
		"varying highp vec2 uv;\n"
		"uniform sampler2D planeY;\n"
		"uniform sampler2D planeUV;\n"
		"void main() {\n"
		"    highp float y = texture2D(planeY, uv).r;\n"
		"    highp vec2 c = texture2D(planeUV, uv).rg;\n"
		"    y = 1.1643 * (y - 0.0625);\n"
		"    highp float u = c.r - 0.5;\n"
		"    highp float v = c.g - 0.5;\n"
		"    highp float r = y + 1.5958 * v;\n"
		"    highp float g = y - 0.39173 * u - 0.81290 * v;\n"
		"    highp float b = y + 2.017 * u;\n"
		"    gl_FragColor = vec4(r, g, b, 1.0);\n"
		"}\n";
}

VaapiVideoWidget::VaapiVideoWidget(QWidget *parent)
	: QOpenGLWidget(parent), m_program(nullptr), m_numFds(0),
	  m_frameWidth(0), m_frameHeight(0), m_working(true), m_reportedFailure(false),
	  m_describedSurface(false)
{
	m_textures[0] = m_textures[1] = 0;
	m_images[0] = m_images[1] = EGL_NO_IMAGE_KHR;
	for (int i = 0; i < 4; i++)
		m_dmabufFds[i] = -1;
}

VaapiVideoWidget::~VaapiVideoWidget()
{
	// The GL objects belong to this widget's context, so it has to be current
	// while they are destroyed.
	makeCurrent();
	releaseImports();
	if (m_textures[0])
		glDeleteTextures(2, m_textures);
	delete m_program;
	doneCurrent();
}

void VaapiVideoWidget::reportFailure(const QString &message)
{
	m_working = false;
	markRenderingBroken();

	if (m_reportedFailure)
		return;

	m_reportedFailure = true;
	qWarning() << message;
	emit renderingFailed(message);
}

void VaapiVideoWidget::setFrame(AVFramePtr frame)
{
	if (!m_working)
		return;

	m_frame = frame;
	update();
}

void VaapiVideoWidget::clearFrame()
{
	m_frame.clear();
	update();
}

void VaapiVideoWidget::initializeGL()
{
	initializeOpenGLFunctions();

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);

	// Importing a surface as an EGL image needs an EGL context to import it
	// into. eglGetCurrentDisplay() is the telling call: under GLX it answers
	// EGL_NO_DISPLAY, and there is no display to create an image on.
	if (eglGetCurrentContext() == EGL_NO_CONTEXT || eglGetCurrentDisplay() == EGL_NO_DISPLAY)
	{
#ifdef HAVE_VAAPI_X11
		// An X11 session should have been given the other renderer, so the
		// display cannot have been opened on an X connection either.
		reportFailure(tr("The OpenGL context is not an EGL one and the VAAPI display was not "
		                 "opened on an X connection, so decoded frames cannot be drawn"));
#else
		reportFailure(tr("The OpenGL context is not an EGL one, so decoded frames cannot be "
		                 "drawn. An X11 session gives Qt a GLX context unless told otherwise; "
		                 "rebuild with --enable-vaapi-x11, or start the client with "
		                 "QT_XCB_GL_INTEGRATION=xcb_egl"));
#endif
		return;
	}

	if (!resolveEglExtensions())
	{
		reportFailure(tr("EGL does not offer the extensions needed to draw GPU frames"));
		return;
	}

	m_program = new QOpenGLShaderProgram(this);
	if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
	    !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
	    !m_program->link())
	{
		reportFailure(tr("Could not build the video shader: %1").arg(m_program->log()));
		return;
	}

	glGenTextures(2, m_textures);
	for (int i = 0; i < 2; i++)
	{
		glBindTexture(GL_TEXTURE_2D, m_textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}

void VaapiVideoWidget::releaseImports()
{
	for (int i = 0; i < 2; i++)
	{
		if (m_images[i] != EGL_NO_IMAGE_KHR)
		{
			if (g_eglDestroyImageKHR)
				g_eglDestroyImageKHR(eglGetCurrentDisplay(), (EGLImageKHR)m_images[i]);
			m_images[i] = EGL_NO_IMAGE_KHR;
		}
	}

	for (int i = 0; i < m_numFds; i++)
	{
		if (m_dmabufFds[i] >= 0)
		{
			::close(m_dmabufFds[i]);
			m_dmabufFds[i] = -1;
		}
	}
	m_numFds = 0;
}

bool VaapiVideoWidget::importFrame()
{
	VADisplay va_display = VaapiContext::display();
	if (!va_display || !m_frame)
		return false;

	// The surface id lives in data[3] for AV_PIX_FMT_VAAPI frames.
	VASurfaceID surface = (VASurfaceID)(uintptr_t)m_frame->data[3];

	// Everything still queued on the surface has to have finished before its
	// memory can be handed to another API.
	if (vaSyncSurface(va_display, surface) != VA_STATUS_SUCCESS)
		return false;

	VADRMPRIMESurfaceDescriptor desc;
	memset(&desc, 0, sizeof desc);

	VAStatus st = vaExportSurfaceHandle(va_display, surface,
	                                    VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
	                                    VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS,
	                                    &desc);
	if (st != VA_STATUS_SUCCESS)
		return false;

	// Held so they can be closed again after drawing; the export hands over
	// ownership of every one of them.
	m_numFds = desc.num_objects > 4 ? 4 : (int)desc.num_objects;
	for (int i = 0; i < m_numFds; i++)
		m_dmabufFds[i] = desc.objects[i].fd;

	m_frameWidth = desc.width;
	m_frameHeight = desc.height;

	// Logged once: how the driver actually laid the surface out is the first
	// thing worth knowing when the picture comes out wrong.
	if (!m_describedSurface)
	{
		m_describedSurface = true;

		QString layers;
		for (uint32_t i = 0; i < desc.num_layers; i++)
		{
			uint32_t object = desc.layers[i].object_index[0];
			layers += QString(" layer%1[fourcc=0x%2 offset=%3 pitch=%4 modifier=0x%5]")
			          .arg(i)
			          .arg(desc.layers[i].drm_format, 0, 16)
			          .arg(desc.layers[i].offset[0])
			          .arg(desc.layers[i].pitch[0])
			          .arg(desc.objects[object].drm_format_modifier, 0, 16);
		}

		qDebug() << QString("VAAPI surface for display: %1x%2 fourcc=0x%3 objects=%4 layers=%5%6")
		            .arg(desc.width).arg(desc.height)
		            .arg(desc.fourcc, 0, 16)
		            .arg(desc.num_objects).arg(desc.num_layers)
		            .arg(layers);
	}

	// Two layers for NV12: the luma plane, then the interleaved chroma plane
	// at half size in both directions.
	if (desc.num_layers < 2)
	{
		releaseImports();
		return false;
	}

	EGLDisplay egl_display = eglGetCurrentDisplay();

	for (int i = 0; i < 2; i++)
	{
		uint32_t object = desc.layers[i].object_index[0];
		int planeWidth = (i == 0) ? desc.width : desc.width / 2;
		int planeHeight = (i == 0) ? desc.height : desc.height / 2;
		uint64_t modifier = desc.objects[object].drm_format_modifier;

		EGLint attribs[16];
		int n = 0;

		attribs[n++] = EGL_WIDTH;                       attribs[n++] = planeWidth;
		attribs[n++] = EGL_HEIGHT;                      attribs[n++] = planeHeight;
		attribs[n++] = EGL_LINUX_DRM_FOURCC_EXT;        attribs[n++] = (EGLint)desc.layers[i].drm_format;
		attribs[n++] = EGL_DMA_BUF_PLANE0_FD_EXT;       attribs[n++] = desc.objects[object].fd;
		attribs[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;   attribs[n++] = (EGLint)desc.layers[i].offset[0];
		attribs[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;    attribs[n++] = (EGLint)desc.layers[i].pitch[0];

		// Without this the import is assumed to be a plain linear buffer. GPUs
		// hand out tiled surfaces instead, and reading one as linear is what
		// produces a picture in bands with the colour gone - so the modifier
		// the driver reported has to be passed along whenever it has one.
		if (modifier != DRM_FORMAT_MOD_INVALID)
		{
			attribs[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
			attribs[n++] = (EGLint)(modifier & 0xFFFFFFFF);
			attribs[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
			attribs[n++] = (EGLint)(modifier >> 32);
		}

		attribs[n++] = EGL_NONE;

		m_images[i] = g_eglCreateImageKHR(egl_display, EGL_NO_CONTEXT,
		                                  EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
		if (m_images[i] == EGL_NO_IMAGE_KHR)
		{
			qWarning() << QString("eglCreateImageKHR failed for plane %1: 0x%2")
			              .arg(i).arg(eglGetError(), 0, 16);
			releaseImports();
			return false;
		}

		glBindTexture(GL_TEXTURE_2D, m_textures[i]);
		g_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)m_images[i]);
	}

	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

void VaapiVideoWidget::paintGL()
{
	glClear(GL_COLOR_BUFFER_BIT);

	if (!m_working || !m_program || !m_frame)
		return;

	if (!importFrame())
	{
		reportFailure(tr("Could not hand the decoded GPU frame to OpenGL for drawing"));
		return;
	}

	// Letterbox rather than stretch, matching what the software path does
	// with Qt::KeepAspectRatio.
	float widgetAspect = float(width()) / float(height() ? height() : 1);
	float frameAspect = float(m_frameWidth) / float(m_frameHeight ? m_frameHeight : 1);
	float sx = 1.0f, sy = 1.0f;

	if (frameAspect > widgetAspect)
		sy = widgetAspect / frameAspect;
	else
		sx = frameAspect / widgetAspect;

	const GLfloat vertices[] =
	{
		-sx, -sy,
		 sx, -sy,
		-sx,  sy,
		 sx,  sy
	};
	// Flipped vertically: video rows run top to bottom, OpenGL's origin is at
	// the bottom left.
	const GLfloat texcoords[] =
	{
		0.0f, 1.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f
	};

	m_program->bind();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_textures[0]);
	m_program->setUniformValue("planeY", 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_textures[1]);
	m_program->setUniformValue("planeUV", 1);

	int vertexLocation = m_program->attributeLocation("vertex");
	int texcoordLocation = m_program->attributeLocation("texcoord");

	m_program->enableAttributeArray(vertexLocation);
	m_program->enableAttributeArray(texcoordLocation);
	m_program->setAttributeArray(vertexLocation, vertices, 2);
	m_program->setAttributeArray(texcoordLocation, texcoords, 2);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	m_program->disableAttributeArray(vertexLocation);
	m_program->disableAttributeArray(texcoordLocation);
	m_program->release();

	glActiveTexture(GL_TEXTURE0);

	// Done with this frame's handles - holding on to them would leak a set of
	// file descriptors for every frame drawn.
	releaseImports();
}

void VaapiVideoWidget::resizeGL(int w, int h)
{
	glViewport(0, 0, w, h);
}

#endif // HAVE_VAAPI
