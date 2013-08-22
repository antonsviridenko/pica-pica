 
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
