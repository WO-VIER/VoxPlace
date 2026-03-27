#ifndef PASSWORD_HASHER_H
#define PASSWORD_HASHER_H

#include <string>

class PasswordHasher
{
public:
	PasswordHasher();

	bool isReady() const;
	bool hashPassword(const std::string &password, std::string &passwordHash);
	bool verifyPassword(const std::string &password, const std::string &passwordHash);
	const std::string &lastError() const;

private:
	bool m_ready = false;
	std::string m_lastError;
};

#endif
