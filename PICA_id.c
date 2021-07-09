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

#include "PICA_id.h"
#include <openssl/bio.h>
#include <string.h>

int PICA_id_from_X509(X509 *x, unsigned char *id)
{
	unsigned char *der = NULL;
	int len;

	len = i2d_X509(x, &der);

	if (len <= 0)
	{
		return 0;
	}

	SHA224(der, len, id);

	OPENSSL_free(der);

	return 1;
}

unsigned char *PICA_id_from_base64(const unsigned char *buf, unsigned char *id)
{
	BIO *biomem, *b64;
	static char localidbuf[PICA_ID_SIZE];
	int inlen, bread = 0;
	unsigned char *idbuf = id;

	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	biomem = BIO_new_mem_buf(buf, -1);
	biomem = BIO_push(b64, biomem);

	if (idbuf == NULL)
		idbuf = localidbuf;

	bread = BIO_read(biomem, idbuf, PICA_ID_SIZE);

	if (bread != PICA_ID_SIZE)
		idbuf = NULL;

	BIO_free_all(biomem);

	return idbuf;
}

char *PICA_id_to_base64(const unsigned char *id, char *buf)
{
	BIO *biomem, *b64;
	static char localbuf[PICA_ID_SIZE * 2];

	char *sourcebuf, *outputbuf = buf;
	char *newlinepos;
	long b64len;

	b64 = BIO_new(BIO_f_base64());
	biomem = BIO_new(BIO_s_mem());
	biomem = BIO_push(b64, biomem);
	BIO_write(biomem, id, PICA_ID_SIZE);
	BIO_flush(biomem);

	b64len = BIO_get_mem_data(biomem, &sourcebuf);

	if (outputbuf == NULL)
		outputbuf = localbuf;

	memcpy(outputbuf, sourcebuf, b64len);

	outputbuf[b64len] = '\0';
	if ((newlinepos = strchr(outputbuf, '\n')))
		*newlinepos = '\0';

	BIO_free_all(biomem);

	return outputbuf;
}
