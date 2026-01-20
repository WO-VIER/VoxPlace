#version 330 core // attribut d'entrée par sommet
layout (location = 0) in vec3 aPos; // (x, y, z ) (12 bytes)
layout (location = 1) in vec3 aColor; // (r, g, b) (12 bytes)
layout (location = 2) in vec2 aTexCoord; // vec 2d for 2d textures (s, t) (x, y) (8 bytes) = 32 bytes

out vec3 ourColor; //en sortie du vertex shader
out vec2 TexCoord; //

uniform mat4 transform; // matrice de transformation 
void main()
{
    gl_Position = transform * vec4(aPos, 1.0); // passer de coord pixel aux coord ecran
    ourColor = aColor;
	TexCoord = vec2(aTexCoord.x, aTexCoord.y);
}


/*
#version 330 core : version GLSL (OpenGL 3.3) utilisée.
layout (location = N) in ... : déclarations des attributs d'entrée (par sommet) :
aPos (location 0) : position vec3 (x,y,z).
aColor (location 1) : couleur vec3 (r,g,b).
aTexCoord (location 2) : coordonnées de texture vec2 (s,t).
out ... : variables envoyées au fragment shader :
ourColor : transmet la couleur.
TexCoord : transmet les coordonnées de texture.
main() :
gl_Position = vec4(aPos, 1.0); place le sommet en espace clip sans appliquer de transformation (pas de matrices model/view/projection).
les attributs aColor et aTexCoord sont passés directement aux sorties pour le fragment shader.
Remarque : les commentaires indiquant les tailles (12/8 bytes) sont informatifs. Si vous voulez des transformations (caméra, modèles), multipliez vec4(aPos,1.0) par des matrices MVP.
*/