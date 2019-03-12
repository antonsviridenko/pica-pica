/*
	(c) Copyright  2012 - 2019 Anton Sviridenko
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
#include "PICA_signverify.h"

#include <openssl/evp.h>

int PICA_signverify(EVP_PKEY *pubkey, void **datapointers, size_t *datalengths, unsigned char *sig, size_t siglen)
{
	EVP_MD_CTX *mdctx = NULL;
	int ret = 0;

	mdctx = EVP_MD_CTX_create();

	if (!mdctx)
		return 0;

	ret = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha224(), NULL, pubkey);

	if (ret != 1)
		goto signverify_exit;

	while (*datapointers)
	{
		ret = EVP_DigestVerifyUpdate(mdctx, *datapointers, *datalengths);

		if (ret != 1)
			goto signverify_exit;

		datapointers++;
		datalengths++;
	}

	ret = EVP_DigestVerifyFinal(mdctx, sig, siglen);

	if (ret != 1)
		ret = 0;

signverify_exit:
	EVP_MD_CTX_destroy(mdctx);
	return ret;
}

int PICA_do_signature(EVP_PKEY *privkey, void **datapointers, size_t *datalengths, unsigned char **sig, size_t *siglen)
{
	int ret = 0;
	EVP_MD_CTX *mdctx = NULL;

	mdctx = EVP_MD_CTX_create();

	if (!mdctx)
		return 0;

	ret = EVP_DigestSignInit(mdctx, NULL, EVP_sha224(), NULL, privkey);

	if (ret != 1)
		goto sig_exit;

	while(*datapointers)
	{
		ret = EVP_DigestSignUpdate(mdctx, *datapointers, *datalengths);

		if (ret != 1)
		{
			goto sig_exit;
			break;
		}

		datapointers++;
		datalengths++;
	}

	ret = EVP_DigestSignFinal(mdctx, NULL, (size_t*) siglen);

	if (ret != 1)
		goto sig_exit;

	*sig = (unsigned char*) malloc(*siglen);

	if (!*sig)
	{
		ret = 0;
		goto sig_exit;
	}

	ret = EVP_DigestSignFinal(mdctx, *sig, (size_t*) siglen);

	if (ret == 1)
		ret = 1;
	else
		free(*sig);

sig_exit:
	EVP_MD_CTX_destroy(mdctx);
	return ret;
}
