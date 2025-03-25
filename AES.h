#ifndef AES_H
#define AES_H

#include <stdint.h>
#include <stdio.h>


#define AES_BLOCK_SIZE 16 // Kích thước khối AES (byte)
#define AES_KEY_SIZE 16   // Kích thước khóa AES-128 (byte)
#define AES_NR 10         // Số vòng AES-128

typedef struct {
    uint8_t round_keys[176]; // Khóa vòng (11 khóa, mỗi khóa 16 byte)
} AESContext;

// Khởi tạo context AES với khóa
void aes_init(AESContext *ctx, const uint8_t *key);

// Mã hóa dữ liệu (plaintext -> ciphertext) theo ECB
void aes_encrypt_ecb(AESContext *ctx, const uint8_t *input, uint8_t *output, size_t len);

// Giải mã dữ liệu (ciphertext -> plaintext) theo ECB
void aes_decrypt_ecb(AESContext *ctx, const uint8_t *input, uint8_t *output, size_t len);

#endif