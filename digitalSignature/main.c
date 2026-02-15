#if 0
Build with:
C:/ProgramFiles/mingw-w64-x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/bin/gcc -Wall -g3 -gdwarf-2 -Og -DDEBUG -o main.exe main.c tweetnacl.c

Debug with:
C:/ProgramFiles/mingw-w64-x86_64-8.1.0-posix-seh-rt_v6-rev0/mingw64/bin/gdb main.exe
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "tweetnacl.h"

// tweetnacl.h:
// #define crypto_sign_ed25519_tweet_BYTES 64
// #define crypto_sign_ed25519_tweet_SECRETKEYBYTES 64

void randombytes(uint8_t *sk, uint64_t size) {
    for(uint64_t u=0u; u<size; ++u) {
        /* In real use, this would become a true random function. */
        sk[u] = (uint8_t)(u & 0xFFu);
    }
}

int crypto_sign_detached(
    unsigned char *sig,
    unsigned long long *siglen,
    const unsigned char *m,
    unsigned long long mlen,
    const unsigned char *sk
) {
    unsigned char sm[mlen + 64];
    unsigned long long smlen;
    
    assert(sizeof(sm) >= mlen+64);
    crypto_sign(sm, &smlen, m, mlen, sk);
    assert(smlen == sizeof(sm));
    assert(*siglen >= smlen);
    *siglen = smlen;
    memcpy(sig, sm, smlen);
   
    return 0;
}

int crypto_sign_verify_detached(
    const unsigned char *sig,
    const unsigned char *m,
    unsigned long long mlen,
    const unsigned char *pk
) {
//    unsigned char sm[mlen + 64];
    unsigned char out[mlen + 64];
    unsigned long long outlen;

//    memcpy(sm, sig, 64);
//    memcpy(sm + 64, m, mlen);

//    if (crypto_sign_open(out, &outlen, sm, 64 + mlen, pk) != 0) {
    if (crypto_sign_open(out, &outlen, sig, 64 + mlen, pk) != 0) {
        /* Signature is invalid. */
        return -1; 
    }
    
    assert(outlen == mlen);
    assert(memcmp(out, m, mlen) == 0);
    
    /* Signature is valid. */
    return 0; 

} /* crypto_sign_verify_detached */


int main(void) {
    /* 32 Byte secret key, followed by 32 Byte of public key. The secret key is also called
       seed, as is allows to generate the public key. */
    uint8_t privateA[64]; 
    
    /* The public key, same as second half of privateA. */
    uint8_t publicA[32];

    /* Create a key pair. */
    crypto_sign_keypair(publicA, privateA);
    
    printf("Seed (secret key):              ");
    for(int i=0; i<32; ++i)
        printf(" %02X", privateA[i]);
    printf("\n");

    printf("publicA in 2nd half of privateA:");
    for(int i=32; i<sizeof(privateA); ++i)
        printf(" %02X", privateA[i]);
    printf("\n");

    printf("publicA:                        ");
    for(int i=0; i<sizeof(publicA); ++i)
        printf(" %02X", publicA[i]);
    printf("\n");

    /* Encrypt and sign a message msg. */
    const uint64_t msgLen = 10u;
    const unsigned char msg[10] = {1,2,3,4,5,6,7,8,9,0};
    uint8_t signedMsg[128+64];
    uint64_t signedMsgLen = sizeof(signedMsg);
    crypto_sign_detached(signedMsg, &signedMsgLen, msg, msgLen, privateA);
    assert(signedMsgLen == msgLen+64ull);
    
    printf("Signature:                      ");
    for(int i=0; i<signedMsgLen; ++i)
        printf(" %02X", signedMsg[i]);
    printf("\n");

    /* Check and decrypt message. */
    const int retCode = crypto_sign_verify_detached(signedMsg, msg, msgLen, publicA);
    printf("Decoding %s\n", retCode==0? "succeeded": "failed");

    return 0;
}
