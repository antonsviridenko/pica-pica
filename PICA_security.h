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

#endif
