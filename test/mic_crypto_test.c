// Test of the microphone packet encryption against the host decrypt logic.
//
// Build and run in the moonlight toolbox:
//   gcc -O0 -g -I src -I enet/include test/mic_crypto_test.c \
//       src/MicrophoneCrypto.c src/PlatformCrypto.c -lcrypto -o /tmp/mic_crypto_test \
//   && /tmp/mic_crypto_test
#include "Limelight-internal.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_RI_KEY_ID 0xF0E1D2C3u
// The largest Opus packet that fits a MAX_MIC_PACKET_SIZE packet with its 12 byte header
#define MAX_TEST_OPUS_LEN 1388

// Copy of crypto::cipher::cbc_t::decrypt() in Apollo src/crypto.cpp with
// padding = true, as stream.cpp constructs session->audio.cipher. The host
// keeps one decrypt context for the whole session and sets the IV per packet.
static int hostDecrypt(EVP_CIPHER_CTX* ctx, bool* initialized,
                       const unsigned char* key, const unsigned char* iv,
                       const unsigned char* cipher, int cipherLength,
                       unsigned char* plain, int* plainLength) {
    int updateLength = 0;
    int finalLength = 0;

    if (!*initialized) {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) != 1) {
            return -1;
        }
        EVP_CIPHER_CTX_set_padding(ctx, 1);
        *initialized = true;
    }

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, NULL, iv) != 1) {
        return -1;
    }
    if (EVP_DecryptUpdate(ctx, plain, &updateLength, cipher, cipherLength) != 1) {
        return -2;
    }
    if (EVP_DecryptFinal_ex(ctx, plain + updateLength, &finalLength) != 1) {
        return -3;
    }

    *plainLength = updateLength + finalLength;
    return 0;
}

// Builds the IV the host builds: BE32(avRiKeyId + sequenceNumber) in the first
// 4 bytes, the rest zero.
static void hostIv(uint32_t riKeyId, uint16_t sequenceNumber, unsigned char iv[16]) {
    uint32_t ivSeq = BE32(riKeyId + sequenceNumber);
    memset(iv, 0, 16);
    memcpy(iv, &ivSeq, sizeof(ivSeq));
}

static int failures;

static void check(bool condition, const char* what, int opusLength) {
    if (!condition) {
        failures++;
        printf("FAIL: %s (opus length %d)\n", what, opusLength);
    }
}

int main(void) {
    unsigned char key[16];
    PPLT_CRYPTO_CONTEXT clientCtx = PltCreateCryptoContext();
    EVP_CIPHER_CTX* hostCtx = EVP_CIPHER_CTX_new();
    bool hostInitialized = false;
    uint16_t sequenceNumber = 0;

    for (int i = 0; i < 16; i++) {
        key[i] = (unsigned char)(i * 7 + 1);
    }

    // One packet per length, in sequence, on one context pair as in a stream
    for (int opusLength = 1; opusLength <= MAX_TEST_OPUS_LEN; opusLength++, sequenceNumber++) {
        unsigned char opus[MAX_TEST_OPUS_LEN];
        unsigned char encrypted[ROUND_TO_PKCS7_PADDED_LEN(MAX_MIC_PACKET_SIZE)];
        unsigned char plain[ROUND_TO_PKCS7_PADDED_LEN(MAX_MIC_PACKET_SIZE) + 16];
        unsigned char iv[16];
        int encryptedLength = (int)sizeof(encrypted);
        int plainLength = 0;

        for (int i = 0; i < opusLength; i++) {
            opus[i] = (unsigned char)(i ^ 0x5A);
        }

        check(encryptMicrophonePacket(clientCtx, key, sizeof(key), TEST_RI_KEY_ID, sequenceNumber,
                                      opus, opusLength, encrypted, &encryptedLength),
              "encryptMicrophonePacket() returned false", opusLength);

        // The host expects one PKCS#7 padding: 1 to 16 bytes, so a multiple of 16 gets
        // a full block. ROUND_TO_PKCS7_PADDED_LEN() rounds up and gives 16 for 16, so it
        // is the wrong expectation here.
        check(encryptedLength == PKCS7_ENCRYPTED_LEN(opusLength),
              "encrypted length is not PKCS7_ENCRYPTED_LEN(opusLength)", opusLength);

        hostIv(TEST_RI_KEY_ID, sequenceNumber, iv);
        check(hostDecrypt(hostCtx, &hostInitialized, key, iv, encrypted, encryptedLength, plain, &plainLength) == 0,
              "host decrypt failed", opusLength);
        check(plainLength == opusLength, "host got a different length", opusLength);
        check(memcmp(plain, opus, opusLength) == 0, "host got different data", opusLength);
    }

    // A full output buffer must not be written past its end
    {
        unsigned char opus[32] = {0};
        unsigned char small[16];
        int smallLength = (int)sizeof(small);
        check(!encryptMicrophonePacket(clientCtx, key, sizeof(key), TEST_RI_KEY_ID, 0,
                                       opus, sizeof(opus), small, &smallLength),
              "encryptMicrophonePacket() accepted a too small output buffer", 32);
    }

    EVP_CIPHER_CTX_free(hostCtx);
    PltDestroyCryptoContext(clientCtx);

    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }

    printf("mic_crypto_test: all checks passed\n");
    return 0;
}
