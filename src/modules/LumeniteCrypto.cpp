#include "LumeniteCrypto.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <lua.hpp>

// Convert bytes to hex
static std::string to_hex(const unsigned char *data, size_t len)
{
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int) data[i];
    return oss.str();
}

static int l_sha256(lua_State *L)
{
    const char *input = luaL_checkstring(L, 1);
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *) input, strlen(input), hash);
    lua_pushstring(L, to_hex(hash, sizeof(hash)).c_str());
    return 1;
}

static int l_random_bytes(lua_State *L)
{
    int len = luaL_checkinteger(L, 1);
    std::vector<unsigned char> buf(len);
    if (RAND_bytes(buf.data(), len) != 1)
        return luaL_error(L, "RAND_bytes failed");
    lua_pushlstring(L, (const char *) buf.data(), len);
    return 1;
}

// AES-256-CBC encryption
static int l_encrypt(lua_State *L)
{
    size_t key_len, plain_len;
    const char *key = luaL_checklstring(L, 1, &key_len);
    const char *plaintext = luaL_checklstring(L, 2, &plain_len);

    if (key_len != 32)
        return luaL_error(L, "Key must be exactly 32 bytes");

    unsigned char iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1)
        return luaL_error(L, "Failed to generate IV");

    std::vector<unsigned char> ciphertext(plain_len + EVP_MAX_BLOCK_LENGTH);
    int len, total_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return luaL_error(L, "EVP_CIPHER_CTX_new failed");

    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (const unsigned char *) key, iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_EncryptInit_ex failed");
    }

    if (!EVP_EncryptUpdate(ctx, ciphertext.data(), &len, (const unsigned char *) plaintext, plain_len)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_EncryptUpdate failed");
    }
    total_len += len;

    if (!EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_EncryptFinal_ex failed");
    }
    total_len += len;
    EVP_CIPHER_CTX_free(ctx);

    std::string result((const char *) iv, sizeof(iv));
    result.append((const char *) ciphertext.data(), total_len);
    lua_pushlstring(L, result.data(), result.size());
    return 1;
}

static int l_decrypt(lua_State *L)
{
    size_t key_len, input_len;
    const char *key = luaL_checklstring(L, 1, &key_len);
    const char *input = luaL_checklstring(L, 2, &input_len);

    if (key_len != 32)
        return luaL_error(L, "Key must be exactly 32 bytes");

    if (input_len < 16)
        return luaL_error(L, "Input too short to contain IV");

    const unsigned char *iv = (const unsigned char *) input;
    const unsigned char *ciphertext = (const unsigned char *) (input + 16);
    int ciphertext_len = (int) (input_len - 16);

    std::vector<unsigned char> plaintext(ciphertext_len + EVP_MAX_BLOCK_LENGTH);
    int len, total_len = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return luaL_error(L, "EVP_CIPHER_CTX_new failed");

    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (const unsigned char *) key, iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_DecryptInit_ex failed");
    }

    if (!EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_DecryptUpdate failed");
    }
    total_len += len;

    if (!EVP_DecryptFinal_ex(ctx, plaintext.data() + total_len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return luaL_error(L, "EVP_DecryptFinal_ex failed: data may be corrupted or key wrong");
    }
    total_len += len;
    EVP_CIPHER_CTX_free(ctx);

    lua_pushlstring(L, (const char *) plaintext.data(), total_len);
    return 1;
}

// PBKDF2-HMAC-SHA256
static int l_secure_hash(lua_State *L)
{
    const char *pw = luaL_checkstring(L, 1);
    unsigned char salt[16], hash[32];

    if (RAND_bytes(salt, sizeof(salt)) != 1)
        return luaL_error(L, "Failed to generate salt");

    if (!PKCS5_PBKDF2_HMAC(pw, strlen(pw), salt, sizeof(salt), 100000, EVP_sha256(), sizeof(hash), hash))
        return luaL_error(L, "PBKDF2 hashing failed");

    std::ostringstream oss;
    oss << "$pbkdf2$100000$" << to_hex(salt, 16) << "$" << to_hex(hash, 32);
    lua_pushstring(L, oss.str().c_str());
    return 1;
}

static int l_secure_verify(lua_State *L)
{
    const char *pw = luaL_checkstring(L, 1);
    const char *stored = luaL_checkstring(L, 2);
    int iters;
    char salt_hex[33], hash_hex[65];

    if (sscanf(stored, "$pbkdf2$%d$%32[^$]$%64s", &iters, salt_hex, hash_hex) != 3) {
        lua_pushboolean(L, 0);
        return 1;
    }

    unsigned char salt[16], expected[32], computed[32];
    for (int i = 0; i < 16; ++i) sscanf(salt_hex + 2 * i, "%2hhx", &salt[i]);
    for (int i = 0; i < 32; ++i) sscanf(hash_hex + 2 * i, "%2hhx", &expected[i]);

    if (!PKCS5_PBKDF2_HMAC(pw, strlen(pw), salt, sizeof(salt), iters, EVP_sha256(), sizeof(computed), computed)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_pushboolean(L, CRYPTO_memcmp(expected, computed, 32) == 0);
    return 1;
}

int LumeniteCrypto::luaopen(lua_State *L)
{
    lua_newtable(L);

    lua_pushcfunction(L, l_sha256);
    lua_setfield(L, -2, "sha256");

    lua_pushcfunction(L, l_random_bytes);
    lua_setfield(L, -2, "random");

    lua_pushcfunction(L, l_encrypt);
    lua_setfield(L, -2, "encrypt");

    lua_pushcfunction(L, l_decrypt);
    lua_setfield(L, -2, "decrypt");

    lua_pushcfunction(L, l_secure_hash);
    lua_setfield(L, -2, "hash");

    lua_pushcfunction(L, l_secure_verify);
    lua_setfield(L, -2, "verify");

    return 1;
}
