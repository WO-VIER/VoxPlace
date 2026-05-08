#include <CryptoHandshake.h>

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace
{
	constexpr size_t PASSWORD_LENGTH_OFFSET = VOXPLACE_LOGIN_CHALLENGE_BYTES;
	constexpr size_t PASSWORD_TEXT_OFFSET = PASSWORD_LENGTH_OFFSET + sizeof(uint8_t);

	bool ensureSodiumReady(std::string &errorMessage)
	{
		if (sodium_init() < 0)
		{
			errorMessage = "Failed to initialize libsodium";
			return false;
		}
		return true;
	}

	int hexDigitValue(char character)
	{
		if (character >= '0' && character <= '9')
		{
			return character - '0';
		}
		if (character >= 'a' && character <= 'f')
		{
			return 10 + character - 'a';
		}
		if (character >= 'A' && character <= 'F')
		{
			return 10 + character - 'A';
		}
		return -1;
	}

	void buildPasswordPlaintext(
		const std::string &password,
		const uint8_t challenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
		uint8_t plaintext[VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES])
	{
		std::memset(plaintext, 0, VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES);
		std::memcpy(plaintext, challenge, VOXPLACE_LOGIN_CHALLENGE_BYTES);
		plaintext[PASSWORD_LENGTH_OFFSET] = static_cast<uint8_t>(password.size());
		std::memcpy(plaintext + PASSWORD_TEXT_OFFSET, password.data(), password.size());
	}

	bool extractPasswordPlaintext(
		const uint8_t plaintext[VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES],
		const uint8_t expectedChallenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
		std::string &password,
		std::string &errorMessage)
	{
		if (sodium_memcmp(
				plaintext,
				expectedChallenge,
				VOXPLACE_LOGIN_CHALLENGE_BYTES) != 0)
		{
			errorMessage = "Encrypted password challenge mismatch";
			return false;
		}

		size_t passwordLength = plaintext[PASSWORD_LENGTH_OFFSET];
		if (passwordLength > VOXPLACE_PASSWORD_MAX_LENGTH)
		{
			errorMessage = "Encrypted password length is invalid";
			return false;
		}

		password.assign(
			reinterpret_cast<const char *>(plaintext + PASSWORD_TEXT_OFFSET),
			passwordLength);
		return true;
	}
}

bool generateServerCryptoIdentity(
	uint8_t publicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	uint8_t secretKey[VOXPLACE_CRYPTO_SECRET_KEY_BYTES],
	std::string &errorMessage)
{
	errorMessage.clear();
	if (!ensureSodiumReady(errorMessage))
	{
		return false;
	}
	crypto_box_keypair(publicKey, secretKey);
	return true;
}

bool encryptPasswordPayload(
	const std::string &password,
	const uint8_t serverPublicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	const uint8_t challenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
	EncryptedPasswordPayload &payload,
	std::string &errorMessage)
{
	errorMessage.clear();
	if (!ensureSodiumReady(errorMessage))
	{
		return false;
	}
	if (password.size() > VOXPLACE_PASSWORD_MAX_LENGTH)
	{
		errorMessage = "Password is too long";
		return false;
	}

	uint8_t clientSecretKey[VOXPLACE_CRYPTO_SECRET_KEY_BYTES];
	uint8_t plaintext[VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES];
	crypto_box_keypair(payload.clientPublicKey, clientSecretKey);
	randombytes_buf(payload.nonce, sizeof(payload.nonce));
	buildPasswordPlaintext(password, challenge, plaintext);

	if (crypto_box_easy(
			payload.ciphertext,
			plaintext,
			VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES,
			payload.nonce,
			serverPublicKey,
			clientSecretKey) != 0)
	{
		errorMessage = "Failed to encrypt password payload";
		sodium_memzero(clientSecretKey, sizeof(clientSecretKey));
		sodium_memzero(plaintext, sizeof(plaintext));
		return false;
	}

	sodium_memzero(clientSecretKey, sizeof(clientSecretKey));
	sodium_memzero(plaintext, sizeof(plaintext));
	return true;
}

bool decryptPasswordPayload(
	const EncryptedPasswordPayload &payload,
	const uint8_t serverSecretKey[VOXPLACE_CRYPTO_SECRET_KEY_BYTES],
	const uint8_t expectedChallenge[VOXPLACE_LOGIN_CHALLENGE_BYTES],
	std::string &password,
	std::string &errorMessage)
{
	errorMessage.clear();
	password.clear();
	if (!ensureSodiumReady(errorMessage))
	{
		return false;
	}

	uint8_t plaintext[VOXPLACE_PASSWORD_BOX_PLAINTEXT_BYTES];
	if (crypto_box_open_easy(
			plaintext,
			payload.ciphertext,
			VOXPLACE_ENCRYPTED_PASSWORD_BYTES,
			payload.nonce,
			payload.clientPublicKey,
			serverSecretKey) != 0)
	{
		errorMessage = "Failed to decrypt password payload";
		sodium_memzero(plaintext, sizeof(plaintext));
		return false;
	}

	bool extracted = extractPasswordPlaintext(
		plaintext,
		expectedChallenge,
		password,
		errorMessage);
	sodium_memzero(plaintext, sizeof(plaintext));
	return extracted;
}

std::string bytesToHex(const uint8_t *data, size_t size)
{
	const char *digits = "0123456789abcdef";
	std::string output;
	output.resize(size * 2);
	for (size_t index = 0; index < size; index++)
	{
		output[index * 2] = digits[(data[index] >> 4) & 0x0f];
		output[index * 2 + 1] = digits[data[index] & 0x0f];
	}
	return output;
}

bool hexToBytes(const std::string &hex, uint8_t *data, size_t size)
{
	if (hex.size() != size * 2)
	{
		return false;
	}
	for (size_t index = 0; index < size; index++)
	{
		int high = hexDigitValue(hex[index * 2]);
		int low = hexDigitValue(hex[index * 2 + 1]);
		if (high < 0 || low < 0)
		{
			return false;
		}
		data[index] = static_cast<uint8_t>((high << 4) | low);
	}
	return true;
}

std::string serverPublicKeyFingerprint(
	const uint8_t publicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES])
{
	uint8_t digest[crypto_hash_sha256_BYTES];
	crypto_hash_sha256(digest, publicKey, VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES);
	std::string fingerprint = "SHA256:";
	fingerprint += bytesToHex(digest, sizeof(digest));
	sodium_memzero(digest, sizeof(digest));
	return fingerprint;
}
