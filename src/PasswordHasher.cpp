#include <PasswordHasher.h>

#include <sodium.h>

PasswordHasher::PasswordHasher()
{
	if (sodium_init() >= 0)
	{
		m_ready = true;
	}
	else
	{
		m_lastError = "Failed to initialize libsodium";
	}
}

bool PasswordHasher::isReady() const
{
	return m_ready;
}

bool PasswordHasher::hashPassword(const std::string &password, std::string &passwordHash)
{
	m_lastError.clear();
	if (!m_ready)
	{
		m_lastError = "Password hasher is not ready";
		return false;
	}

	char buffer[crypto_pwhash_STRBYTES];
	if (crypto_pwhash_str(
			buffer,
			password.c_str(),
			password.size(),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
	{
		m_lastError = "Failed to hash password";
		return false;
	}

	passwordHash = buffer;
	return true;
}

bool PasswordHasher::verifyPassword(const std::string &password, const std::string &passwordHash)
{
	m_lastError.clear();
	if (!m_ready)
	{
		m_lastError = "Password hasher is not ready";
		return false;
	}

	if (passwordHash.empty())
	{
		m_lastError = "Stored password hash is empty";
		return false;
	}

	if (crypto_pwhash_str_verify(
			passwordHash.c_str(),
			password.c_str(),
			password.size()) != 0)
	{
		m_lastError = "Password verification failed";
		return false;
	}

	return true;
}

const std::string &PasswordHasher::lastError() const
{
	return m_lastError;
}
