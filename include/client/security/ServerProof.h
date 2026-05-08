#ifndef CLIENT_SECURITY_SERVER_PROOF_H
#define CLIENT_SECURITY_SERVER_PROOF_H

#include <CryptoHandshake.h>

#include <cstdint>
#include <string>

bool verifyServerPublicKeyProof(
	const std::string &hostName,
	uint16_t port,
	const uint8_t serverPublicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	std::string &errorMessage);

#endif
