#include "crypto.hpp"
#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <cstring>
#include <memory>

using EVP_CIPHER_CTX_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

namespace Crypto {

    // Base64 Decode
    Result<std::vector<uint8_t>> base64_decode(std::string_view input) noexcept {
        try {
            // Calculate max decoded length
            size_t len = input.length();
            std::vector<uint8_t> out(len);

            // Create BIO chain
            std::unique_ptr<BIO, decltype(&BIO_free_all)> bio(BIO_new_mem_buf(input.data(), -1), BIO_free_all);
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // No newlines
            bio.reset(BIO_push(b64, bio.release()));

            int decoded_len = BIO_read(bio.get(), out.data(), len);
            if (decoded_len < 0) {
                return tl::unexpected(BotError("Failed to decode Base64 string"));
            }
            out.resize(decoded_len);
            return out;
        } catch (const std::exception& e) {
            return tl::unexpected(BotError(fmt::format("Base64 decode error: {}", e.what())));
        } catch (...) {
            return tl::unexpected(BotError("Base64 decode unknown error"));
        }
    }

    // Base64 Encode (for encrypted output)
    std::string base64_encode(std::span<uint8_t> data) noexcept {
        try {
            std::unique_ptr<BIO, decltype(&BIO_free_all)> bio(BIO_new(BIO_s_mem()), BIO_free_all);
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
            bio.reset(BIO_push(b64, bio.release()));

            BIO_write(bio.get(), data.data(), data.size());
            BIO_flush(bio.get());

            BUF_MEM* bufferPtr;
            BIO_get_mem_ptr(bio.get(), &bufferPtr);
            std::string result(bufferPtr->data, bufferPtr->length);
            return result;
        } catch (...) {
            return {};
        }
    }

    // AES-128-ECB Encryption
    Result<std::string> aes_encrypt_ecb(std::string_view plaintext, std::string_view key) noexcept {
        try {
            EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
            if (!ctx) return tl::unexpected(BotError("Failed to create cipher context"));

            // Initialize encryption operation
            if (1 != EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr,
                                        reinterpret_cast<const uint8_t*>(key.data()), nullptr)) {
                return tl::unexpected(BotError("Failed to init AES encryption"));
            }

            // Enable PKCS7 Padding
            EVP_CIPHER_CTX_set_padding(ctx.get(), 1);

            std::vector<uint8_t> ciphertext(plaintext.size() + AES_BLOCK_SIZE);
            int len = 0;
            int ciphertext_len = 0;

            // Update data
            if (1 != EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len,
                                       reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size())) {
                return tl::unexpected(BotError("Encryption update failed"));
            }
            ciphertext_len = len;

            // Finalize data
            if (1 != EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len)) {
                return tl::unexpected(BotError("Encryption finalization failed"));
            }
            ciphertext_len += len;

            ciphertext.resize(ciphertext_len);
            std::string encoded = base64_encode(ciphertext);
            return encoded;
        } catch (const std::exception& e) {
            return tl::unexpected(BotError(fmt::format("AES encryption error: {}", e.what())));
        }
    }
}  // namespace Crypto
