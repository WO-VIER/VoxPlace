# Notes OpenGL — VoxPlace

Version ciblée : OpenGL 3.3 (compatible Emscripten / WebGL2)

Ce document contient des notions essentielles à retenir pour débuter avec OpenGL et l'usage courant (buffers, pipeline de rendu, VBO/VAO/EBO). C'est un mémo destiné à garder une trace et à s'y référer rapidement.

## Sommaire
- [Principes de base](#principes-de-base)
- [Double buffering](#double-buffering)
- [Pipeline de rendu (étapes)](#pipeline-de-rendu-étapes)
- [Objets OpenGL importants](#objets-opengl-importants)
- [Liens utiles](#liens-utiles)
- [À retenir / Bonnes pratiques](#à-retenir--bonnes-pratiques)

---

## Principes de base
- OpenGL fonctionne en mode état-machine : on configure des états (ex : blend, cull, depth test), on lie des buffers et on envoie des commandes de dessin.
- Cibler OpenGL 3.3 (ou WebGL2 côté Emscripten) offre un ensemble moderne d'APIs (Programmable Pipeline — shaders obligatoires).

## Double buffering
- Le double buffering évite le flickering en dessinant sur un back buffer (off-screen) puis en swapant les buffers pour afficher l'image complète.
- `glfwSwapBuffers(window)` effectue le swap (GLFW), tu dois dessiner sur le back buffer avant de l'appeler.

## Pipeline de rendu (étapes)
Le pipeline graphique (simplifié) :
1. Vertex Shader (programmation par sommet — transform positions, calcule UV/normales)
2. Shape Assembly (assembly des primitives à partir des sommets)
3. Geometry Shader (optionnel : génère/transforme primitives)
4. Tests & blending (depth test, stencil test, blending)
5. Fragment Shader (programmation par fragment — calcule couleur, lighting)
6. Rasterisation (conversion des primitives en fragments/pixels)

> Remarque : Les étapes critiques pour la plupart des applications sont le vertex shader et le fragment shader : on y met la logique de transformation et d’éclairement.

## Objets OpenGL importants
- **VBO (Vertex Buffer Object)** — buffer mémoire sur GPU contenant les attributs (positions, normales, UV).
- **VAO (Vertex Array Object)** — décrit comment sont liés les VBO pour un objet : layout d'attributs, quel VBO lié à quel attribut.
- **EBO (Element Buffer Object)** — indices pour dessiner des primitives via `glDrawElements`.

```cpp
float vertices[] = {
     0.5f,  0.5f, 0.0f,  // top right           0
     0.5f, -0.5f, 0.0f,  // bottom right        1
    -0.5f, -0.5f, 0.0f,  // bottom left         2
    -0.5f,  0.5f, 0.0f   // top left            3
};
unsigned int indices[] = {  // Notons que l’on commence à 0!
    0, 1, 3,   // premier triangle
    1, 2, 3    // second triangle
};
```

![Diagramme : Vertex Array Objects](images/vertex_array_objects.png)
*Figure : Schéma montrant l’organisation d’un VAO (Vertex Array Object) et ses VBO/EBO associés.*

Code minimal pour configurer VBO/VAO :
```cpp
float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.0f,  0.5f, 0.0f
};
unsigned int VBO, VAO;
glGenVertexArrays(1, &VAO);
glGenBuffers(1, &VBO);
glBindVertexArray(VAO);
glBindBuffer(GL_ARRAY_BUFFER, VBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
glBindBuffer(GL_ARRAY_BUFFER, 0);
glBindVertexArray(0);
```

## Variable uniformes
Permettent de passer des données constantes aux shaders (ex : matrices de transformation, couleurs globales).

Du programme -> Shader 

```cpp
unsigned int shaderProgram = ...; // Programme shader compilé
glUseProgram(shaderProgram);
int uniformLocation = glGetUniformLocation(shaderProgram, "myUniform");
glUniform1f(uniformLocation, 0.5f); // Exemple pour float
```

## Vertex Pulling & SSBO
Technique avancée pour optimiser le rendu, très utilisée pour les moteurs de voxels.

- **Vertex Pulling** : Au lieu qu'OpenGL envoie automatiquement les données (via `glVertexAttribPointer`), c'est le Vertex Shader qui va "piocher" (pull) lui-même les données dans un buffer.
- **Pourquoi l'utiliser ?**
    - **Gain mémoire** : On peut compacter une face entière dans un seul `int` (bit packing). On ne stocke plus 4 ou 6 sommets complets, mais juste les infos de la face.
    - **Performance** : Moins de bande passante consommée entre le CPU et le GPU.

- **SSBO (Shader Storage Buffer Object)** : 
    - C'est un type de buffer très flexible (sorte de gros tableau de données).
    - Contrairement au VBO, on peut y accéder comme à une structure C++ dans le shader.
    - **Attention** : Nécessite **OpenGL 4.3** au minimum.

> **Note pour VoxPlace** : Si on veut rester en OpenGL 3.3 (ou WebGL2), on peut simuler ça avec un **TBO (Texture Buffer Object)** qui permet aussi de lire des données brutes, mais c'est un peu moins moderne que les SSBO.

```cpp
// Exemple de ce qu'on stocke (1 seul entier par face)
uint32_t faceData = (x << 20) | (y << 10) | z; 
```

```glsl
// Dans le shader (version 4.3)
layout(std430, binding = 0) readonly buffer myBuffer {
    uint data[]; // On pioche ici avec gl_VertexID
};
```


## Pipeline Technique : Vertex Pulling & SSBO (OpenGL 4.3+)

### 1. Concept : Inversion du contrôle
Au lieu d'envoyer des sommets pré-calculés (triangles), on envoie une liste compressée de **faces** (blocs). C'est le Vertex Shader qui génère les sommets à la volée.
- **Avant** : CPU calcule 4 sommets/face → envoie 100+ octets/face → GPU dessine.
- **Maintenant** : CPU envoie 1 entier (4 octets)/face → GPU décompresse et dessine.
- **Gain** : Réduction mémoire facteur x25.

### 2. Côté CPU (Packing)
On "écrase" toutes les infos d'une face dans un seul entier 32 bits (`uint32_t`).
Exemple pour un sub-chunk 32x32x32 :
- `X` (5 bits) : 0-31
- `Y` (5 bits) : 0-31
- `Z` (5 bits) : 0-31
- `Face` (3 bits) : 0-5 (Haut, Bas, N, S, E, O)
- `TextureID` (8 bits) : 0-255
- Reste : 6 bits libres (AO, lumière...)

```cpp
// Bit packing : On décale les bits pour tout caser
uint32_t packedData = x | (y << 5) | (z << 10) | (face << 15) | (texture << 18);
std::vector<uint32_t> ssboData;
ssboData.push_back(packedData);
```

### 3. Côté CPU (Envoi & Dessin)
On utilise un **SSBO** (Shader Storage Buffer Object) au lieu d'un VBO classique.
```cpp
// 1. Création et envoi (une fois ou à chaque update)
GLuint ssbo;
glGenBuffers(1, &ssbo);
glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
glBufferData(GL_SHADER_STORAGE_BUFFER, ssboData.size() * sizeof(uint32_t), ssboData.data(), GL_STATIC_DRAW);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo); // Binding = 0

// 2. Dessin (à chaque frame)
// On dessine "dans le vide", sans VBO lié au VAO (il faut quand même un VAO dummy bindé).
// 1 face = 6 sommets (2 triangles)
glDrawArrays(GL_TRIANGLES, 0, nbFaces * 6);
```

### 4. Côté GPU (Vertex Shader)
Le shader s'exécute pour chaque sommet. Il utilise `gl_VertexID` pour savoir quelle face et quel coin du triangle il doit traiter.

```glsl
#version 430 core

// Récupération du SSBO (binding = 0)
layout(std430, binding = 0) readonly buffer ChunkData {
    uint faces[];
};

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkPos;

// Tables pour générer les sommets d'un cube sans if/else
const vec3 cubeVerts[8] = vec3[8](...); // Les 8 coins (0,0,0), (1,0,0)...
const int quadIndices[6] = int[6](0, 1, 2, 0, 2, 3); // Ordre pour faire 2 triangles

void main() {
    // A. Identification
    int faceID = gl_VertexID / 6;   // Index de la face dans le tableau SSBO
    int vertID = gl_VertexID % 6;   // Index du sommet (0..5) pour cette face

    // B. Récupération & Unpacking
    uint data = faces[faceID];
    
    int x = int(data & 31u);
    int y = int((data >> 5u) & 31u);
    int z = int((data >> 10u) & 31u);
    int faceDir = int((data >> 15u) & 7u);
    
    // C. Génération de la position
    vec3 localPos = vec3(x, y, z) + offsetSelonFace(faceDir, quadIndices[vertID]);
    
    gl_Position = projection * view * vec4(chunkPos + localPos, 1.0);
}
```


## Stratégies d'Optimisation pour Moteur Voxel (Stack C++ / OpenGL 4.3+)

### 1. Greedy Meshing : Fausse Bonne Idée ?
Le **Greedy Meshing** consiste à fusionner les faces adjacentes identiques en un seul grand rectangle (quad) pour réduire le nombre de triangles.
- **Verdict** : Souvent **contre-productif** pour un monde destructible/multijoueur temps réel avec cette stack.
- **Pourquoi ?**
    - **Coût CPU** : Algorithme lourd à recalculer à chaque modification de bloc (lag potentiel).
    - **Complexité Shader** : Gérer les UVs de textures répétées sur un grand quad complique le shader.
    - **Gain négligeable** : Avec le *Vertex Pulling* et les *SSBO*, les sommets pèsent très peu (4 octets). Le GPU est plus rapide à afficher beaucoup de petits triangles simples qu'à attendre un algorithme CPU complexe.
- **Meilleure approche** : **Simple Face Culling** (ne pas dessiner les faces cachées par un voisin). C'est ultra-rapide et suffit largement.

### 2. Le "Big Three" des Optimisations Modernes

#### A. Multi-Draw Indirect (MDI) — *L'arme atomique*
- **Problème** : Faire 1000 appels `glDrawArrays` (un par chunk) tue les performances CPU (overhead du driver).
- **Solution** : Mettre toutes les commandes de dessin dans un buffer et tout dessiner en **1 seul appel** (`glMultiDrawArraysIndirect`).
- **Impact** : Réduit drastiquement l'usage CPU.

#### B. Frustum Culling (CPU)
- Ne pas envoyer au GPU les chunks qui sont derrière la caméra ou hors du champ de vision.
- Test simple boîte englobante (AABB) vs Frustum caméra.

#### C. Gestion des Chunks (Threading & Dirty Flags)
- **Dirty Flags** : Ne jamais recalculer un mesh si le chunk n'a pas changé (`bool needsUpdate`).
- **Threading** : La génération des meshs (parcours du tableau `blocks`) doit se faire dans un **thread séparé**, pas dans la boucle de rendu principale, pour éviter de bloquer l'affichage.

## Liens utiles
- Voxel Wiki (Bible du voxel) : https://voxel.wiki/
- Article Vertex Pulling : https://voxel.wiki/wiki/vertex-pulling/
- Vidéo Explicative (SSBO/Geometry) : https://www.youtube.com/watch?v=IoS5opco9LA&t=1s
- Tutoriels & diagrammes : https://opengl.developpez.com/tutoriels/apprendre-opengl/
- Pipeline diagram : https://opengl.developpez.com/tutoriels/apprendre-opengl/images/pipeline.png
- Cheat sheet : https://stackoverflow.com/questions/2772570/opengl-cheat-sheet
- Cheat sheet (repo) : https://github.com/henkeldi/opengl_cheatsheet
- Shadertoy (exemples shader) : https://www.shadertoy.com/

## À retenir / Bonnes pratiques
- Toujours vérifier l’init : `glfwInit()` puis `gladLoadGLLoader()` (ou loader adapté).
- Utiliser `glfwWindowHint` pour demander une version de contexte compatible (e.g., 3.3)
- Exclure `glad.c` pour les builds WebAssembly si le loader est fourni par la chaîne Emscripten.
- Pour WASM : préférer WebGL2 et utiliser `emscripten_set_main_loop` au lieu d’une boucle `while` classique.
- Toujours vérifier erreurs de compilation/linkage des shaders (`glGetShaderiv`/`glGetProgramiv`) et récupérer `glGetShaderInfoLog`/`glGetProgramInfoLog` en cas d’échec.

---

Dernière mise à jour : (mettre la date) *8 décembre 2025*

