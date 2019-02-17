#include "../PICA_signverify.h"
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <stdio.h>

int main(int argc, char **argv)
{
	char databuf[512];
	void* dataptrs[2];
	int datalengths[2];
	unsigned char *sig;
	int siglen;
	int ret;
	EVP_PKEY *privkey, *pubkey;
	FILE *f;
	X509 *x;
	RSA *rsa;

	OpenSSL_add_all_algorithms();

	RAND_pseudo_bytes(databuf, sizeof databuf);

	dataptrs[0] = databuf;
	datalengths[0] = sizeof databuf;

	dataptrs[1] = NULL;
	datalengths[1] = 0;

	f = fopen(argv[1], "r");

	if (!f)
	{
		fprintf(stderr, "failed to open %s\n", argv[1]);
		return -1;
	}

	privkey = EVP_PKEY_new();

	x = PEM_read_X509(f, 0, 0, 0);

	if (!x)
	{
		fprintf(stderr, "failed to read X.509 cert from file");
		return -1;
	}

	//pubkey = x->cert_info->key->pkey;
	pubkey = X509_get_pubkey(x);

	if (!pubkey)
	{
		fprintf(stderr, "no pubkey found");
	}

	//fclose(f);

	//f = fopen(argv[1], "r");

	rsa = PEM_read_RSAPrivateKey(f, NULL, NULL, NULL);
	fclose(f);

	if (!rsa)
	{
		fprintf(stderr, "failed to read RSA private key from file");
		return -1;
	}

	ret = EVP_PKEY_set1_RSA(privkey, rsa);

	ret = PICA_do_signature(privkey, dataptrs, datalengths, &sig, &siglen);

	ret = PICA_signverify(pubkey, dataptrs, datalengths, sig, siglen);

	printf("signature length is %i octets\n", siglen);

	if (ret != 1)
	{
		fprintf(stderr, "signature verification failed\n");
		return -1;
	}

	databuf[0] += 1;//modify data

	ret = PICA_signverify(pubkey, dataptrs, datalengths, sig, siglen);

	if (ret == 1)
	{
		fprintf(stderr, "err signature matched on corrupted data!\n");
		return -1;
	}

	free(sig);
	RSA_free(rsa);
	EVP_PKEY_free(pubkey);
	EVP_PKEY_free(privkey);
	X509_free(x);

	return 0;
}
