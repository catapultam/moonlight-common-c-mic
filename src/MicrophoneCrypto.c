#include "Limelight-internal.h"

bool encryptMicrophonePacket(PPLT_CRYPTO_CONTEXT ctx,
                             const unsigned char* key, int keyLength,
                             uint32_t riKeyId, uint16_t sequenceNumber,
                             const unsigned char* opusData, int opusLength,
                             unsigned char* output, int* outputLength) {
    unsigned char iv[MIC_IV_LEN] = {0};
    uint32_t ivSeq;

    if (opusLength <= 0 || opusLength > MAX_MIC_PACKET_SIZE ||
            *outputLength < PKCS7_ENCRYPTED_LEN(opusLength)) {
        return false;
    }

    // The IV is the rikeyid plus the sequence number, in big endian
    ivSeq = BE32(riKeyId + sequenceNumber);
    memcpy(iv, &ivSeq, sizeof(ivSeq));

    // CIPHER_FLAG_FINISH lets the cipher add the PKCS#7 padding. Do not add
    // CIPHER_FLAG_PAD_TO_BLOCK_SIZE: with OpenSSL the two flags together add a
    // second padding block, which the host decrypts into bytes behind the
    // Opus data. With PSA crypto both flag sets give the same bytes. Without
    // CIPHER_FLAG_PAD_TO_BLOCK_SIZE the input is not modified, so the cast is safe.
    return PltEncryptMessage(ctx,
                             ALGORITHM_AES_CBC,
                             CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH,
                             (unsigned char*)key, keyLength,
                             iv, sizeof(iv),
                             NULL, 0,
                             (unsigned char*)opusData, opusLength,
                             output, outputLength);
}
