#ifndef COOLDOWN_HUD_H
#define COOLDOWN_HUD_H

#include <WorldProtocol.h>

#include <imgui.h>

#include <cstdint>

class CooldownHud
{
public:
	CooldownHud() = default;

	bool loadAssets();
	void render(bool inGame,
				 uint64_t remainingCooldownMs,
				 uint64_t remainingBlockCooldownMs,
				 const ExpansionStatusMessage *expansionStatus);

private:
	ImFont *m_font = nullptr;
};

#endif
