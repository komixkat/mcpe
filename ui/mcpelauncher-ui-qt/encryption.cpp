#include <encryption.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>

std::string Encryption::Encrypt(std::string raw, std::string key) {
    unsigned char aeskey[48];
    if(!PKCS5_PBKDF2_HMAC_SHA1(key.data(), key.size(), NULL, 0, 1000, sizeof(aeskey), aeskey)) {
        return "";
    }
    auto e_ctx = EVP_CIPHER_CTX_new();
    int r = EVP_EncryptInit_ex(e_ctx, EVP_aes_256_cbc(), NULL, aeskey, aeskey + 32);
    std::vector<unsigned char> encypted((raw.length() / 16 + 1) * 16);
    size_t wholelen = 0;
    int len = 0;
    r = EVP_EncryptUpdate(e_ctx, encypted.data(), &len, (const unsigned char*)raw.data(), raw.length());
    wholelen += len;
    r = EVP_EncryptFinal_ex(e_ctx, encypted.data() + wholelen, &len);
    wholelen += len;
    EVP_CIPHER_CTX_free(e_ctx);

    std::vector<unsigned char> b64enc(EVP_ENCODE_LENGTH(wholelen));
    auto ol = EVP_EncodeBlock(b64enc.data(), encypted.data(), wholelen);
    return std::string(b64enc.data(), b64enc.data() + ol);
}

std::string Encryption::Decrypt(std::string raw, std::string key) {
    std::vector<unsigned char> decoded(EVP_DECODE_LENGTH(raw.size()));
    auto ol = EVP_DecodeBlock(decoded.data(), (const unsigned char*)raw.data(), raw.length()) / 16 * 16;
    decoded.resize(ol);
    unsigned char aeskey[48];
    if(!PKCS5_PBKDF2_HMAC_SHA1(key.data(), key.size(), NULL, 0, 1000, sizeof(aeskey), aeskey)) {
        return "";
    }
    auto d_ctx = EVP_CIPHER_CTX_new();
    int r = EVP_DecryptInit_ex(d_ctx, EVP_aes_256_cbc(), NULL, aeskey, aeskey + 32);
    
    std::vector<char> dec(raw.length() + 32);
    size_t wholelen = 0;
    int outlen = 0;
    r = EVP_DecryptUpdate(d_ctx, (unsigned char *)dec.data(), &outlen, (const unsigned char *)decoded.data(), decoded.size());
    wholelen += outlen;
    r = EVP_DecryptFinal_ex(d_ctx, (unsigned char *)dec.data() + outlen, &outlen);
    wholelen += outlen;
    EVP_CIPHER_CTX_free(d_ctx);
    return std::string(dec.data(), dec.data() + wholelen);
}

std::string Encryption::RandomKey() {
    unsigned char buf[255];
    RAND_bytes(buf, sizeof(buf));
    std::vector<unsigned char> b64enc(EVP_ENCODE_LENGTH(sizeof(buf)));
    auto ol = EVP_EncodeBlock(b64enc.data(), buf, sizeof(buf));
    return std::string(b64enc.data(), b64enc.data() + ol);
}
