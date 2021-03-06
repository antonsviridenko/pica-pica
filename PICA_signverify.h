/*
	(c) Copyright  2012 - 2019 Anton Sviridenko
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

#ifndef PICA_SIGNVERIFY_H
#define PICA_SIGNVERIFY_H

#include <openssl/evp.h>

int PICA_signverify(EVP_PKEY *pubkey, void **datapointers, size_t *datalengths, unsigned char *sig, size_t siglen);
int PICA_do_signature(EVP_PKEY *privkey, void **datapointers, size_t *datalengths, unsigned char **sig, size_t *siglen);

#endif
