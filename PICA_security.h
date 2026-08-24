/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
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

#ifndef PICA_SECURITY_H
#define PICA_SECURITY_H

/* Important security parameters grouped together */

#define PICA_RSA_MINKEYSIZE 4096
#define PICA_TLS_CIPHERLIST "DHE-RSA-AES256-GCM-SHA384:DHE-RSA-CAMELLIA256-SHA"
#define PICA_TLS_ANONDHCIPHERLIST "ADH-AES256-GCM-SHA384:ADH-CAMELLIA256-SHA"
#define PICA_OPENSSL_SECURUTY_LEVEL 3
#define PICA_CERTDIGESTALGO "-sha256"
#define PICA_PRIVKEYENCALGO "-aes256"
#define PICA_TIMETOLERANCE 600

/* The TLS version range, pinned to exactly 1.2 at both ends.
 *
 * The upper end is deliberate and load bearing, not tidiness. Both cipher
 * lists above name TLS 1.2 suites, and the anonymous one has no TLS 1.3
 * counterpart at all - 1.3 dropped anonymous key exchange entirely. On top of
 * that, SSL_CTX_set_cipher_list() has no say over which TLS 1.3 suites a
 * connection ends up using; that is a separate call
 * (SSL_CTX_set_ciphersuites). So letting 1.3 be negotiated would quietly
 * ignore PICA_TLS_CIPHERLIST and leave PICA_TLS_ANONDHCIPHERLIST with nothing
 * to offer.
 *
 * Moving the ceiling therefore means naming TLS 1.3 suites explicitly and
 * finding another way to do the anonymous handshake - it is a protocol
 * decision, not a configuration change.
 *
 * Applied with SSL_CTX_set_min_proto_version()/SSL_CTX_set_max_proto_version()
 * on OpenSSL 1.1.0 and later. Those replaced TLSv1_2_method(), which was
 * deprecated in 1.1.0 and removed in 4.0.
 *
 * These expand to <openssl/ssl.h> constants, so they are only usable in a
 * translation unit that includes it.
 */
#define PICA_TLS_MINVERSION TLS1_2_VERSION
#define PICA_TLS_MAXVERSION TLS1_2_VERSION

#endif
