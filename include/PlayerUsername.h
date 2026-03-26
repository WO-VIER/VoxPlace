#ifndef PLAYER_USERNAME_H
#define PLAYER_USERNAME_H

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

constexpr size_t PLAYER_USERNAME_MIN_LENGTH = 3;
constexpr size_t PLAYER_USERNAME_MAX_LENGTH = 24;

enum class PlayerUsernameValidationError
{
	None = 0,
	Empty,
	TooShort,
	TooLong,
	InvalidCharacter
};

inline bool isPlayerUsernameWhitespace(char character)
{
	return character == ' '
		|| character == '\t'
		|| character == '\n'
		|| character == '\r'
		|| character == '\f'
		|| character == '\v';
}

inline std::string trimPlayerUsername(std::string_view rawUsername)
{
	size_t begin = 0;
	size_t end = rawUsername.size();

	while (begin < end && isPlayerUsernameWhitespace(rawUsername[begin]))
	{
		begin++;
	}

	while (end > begin && isPlayerUsernameWhitespace(rawUsername[end - 1]))
	{
		end--;
	}

	return std::string(rawUsername.substr(begin, end - begin));
}

inline bool isValidPlayerUsernameCharacter(char character)
{
	if (character >= 'a' && character <= 'z')
	{
		return true;
	}
	if (character >= 'A' && character <= 'Z')
	{
		return true;
	}
	if (character >= '0' && character <= '9')
	{
		return true;
	}
	if (character == '_' || character == '-')
	{
		return true;
	}
	return false;
}

inline PlayerUsernameValidationError validatePlayerUsername(std::string_view username)
{
	if (username.empty())
	{
		return PlayerUsernameValidationError::Empty;
	}
	if (username.size() < PLAYER_USERNAME_MIN_LENGTH)
	{
		return PlayerUsernameValidationError::TooShort;
	}
	if (username.size() > PLAYER_USERNAME_MAX_LENGTH)
	{
		return PlayerUsernameValidationError::TooLong;
	}

	for (char character : username)
	{
		if (!isValidPlayerUsernameCharacter(character))
		{
			return PlayerUsernameValidationError::InvalidCharacter;
		}
	}

	return PlayerUsernameValidationError::None;
}

inline const char *playerUsernameValidationErrorText(PlayerUsernameValidationError error)
{
	switch (error)
	{
	case PlayerUsernameValidationError::None:
		return "ok";
	case PlayerUsernameValidationError::Empty:
		return "username is empty";
	case PlayerUsernameValidationError::TooShort:
		return "username is too short";
	case PlayerUsernameValidationError::TooLong:
		return "username is too long";
	case PlayerUsernameValidationError::InvalidCharacter:
		return "username contains invalid characters";
	}
	return "username validation error";
}

inline bool copyPlayerUsernameToBuffer(std::string_view username,
									   char (&buffer)[PLAYER_USERNAME_MAX_LENGTH + 1])
{
	if (username.size() > PLAYER_USERNAME_MAX_LENGTH)
	{
		return false;
	}

	std::fill(std::begin(buffer), std::end(buffer), '\0');
	for (size_t index = 0; index < username.size(); index++)
	{
		buffer[index] = username[index];
	}
	return true;
}

inline std::string playerUsernameFromBuffer(const char (&buffer)[PLAYER_USERNAME_MAX_LENGTH + 1])
{
	return std::string(buffer);
}

#endif
