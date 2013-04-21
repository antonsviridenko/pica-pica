 # Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=4

inherit eutils

DESCRIPTION="Pica Pica Messenger"
HOMEPAGE="http://picapica.im/"
SRC_URI="http://picapica.im/pica-pica-${PV}.tar.gz"

LICENSE="BSD-2"
SLOT="0"

KEYWORDS="~amd64 ~x86"
IUSE=""

DEPEND=">=dev-libs/openssl-0.9.8
	>dev-qt/qtcore-4.0.0
	>dev-qt/qtgui-4.0.0
	>dev-qt/qtsql-4.0.0[sqlite]
	virtual/pkgconfig
	x11-misc/xdg-utils"

RDEPEND="${DEPEND}"

S="${WORKDIR}/pica-pica-${PV}"

src_configure() {
    econf    --disable-node --disable-menuitem
}

src_install() {
	emake DESTDIR="${D}" install || die "Install failed"
	#dodoc README  || die
}

pkg_postinst() {
	xdg-icon-resource install --size 32 "${S}/pica-client/picapica-icon-sit.png" pica-client
	xdg-icon-resource install --size 22 "${S}/pica-client/picapica-icon-sit.png" pica-client
	xdg-icon-resource install --size 64 "${S}/pica-client/picapica-icon-sit.png" pica-client

	xdg-desktop-menu install "${S}/pica-client/pica-client.desktop"
}

pkg_postrm() {
	xdg-icon-resource uninstall --size 32 pica-client
	xdg-icon-resource uninstall --size 22 pica-client
	xdg-icon-resource uninstall --size 64 pica-client

	xdg-desktop-menu uninstall pica-client.desktop
}

