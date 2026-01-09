#version 330 core
out vec4 FragColor; //Variable de sortie fu fs (couleutr final du pixel)

in vec3 ourColor; //Entrée venant du vs  (doivent avoir les mêmes noms/types que les out du vs)
in vec2 TexCoord;

uniform sampler2D ourTexture; //Accès à la texture 2D fournie par l'application

void main()
{
    FragColor = texture(ourTexture, TexCoord); // Echantillonne la texture au coord TexCoord généralement dans [0,1] et écrit la couleur RGBA résultante dans la sortie.
}

/*
#version 330 core : GLSL OpenGL 3.3.
out vec4 FragColor; : variable de sortie du fragment shader (couleur finale du pixel).
in vec3 ourColor; et in vec2 TexCoord; : entrées venant du vertex shader (doivent avoir les mêmes noms/types que les out du vertex). Ici ourColor est déclaré mais non utilisé (peut déclencher un warning).
uniform sampler2D ourTexture; : accès à la texture 2D fournie par l'application.
FragColor = texture(ourTexture, TexCoord); : échantillonne la texture aux coordonnées TexCoord (généralement dans [0,1]) et écrit la couleur RGBA résultante dans la sortie.
Remarques pratiques :

Pour combiner couleur et texture : FragColor = texture(ourTexture, TexCoord) * vec4(ourColor, 1.0);.
Attention à l’orientation des coordonnées (image loaders peuvent inverser Y) et au comportement d’échantillonnage en dehors de [0,1] (wrapping).
Pas de correction gamma, d’éclairage ni d’alpha testing ici — c’est un shader de texturing très simple.
*/