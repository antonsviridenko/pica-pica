/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PICA_ID_H
#define PICA_ID_H

#include <openssl/sha.h>
#include <openssl/x509.h>

#define PICA_ID_SIZE SHA224_DIGEST_LENGTH

#ifdef __cplusplus
extern "C" {
#endif
int PICA_id_from_X509(X509 *x, unsigned char *id);

unsigned char *PICA_id_from_base64(const unsigned char *buf, unsigned char *id);
char *PICA_id_to_base64(const unsigned char *id, char *buf);

#ifdef __cplusplus
}
#endif

#endif
