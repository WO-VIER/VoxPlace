#ifndef CRYPTO_HANDSHAKE_H
#define CRYPTO_HANDSHAKE_H

#include <cstddef>
#include <cstdint>
#include <string>

constexpr size_t VOXPLACE_PASSWORD_MAX_LENGTH = 128;
constexpr size_t VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES = 32;
constexpr size_t VOXPLACE_CRYPTO_SECRET_KEY_BYTES = 32;
constexpr size_t VOXPLACE_CRYPTO_NONCE_BYTES = 24;
constexpr size_t VOXPLACE_CRYPTO_BOX_MAC_BYTES = 16;
constexpr size_t VOXPLACE_LOGIN_CHALLENGE_BYTES = 32;
constexpr size_t VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES =
	VOXPLACE_LOGIN_CHALLENGE_BYTES + sizeof(uint8_t) + VOXPLACE_PASSWORD_MAX_LENGTH;
constexpr size_t VOXPLACE_ENCRYPTED_PASSWORD_BYTES =
	VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES + VOXPLACE_CRYPTO_BOX_MAC_BYTES;

struct EncryptedPasswordPayload
{
	uint8_t clientPublicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES] = {};
	uint8_t nonce[VOXPLACE_CRYPTO_NONCE_BYTES] = {};
	uint8_t ciphertext[VOXPLACE_ENCRYPTED_PASSWORD_BYTES] = {};
};

bool generateServerCryptoIdentity(
	uint8_t publicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	uint8_t secretKey[VOXPLACE_CRYPTO_SECRET_KEY_BYTES],
	std::string &errorMessage);

bool encryptPasswordPayload(
	const std::string &password,
	const uint8_t serverPublicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	const uint8_t challenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
	EncryptedPasswordPayload &payload,
	std::string &errorMessage);

bool decryptPasswordPayload(
	const EncryptedPasswordPayload &payload,
	const uint8_t serverSecretKey[VOXPLACE_CRYPTO_SECRET_KEY_BYTES],
	const uint8_t expectedChallenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
	std::string &password,
	std::string &errorMessage);

std::string bytesToHex(const uint8_t *data, size_t size);
bool hexToBytes(const std::string &hex, uint8_t *data, size_t size);
std::string serverPublicKeyFingerprint(
	const uint8_t publicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES]);

#endif
