# Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=8

inherit autotools desktop git-r3 xdg

if [[ ${PV} == "9999" ]] ; then
    EGIT_REPO_URI="https://github.com/antonsviridenko/pica-pica.git"
    SRC_URI=""
else
	SRC_URI="http://picapica.im/pica-pica-${PV}.tar.gz"
	KEYWORDS="~amd64 ~x86"
fi

DESCRIPTION="Pica Pica Messenger and Node"
HOMEPAGE="http://picapica.im/"

LICENSE="GPL-3"
SLOT="0"

IUSE="+client node upnp +vaapi pulseaudio X"

DEPEND=">=dev-libs/openssl-1.0.2r
	upnp? ( >=net-libs/miniupnpc-2.0 )
	!net-im/pica-node
	!net-im/pica-client

	node? (
			>=dev-db/sqlite-3.7.0
			>=dev-libs/libevent-2.1.6
		)

	client? (
		dev-qt/qtbase:6[gui,network,sql,sqlite,widgets]
		virtual/pkgconfig
		x11-misc/xdg-utils
		media-sound/alsa-utils

		media-libs/alsa-lib
		|| (
			media-libs/webrtc-audio-processing:1
			media-libs/webrtc-audio-processing:2
		)

		>=media-video/ffmpeg-4.2:=[alsa,opus,v4l,x264,pulseaudio?,vaapi?]

		pulseaudio? ( media-libs/libpulse )

		vaapi? (
			dev-qt/qtbase:6[opengl]
			media-libs/libva:=[drm(+),X?]
			media-libs/libglvnd[X?]
			X? ( x11-libs/libX11 )
		)
	)"

RDEPEND="${DEPEND}
	node? (
			acct-user/pica-node
			acct-group/pica-node
		)"

S="${WORKDIR}/pica-pica-${PV}"

if [[ ${PV} == "9999" ]] ; then
src_prepare() {
    eapply_user
    eautoreconf -i
}
fi

src_configure() {
	# VAAPI and the PulseAudio device listing are autodetected by
	# configure, so they have to be asked for explicitly - otherwise the
	# USE flags would only say what may be linked, not what is. The X11
	# rendering path is part of VAAPI and configure refuses it on its own.
	local vaapiflags="--disable-vaapi --disable-vaapi-x11"
	if use vaapi; then
		vaapiflags="--enable-vaapi $(use_enable X vaapi-x11)"
	fi

	econf $(use_with upnp miniupnpc)  $(use_enable client) $(use_enable node) --with-qt=qt6 \
		${vaapiflags} $(use_enable pulseaudio pulse)
}

src_install() {
	emake DESTDIR="${D}" install || die "Install failed"
	use node && newinitd "${FILESDIR}/pica-node-initd" pica-node
	use node && newconfd "${FILESDIR}/pica-node-confd" pica-node
	domenu "${S}/pica-client/pica-client.desktop"
	newicon -s 16 "${S}/pica-client/icons/icon_16x16.png" pica-client.png
	newicon -s 32 "${S}/pica-client/icons/icon_32x32.png" pica-client.png
	newicon -s 48 "${S}/pica-client/icons/icon_48x48.png" pica-client.png
	newicon -s 256 "${S}/pica-client/picapica-icon-fly.png" pica-client.png
}

pkg_preinst() {
	xdg_pkg_preinst

	if use node; then
		fowners -R pica-node:pica-node "/var/lib/pica-node"

		dodir "/var/log/pica-node"
		fowners -R pica-node:pica-node "/var/log/pica-node"
	fi
}

pkg_postinst() {
	xdg_pkg_postinst

	use node && elog "Set the announced_addr value to your IP address in a config file before running pica-node"
}


