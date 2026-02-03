#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

// Texture atlas
uniform sampler2D texture1;

// Couleur de tint pour l'herbe
// Extraite du vert des 3-4 premières lignes de grass_side
// Minecraft plains biome: #91BD59 = (145, 189, 89) / 255
const vec3 grassTint = vec3(0.57, 0.74, 0.35);

// Plage UV de la texture grass_top dans l'atlas (col=8, row=2)
// u: 8/16 à 9/16 = 0.5 à 0.5625
// v: (avec flip) 1 - 3/16 à 1 - 2/16 = 0.8125 à 0.875
const float GRASS_TOP_U_MIN = 0.5;
const float GRASS_TOP_U_MAX = 0.5625;
const float GRASS_TOP_V_MIN = 0.8125;
const float GRASS_TOP_V_MAX = 0.875;

void main()
{
	vec4 texColor = texture(texture1, TexCoord);
	
	// Détecter si on est sur la texture grass_top via les UV
	bool isGrassTop = (TexCoord.x >= GRASS_TOP_U_MIN && TexCoord.x <= GRASS_TOP_U_MAX &&
	                   TexCoord.y >= GRASS_TOP_V_MIN && TexCoord.y <= GRASS_TOP_V_MAX);
	
	if (isGrassTop) {
		// Appliquer le tint vert sur grass_top
		FragColor = vec4(texColor.rgb * grassTint, texColor.a);
	} else {
		// Garder la couleur originale pour les autres textures
		FragColor = texColor;
	}
}