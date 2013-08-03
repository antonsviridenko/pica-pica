 # Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=4

inherit eutils

DESCRIPTION="Node for Pica Pica distributed decentralized IM system"
HOMEPAGE="http://picapica.im/"
SRC_URI="http://picapica.im/pica-pica-${PV}.tar.gz"

LICENSE="BSD-2"
SLOT="0"

KEYWORDS="~amd64 ~x86"
IUSE=""

DEPEND=">=dev-libs/openssl-0.9.8
	>=dev-db/sqlite-3.7.0"

RDEPEND="${DEPEND}"

S="${WORKDIR}/pica-pica-${PV}"

src_configure() {
    # We have optional perl, python and ruby support
    econf    --disable-client --localstatedir=/var
}

src_install() {
	emake DESTDIR="${D}" install || die "Install failed"
	newinitd "${S}/gentoo/gentoo_initscript" ${PN}
	#dodoc README  || die
}

pkg_preinst() {
#    enewgroup pica-node
	enewuser pica-node
	fowners -R pica-node:pica-node "/var/lib/pica-node"
	
	dodir "/var/log/pica-node"
	fowners -R pica-node:pica-node "/var/log/pica-node"
}

pkg_postinst() {

	elog "Set announced_addr value to your IP address in config file before running pica-node"
}
