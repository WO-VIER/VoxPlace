#ifndef COOLDOWN_HUD_H
#define COOLDOWN_HUD_H

#include <imgui.h>

#include <cstdint>

class CooldownHud
{
public:
	CooldownHud() = default;

	bool loadAssets();
	void render(bool inGame, uint64_t remainingCooldownMs);

private:
	ImFont *m_font = nullptr;
};

#endif
