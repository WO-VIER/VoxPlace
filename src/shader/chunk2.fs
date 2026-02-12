#version 460 core

out vec4 FragColor;

// Données du vertex shader
in flat int vColorIndex;
in flat int vFaceDir;
in vec3 vFragPos;

// Uniforms
uniform vec3 cameraPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;

// ============================================================================
// PALETTE r/place 2023 (33 couleurs : 0 = air, 1-32 = couleurs)
// ============================================================================

const vec3 PALETTE[33] = vec3[33](
    vec3(0.000, 0.000, 0.000),  //  0: AIR (jamais rendu)
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
    vec3(1.000, 1.000, 1.000)   // 32: Blanc           #FFFFFF
);

// ============================================================================
// FACE SHADING — luminosité par direction (donne du volume)
// ============================================================================

const float FACE_BRIGHTNESS[6] = float[6](
    1.00,  // 0: TOP     — plein soleil
    0.50,  // 1: BOTTOM  — très sombre
    0.80,  // 2: NORTH   — éclairé
    0.70,  // 3: SOUTH   — un peu sombre
    0.60,  // 4: EAST    — ombre
    0.65   // 5: WEST    — ombre légère
);

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // 1. Couleur depuis la palette
    vec3 color = PALETTE[vColorIndex];

    // 2. Face shading (éclairage directionnel simple)
    color *= FACE_BRIGHTNESS[vFaceDir];

    // 3. Distance fog
    float dist = length(vFragPos - cameraPos);
    //float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    //color = mix(color, fogColor, fogFactor);

    FragColor = vec4(color, 1.0);
}
