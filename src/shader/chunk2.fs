#version 460 core

out vec4 FragColor;

// Données du vertex shader
in flat int vColorIndex;
in flat int vFaceDir;
in flat int vShade;      // 1 = éclairé, 0 = ombré (sunblock diagonal)
in vec3 vFragPos;
in flat vec3 vWorldBlockPos;  // Position monde du bloc (flat = pas d'interpolation)
in float vAO;  // AO interpolé par le rasterizer (0.2 → 1.0)

// Uniforms
uniform vec3 cameraPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;
uniform int useAO;

// ============================================================================
// PALETTE r/place 2023 (65 couleurs : 0 = air, 1-32 = r/place, 33-64 = terrain)
//
// Indices 1-32  : couleurs joueurs (palette r/place officielle)
// Indices 33-64 : nuances terrain (à définir, placeholder = copies pour l'instant)
// ============================================================================

const vec3 PALETTE[65] = vec3[65](
    vec3(0.000, 0.000, 0.000),  //  0: AIR (jamais rendu)
    // --- r/place 2023 (1-32) ---
    vec3(0.420, 0.004, 0.098),  //  1: Bordeaux      #6B0119
    vec3(0.741, 0.000, 0.216),  //  2: Rouge          #BD0037
    vec3(1.000, 0.271, 0.000),  //  3: Rouge-Orange   #FF4500
    vec3(0.996, 0.659, 0.000),  //  4: Orange          #FEA800
    vec3(1.000, 0.831, 0.208),  //  5: Jaune           #FFD435
    vec3(0.996, 0.973, 0.725),  //  6: Jaune pâle     #FEF8B9
    vec3(0.004, 0.635, 0.404),  //  7: Vert            #01A267
    vec3(0.035, 0.800, 0.463),  //  8: Vert clair      #09CC76
    vec3(0.494, 0.925, 0.341),  //  9: Vert lime       #7EEC57
    vec3(0.008, 0.459, 0.427),  // 10: Teal foncé     #02756D
    vec3(0.000, 0.616, 0.667),  // 11: Teal            #009DAA
    vec3(0.000, 0.800, 0.745),  // 12: Cyan            #00CCBE
    vec3(0.141, 0.310, 0.643),  // 13: Bleu foncé     #244FA4
    vec3(0.216, 0.565, 0.918),  // 14: Bleu            #3790EA
    vec3(0.322, 0.910, 0.953),  // 15: Bleu clair      #52E8F3
    vec3(0.282, 0.224, 0.749),  // 16: Indigo          #4839BF
    vec3(0.412, 0.357, 1.000),  // 17: Violet          #695BFF
    vec3(0.580, 0.702, 1.000),  // 18: Lavande         #94B3FF
    vec3(0.502, 0.114, 0.624),  // 19: Violet foncé   #801D9F
    vec3(0.706, 0.286, 0.749),  // 20: Magenta         #B449BF
    vec3(0.894, 0.671, 0.992),  // 21: Rose clair      #E4ABFD
    vec3(0.867, 0.067, 0.494),  // 22: Rose vif        #DD117E
    vec3(0.996, 0.216, 0.506),  // 23: Pink            #FE3781
    vec3(0.996, 0.600, 0.663),  // 24: Saumon          #FE99A9
    vec3(0.427, 0.275, 0.184),  // 25: Marron foncé   #6D462F
    vec3(0.608, 0.412, 0.149),  // 26: Marron          #9B6926
    vec3(0.996, 0.706, 0.439),  // 27: Pêche          #FEB470
    vec3(0.000, 0.000, 0.000),  // 28: Noir            #000000
    vec3(0.322, 0.322, 0.322),  // 29: Gris foncé     #525252
    vec3(0.533, 0.553, 0.565),  // 30: Gris            #888D90
    vec3(0.835, 0.839, 0.847),  // 31: Gris clair      #D5D6D8
    vec3(1.000, 1.000, 1.000),  // 32: Blanc           #FFFFFF
    // --- Terrain nuances (33-64) — gradient BetterSpades dirt_color_table ---
    vec3(0.314, 0.376, 0.314),  // 33: Herbe sombre    #506050
    vec3(0.376, 0.345, 0.282),  // 34: Herbe-terre     #605848
    vec3(0.439, 0.314, 0.251),  // 35: Terre claire    #705040
    vec3(0.502, 0.282, 0.220),  // 36: Terre           #804838
    vec3(0.439, 0.251, 0.188),  // 37: Terre-rouge     #704030
    vec3(0.376, 0.220, 0.157),  // 38: Terre foncée    #603828
    vec3(0.314, 0.188, 0.125),  // 39: Terre sombre    #503020
    vec3(0.251, 0.157, 0.094),  // 40: Très foncé      #402818
    vec3(0.188, 0.125, 0.063),  // 41: Fond             #302010
    vec3(0.200, 0.400, 0.200),  // 42: Herbe vive      #336633
    vec3(0.180, 0.350, 0.180),  // 43: Herbe moyenne   #2E592E
    vec3(0.160, 0.300, 0.160),  // 44: Herbe sombre 2  #294D29
    vec3(0.400, 0.400, 0.420),  // 45: Pierre claire   #66666B
    vec3(0.350, 0.350, 0.370),  // 46: Pierre          #59595E
    vec3(0.300, 0.300, 0.320),  // 47: Pierre sombre   #4D4D52
    vec3(0.580, 0.520, 0.380),  // 48: Sable clair     #948561
    vec3(0.520, 0.460, 0.340),  // 49: Sable           #857557
    vec3(0.460, 0.400, 0.300),  // 50: Sable foncé     #76664D
    vec3(0.700, 0.700, 0.700),  // 51: Neige sale      #B3B3B3
    vec3(0.900, 0.920, 0.940),  // 52: Neige           #E6EBF0
    vec3(0.180, 0.280, 0.180),  // 53: Feuillage       #2E472E
    vec3(0.220, 0.320, 0.120),  // 54: Mousse          #38521F
    vec3(0.150, 0.200, 0.350),  // 55: Eau profonde    #263359
    vec3(0.200, 0.350, 0.500),  // 56: Eau             #335980
    vec3(0.400, 0.250, 0.150),  // 57: Bois foncé      #664026
    vec3(0.550, 0.380, 0.220),  // 58: Bois            #8C6138
    vec3(0.650, 0.500, 0.300),  // 59: Bois clair      #A6804D
    vec3(0.600, 0.300, 0.200),  // 60: Brique          #994D33
    vec3(0.700, 0.350, 0.250),  // 61: Brique claire   #B35940
    vec3(0.250, 0.250, 0.300),  // 62: Ardoise         #40404D
    vec3(0.350, 0.200, 0.180),  // 63: Terre rouge     #59332E
    vec3(0.450, 0.420, 0.350)   // 64: Grès            #736B59
);

// ============================================================================
// FACE SHADING —  (ombre directionnelle simple)
//
// BetterSpades a un range plus serré (0.5-1.0) car il compte
// sur le sunblock + AO pour la profondeur. Nos anciens facteurs
// étaient plus contrastés car on n'avait pas le sunblock.
// ============================================================================

const float FACE_BRIGHTNESS[6] = float[6](
    1.000,  // 0: TOP     — plein soleil
    0.500,  // 1: BOTTOM  — très sombre
    0.875,  // 2: NORTH   — bien éclairé  (était 0.80)
    0.625,  // 3: SOUTH   — ombre          (était 0.70)
    0.750,  // 4: EAST    — ombre moyenne  (était 0.60)
    0.750   // 5: WEST    — ombre moyenne  (était 0.65)
);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // 1. Couleur depuis la palette
    vec3 color = PALETTE[vColorIndex];

    // 2. Variation de couleur par position (fondu)
    //    Hash pur par bloc — chaque bloc a une teinte unique subtile
    float h1 = fract(sin(dot(vWorldBlockPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
    float h2 = fract(sin(dot(vWorldBlockPos.xyz, vec3(93.9898, 67.345, 18.724))) * 28461.6271);
    float h3 = fract(sin(dot(vWorldBlockPos.xyz, vec3(27.1653, 14.892, 91.537))) * 61539.2847);
    color += vec3(h1, h2, h3) * 0.06;  // ~3% variation max

    // 3. Face shading (éclairage directionnel)
    color *= FACE_BRIGHTNESS[vFaceDir];

    // 4. Sunblock shade (ombre diagonale)
    if (vShade == 0)
        color *= 0.7;

    // 5. Ambient Occlusion (toggle via ImGui)
    if (useAO == 1)
        color *= vAO;

    // 6. Distance fog
    float dist = length(vFragPos - cameraPos);
    float fogFactor = 0 ; //clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
