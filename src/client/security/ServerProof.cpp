#include <client/security/ServerProof.h>

#include <curl/curl.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace
{
	constexpr size_t MAX_PROOF_JSON_BYTES = 16 * 1024;

	bool isOfficialVoxPlaceHost(const std::string &hostName)
	{
		if (hostName == "voxplace.codes")
		{
			return true;
		}
		if (hostName == "play.voxplace.codes")
		{
			return true;
		}
		return false;
	}

	std::string officialProofUrl()
	{
		const char *overrideUrl = std::getenv("VOXPLACE_SERVER_PROOF_URL");
		if (overrideUrl != nullptr && overrideUrl[0] != '\0')
		{
			return overrideUrl;
		}
		return "https://voxplace.codes/.proof/voxplace-server.json";
	}

	size_t appendCurlBody(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		std::string *body = static_cast<std::string *>(userdata);
		size_t byteCount = size * nmemb;
		if (body->size() + byteCount > MAX_PROOF_JSON_BYTES)
		{
			return 0;
		}
		body->append(ptr, byteCount);
		return byteCount;
	}

	bool fetchHttpsProof(const std::string &url, std::string &body)
	{
		static bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == 0;
		if (!curlReady)
		{
			return false;
		}
		CURL *curl = curl_easy_init();
		if (curl == nullptr)
		{
			return false;
		}

		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlBody);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "VoxPlace/1.0");
		CURLcode result = curl_easy_perform(curl);
		long statusCode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
		curl_easy_cleanup(curl);
		if (result != CURLE_OK || statusCode < 200 || statusCode >= 300)
		{
			body.clear();
			return false;
		}
		return true;
	}

	bool extractJsonStringField(
		const std::string &json,
		const std::string &fieldName,
		std::string &value)
	{
		std::string key = "\"" + fieldName + "\"";
		size_t keyOffset = json.find(key);
		if (keyOffset == std::string::npos)
		{
			return false;
		}
		size_t colonOffset = json.find(':', keyOffset + key.size());
		if (colonOffset == std::string::npos)
		{
			return false;
		}
		size_t valueBegin = json.find('"', colonOffset + 1);
		if (valueBegin == std::string::npos)
		{
			return false;
		}
		size_t valueEnd = json.find('"', valueBegin + 1);
		if (valueEnd == std::string::npos)
		{
			return false;
		}
		value = json.substr(valueBegin + 1, valueEnd - valueBegin - 1);
		return true;
	}

	bool extractProofFingerprint(const std::string &json, std::string &fingerprint)
	{
		if (extractJsonStringField(json, "public_key_fingerprint", fingerprint))
		{
			return true;
		}
		if (extractJsonStringField(json, "fingerprint", fingerprint))
		{
			return true;
		}
		return false;
	}

	std::filesystem::path trustedServersPath()
	{
		const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
		if (xdgConfigHome != nullptr && xdgConfigHome[0] != '\0')
		{
			return std::filesystem::path(xdgConfigHome) /
				"VoxPlace" / "trusted_servers.txt";
		}

		const char *home = std::getenv("HOME");
		if (home != nullptr && home[0] != '\0')
		{
			return std::filesystem::path(home) /
				".config" / "VoxPlace" / "trusted_servers.txt";
		}
		return "voxplace_trusted_servers.txt";
	}

	std::string trustedServerKey(const std::string &hostName, uint16_t port)
	{
		return hostName + ":" + std::to_string(port);
	}

	bool readTrustedFingerprint(const std::string &key, std::string &fingerprint)
	{
		std::ifstream input(trustedServersPath());
		if (!input.is_open())
		{
			return false;
		}

		std::string storedKey;
		std::string storedFingerprint;
		while (input >> storedKey >> storedFingerprint)
		{
			if (storedKey == key)
			{
				fingerprint = storedFingerprint;
				return true;
			}
		}
		return false;
	}

	void writeTrustedFingerprints(
		const std::string &key,
		const std::string &fingerprint)
	{
		std::filesystem::path path = trustedServersPath();
		std::filesystem::path parentPath = path.parent_path();
		if (!parentPath.empty())
		{
			std::error_code error;
			std::filesystem::create_directories(parentPath, error);
		}
		std::ifstream input(path);
		std::stringstream rewritten;
		bool replaced = false;
		std::string storedKey;
		std::string storedFingerprint;
		while (input >> storedKey >> storedFingerprint)
		{
			if (storedKey == key)
			{
				rewritten << key << " " << fingerprint << "\n";
				replaced = true;
			}
			else
			{
				rewritten << storedKey << " " << storedFingerprint << "\n";
			}
		}
		if (!replaced)
		{
			rewritten << key << " " << fingerprint << "\n";
		}
		std::ofstream output(path, std::ios::trunc);
		output << rewritten.str();
	}

	bool verifyTofu(
		const std::string &hostName,
		uint16_t port,
		const std::string &fingerprint,
		std::string &errorMessage)
	{
		std::string key = trustedServerKey(hostName, port);
		std::string storedFingerprint;
		if (!readTrustedFingerprint(key, storedFingerprint))
		{
			writeTrustedFingerprints(key, fingerprint);
			return true;
		}
		if (storedFingerprint == fingerprint)
		{
			return true;
		}
		errorMessage = "Server public key changed for " + key;
		return false;
	}

	bool verifyOfficialProof(
		const std::string &expectedFingerprint,
		bool &proofChecked,
		std::string &errorMessage)
	{
		std::string proofBody;
		proofChecked = false;
		if (!fetchHttpsProof(officialProofUrl(), proofBody))
		{
			return true;
		}

		std::string proofFingerprint;
		proofChecked = true;
		if (!extractProofFingerprint(proofBody, proofFingerprint))
		{
			errorMessage = "HTTPS proof did not contain a server fingerprint";
			return false;
		}
		if (proofFingerprint != expectedFingerprint)
		{
			errorMessage = "HTTPS server proof does not match ENet public key";
			return false;
		}
		return true;
	}
}

bool verifyServerPublicKeyProof(
	const std::string &hostName,
	uint16_t port,
	const uint8_t serverPublicKey[VOXPLACE_CRYPTO_PUBLIC_KEY_BYTES],
	std::string &errorMessage)
{
	errorMessage.clear();
	std::string fingerprint = serverPublicKeyFingerprint(serverPublicKey);
	bool proofChecked = false;
	if (isOfficialVoxPlaceHost(hostName))
	{
		if (!verifyOfficialProof(fingerprint, proofChecked, errorMessage))
		{
			return false;
		}
	}
	if (proofChecked)
	{
		writeTrustedFingerprints(trustedServerKey(hostName, port), fingerprint);
		return true;
	}
	if (isOfficialVoxPlaceHost(hostName))
	{
		std::cerr << "Warning: HTTPS .proof unavailable, falling back to TOFU"
				  << std::endl;
	}
	return verifyTofu(hostName, port, fingerprint, errorMessage);
}
