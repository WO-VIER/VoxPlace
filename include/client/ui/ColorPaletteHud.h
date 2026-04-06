#ifndef CLIENT_UI_COLOR_PALETTE_HUD_H
#define CLIENT_UI_COLOR_PALETTE_HUD_H

#include <imgui.h>

class ColorPaletteHud
{
public:
	ColorPaletteHud();
	~ColorPaletteHud();

	bool loadAssets();
	void cleanup();
	void render(bool inGame, int selectedPaletteIndex);

private:
	struct UiTexture;

	ImFont *m_font = nullptr;
	UiTexture *m_barTexture = nullptr;
	UiTexture *m_selectedTexture = nullptr;
};

#endif
