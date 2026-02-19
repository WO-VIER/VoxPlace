# Notes Techniques — VoxPlace

> **Version ciblée :** OpenGL 4.6 Core (`#version 460 core`)
> Ce document rassemble toutes les notions essentielles du projet : bases OpenGL, pipeline de rendu voxel, optimisations GPU/CPU, architecture réseau, et décisions de design.

**Commande Valgrind :**
```bash
valgrind -s --leak-check=full --show-leak-kinds=all --track-origins=yes ./build/VoxPlace > valgrind.txt 2>&1
```

---

## Sommaire

1. [Bases OpenGL](#1-bases-opengl)
2. [Pipeline Technique : Vertex Pulling & SSBO](#2-pipeline-technique--vertex-pulling--ssbo)
3. [État Actuel de l'Implémentation](#3-état-actuel-de-limplémentation)
4. [Optimisations GPU (Pipeline de Rendu)](#4-optimisations-gpu-pipeline-de-rendu)
5. [Optimisations CPU (Types, Cache & Mémoire)](#5-optimisations-cpu-types-cache--mémoire)
6. [Multithreading (CPU)](#6-multithreading-cpu)
7. [Architecture Serveur & Réseau](#7-architecture-serveur--réseau)
8. [Architecture VoxPlace : Lazy Generation](#8-architecture-voxplace--lazy-generation)
9. [Rendu Dynamique & UX des Bordures](#9-rendu-dynamique--ux-des-bordures)
10. [Les "Fausses" Bonnes Idées (À Éviter)](#10-les-fausses-bonnes-idées-à-éviter)
11. [To-Do](#11-to-do)
12. [Liens Utiles](#12-liens-utiles)
13. [Bonnes Pratiques OpenGL](#13-bonnes-pratiques-opengl)

---

## 1. Bases OpenGL

### Principes de base
- OpenGL fonctionne en mode **état-machine** : on configure des états (blend, cull, depth test), on lie des buffers et on envoie des commandes de dessin.
- Le **Programmable Pipeline** (shaders obligatoires) est le standard depuis OpenGL 3.3+.
- VoxPlace cible **OpenGL 4.6 Core** (requis pour les SSBO, `#version 460 core` dans les shaders).

### Double Buffering
- Évite le flickering en dessinant sur un **back buffer** (off-screen), puis en swapant les buffers pour afficher l'image complète.
- `glfwSwapBuffers(window)` effectue le swap.

### Pipeline de Rendu (Simplifié)

| Étape | Rôle |
|-------|------|
| 1. **Vertex Shader** | Transforme les positions, calcule UV/normales |
| 2. **Shape Assembly** | Assemble les primitives à partir des sommets |
| 3. **Geometry Shader** | *(Optionnel)* Génère/transforme des primitives |
| 4. **Rasterisation** | Convertit les primitives en fragments/pixels |
| 5. **Fragment Shader** | Calcule la couleur finale, lighting |
| 6. **Tests & Blending** | Depth test, stencil test, blending |

> Les deux étapes critiques sont le **Vertex Shader** et le **Fragment Shader**.

### Objets OpenGL

| Objet | Rôle |
|-------|------|
| **VBO** (Vertex Buffer Object) | Buffer mémoire GPU contenant les attributs (positions, normales, UV) |
| **VAO** (Vertex Array Object) | Décrit le layout des attributs et lie les VBO |
| **EBO** (Element Buffer Object) | Indices pour dessiner via `glDrawElements` |
| **SSBO** (Shader Storage Buffer) | Buffer flexible, accessible comme un tableau C++ dans le shader *(OpenGL 4.3+)* — **utilisé par VoxPlace** |

```cpp
// Exemple : VBO + VAO minimal
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

```cpp
// Exemple : EBO (Index Buffer)
float vertices[] = {
     0.5f,  0.5f, 0.0f,   // 0 : top right
     0.5f, -0.5f, 0.0f,   // 1 : bottom right
    -0.5f, -0.5f, 0.0f,   // 2 : bottom left
    -0.5f,  0.5f, 0.0f    // 3 : top left
};
unsigned int indices[] = {
    0, 1, 3,   // premier triangle
    1, 2, 3    // second triangle
};
```

![Diagramme : Vertex Array Objects](images/vertex_array_objects.png)
*Figure : Organisation d'un VAO et ses VBO/EBO associés.*

### Variables Uniformes

Passage de données constantes du programme C++ vers les shaders.

```cpp
unsigned int shaderProgram = ...; // Programme shader compilé
glUseProgram(shaderProgram);
int uniformLocation = glGetUniformLocation(shaderProgram, "myUniform");
glUniform1f(uniformLocation, 0.5f);
```

---

## 2. Pipeline Technique : Vertex Pulling & SSBO

### Concept : Inversion du Contrôle

Le **Vertex Pulling** lit directement le SSBO via `gl_VertexID`. On ne passe pas de VBO, pas de Vertex Attributes. Le shader **"tire" (pull)** les données du SSBO lui-même.

| | Pipeline Classique (`Chunk.h`) | Vertex Pulling (`Chunk2.h`) |
|---|---|---|
| **Données envoyées** | 4 sommets/face → ~104 octets | 1 `uint32_t`/face → **4 octets** |
| **Réduction mémoire** | — | **×25** |
| **Qui génère les sommets ?** | Le CPU | Le GPU (Vertex Shader) |

> **Note :** VoxPlace conserve `Chunk.h` (ancienne implémentation avec VBO classique) et `Chunk2.h` (implémentation actuelle avec Vertex Pulling + SSBO). Le pipeline actif est `Chunk2`.

### Côté CPU : Bit Packing (Layout réel de `Chunk2.h`)

On compresse toutes les infos d'une face dans un seul `uint32_t` :

| Champ | Bits | Masque | Plage | Shift |
|-------|------|--------|-------|-------|
| X local | `[0–3]` — 4 bits | `0xF` | 0–15 | `<< 0` |
| Y local | `[4–11]` — 8 bits | `0xFF` | 0–255 | `<< 4` |
| Z local | `[12–15]` — 4 bits | `0xF` | 0–15 | `<< 12` |
| Face Direction | `[16–18]` — 3 bits | `0x7` | 0–5 | `<< 16` |
| Color Index | `[19–24]` — 6 bits | `0x3F` | 0–63 | `<< 19` |
| AO (réservé) | `[25–26]` — 2 bits | — | 0–3 | `<< 25` |
| **Libre** | `[27–31]` — 5 bits | — | — | — |

**Total utilisé : 27/32 bits** (5 bits libres pour extensions futures : lumière, metadata...)

```cpp
// Bit packing réel (Chunk2.h, meshGenerate)
uint32_t packed = x | (y << 4) | (z << 12) | (faceDir << 16) | (colorIndex << 19);
```

> **Performance `std::vector`** : Sans `.reserve()`, chaque `push_back` qui dépasse la capacité déclenche une réallocation : `malloc()` → `memcpy()` → `free()`. Toujours appeler `.reserve(estimatedSize)` avant de remplir le vecteur. La ligne `packedFaces.reserve(...)` est actuellement **commentée** dans le code.

### Côté CPU : Envoi & Dessin (extrait de `Chunk2.h`)

```cpp
// uploadMesh() — Crée/recrée le SSBO
glGenBuffers(1, &ssbo);
glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
glBufferData(GL_SHADER_STORAGE_BUFFER,
             faces.size() * sizeof(uint32_t),
             faces.data(), GL_STATIC_DRAW);

// render() — Dessin chaque frame
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo); // Binding = 0
glBindVertexArray(vao);  // VAO dummy (obligatoire mais vide)
glDrawArrays(GL_TRIANGLES, 0, faceCount * 6);  // 1 face = 6 sommets (2 triangles)
```

### Côté GPU : Vertex Shader (`chunk2.vs`)

```glsl
#version 460 core

layout(std430, binding = 0) readonly buffer ChunkData {
    uint faces[];
};

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkPos;

out flat int vColorIndex;
out flat int vFaceDir;
out vec3 vFragPos;

// Lookup tables (pas de if/else)
const int QUAD_INDICES[6] = int[6](0, 1, 2, 0, 2, 3);

const vec3 FACE_OFFSETS[24] = vec3[24](
    // Face 0: TOP (+Y)
    vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(0, 1, 1),
    // Face 1: BOTTOM (-Y)
    vec3(0, 0, 1), vec3(1, 0, 1), vec3(1, 0, 0), vec3(0, 0, 0),
    // Face 2: NORTH (+Z)
    vec3(0, 0, 1), vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 0, 1),
    // Face 3: SOUTH (-Z)
    vec3(1, 0, 0), vec3(1, 1, 0), vec3(0, 1, 0), vec3(0, 0, 0),
    // Face 4: EAST (+X)
    vec3(1, 0, 1), vec3(1, 1, 1), vec3(1, 1, 0), vec3(1, 0, 0),
    // Face 5: WEST (-X)
    vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 1, 1), vec3(0, 0, 1)
);

void main() {
    int faceID = gl_VertexID / 6;
    int vertID = gl_VertexID % 6;

    uint data = faces[faceID];

    int x        = int(data & 0xFu);           // Bits [0-3]
    int y        = int((data >> 4u) & 0xFFu);   // Bits [4-11]
    int z        = int((data >> 12u) & 0xFu);   // Bits [12-15]
    int faceDir  = int((data >> 16u) & 0x7u);   // Bits [16-18]
    int colorIdx = int((data >> 19u) & 0x3Fu);  // Bits [19-24]

    int cornerIdx = QUAD_INDICES[vertID];
    vec3 offset = FACE_OFFSETS[faceDir * 4 + cornerIdx];
    vec3 localPos = vec3(float(x), float(y), float(z)) + offset;
    vec3 worldPos = chunkPos + localPos;

    gl_Position = projection * view * vec4(worldPos, 1.0);
    vColorIndex = colorIdx;
    vFaceDir = faceDir;
    vFragPos = worldPos;
}
```

### Côté GPU : Fragment Shader (`chunk2.fs`)

Le fragment shader utilise :
- **Palette r/place 2023** : 33 couleurs (index 0 = air, jamais rendu ; 1–32 = couleurs).
- **Face Brightness** : Luminosité par direction de face pour donner du volume sans vrai calcul d'éclairage.
- **Distance Fog** : Uniforms présents (`fogStart`, `fogEnd`, `fogColor`, `cameraPos`) mais **actuellement désactivés** (commentés).

```glsl
// Face shading — luminosité différente par direction
const float FACE_BRIGHTNESS[6] = float[6](
    1.00,  // 0: TOP     — plein soleil
    0.50,  // 1: BOTTOM  — très sombre
    0.80,  // 2: NORTH   — éclairé
    0.70,  // 3: SOUTH   — un peu sombre
    0.60,  // 4: EAST    — ombre
    0.65   // 5: WEST    — ombre légère
);

void main() {
    vec3 color = PALETTE[vColorIndex];
    color *= FACE_BRIGHTNESS[vFaceDir];
    // fog désactivé pour l'instant
    FragColor = vec4(color, 1.0);
}
```

### Vertex Pulling vs MDI : Clarification

Ces deux concepts sont complémentaires mais distincts :

| Concept | Rôle |
|---------|------|
| **Vertex Pulling** | *Comment* le shader lit les données (depuis le SSBO via `gl_VertexID`) |
| **MDI** (Multi-Draw Indirect) | *Comment* le CPU dit au GPU de dessiner (1 appel au lieu de N) |

#### Sans MDI — État actuel (`main3.cpp`)
```
CPU (render loop)                          GPU
┌─────────────────────────────┐
│ for (auto& [key, chunk] :   │
│           chunkMap)         │
│   setVec3("chunkPos", ...)  │──→ draw chunk 0
│   chunk->render()           │──→ draw chunk 1
│   ...                       │──→ draw chunk 399
└─────────────────────────────┘

= 400 × glBindBufferBase + 400 × glDrawArrays
= le CPU parle au GPU 800 fois par frame
```

#### Avec MDI (objectif futur)
```
CPU (render loop)                          GPU
┌─────────────────────────────┐
│ 1 seul SSBO géant avec      │
│ TOUTES les faces            │
│                             │
│ glMultiDrawArraysIndirect() │──→ draw TOUT en 1 appel
└─────────────────────────────┘

= 1 appel par frame
```

---

## 3. État Actuel de l'Implémentation

### Fichiers Clés

| Fichier | Rôle |
|---------|------|
| `include/Chunk2.h` | Classe chunk active : stockage `uint8_t blocks[16][256][16]`, SSBO, Vertex Pulling, face culling inter-chunk |
| `include/Chunk.h` | Ancienne classe chunk (VBO classique, palette 16 couleurs, ~104 octets/face). **Non utilisée.** |
| `include/TerrainGenerator.h` | Génération procédurale : 2 couches Simplex Noise (continent + détail) via FastNoiseLite |
| `include/LowResRenderer.h` | Pipeline de rendu rétro/PS1 : FBO 640×360 → upscale `GL_NEAREST` → 1920×1080 |
| `include/TextureAtlas.h` | Atlas de textures (non utilisé pour l'instant, palette couleurs uniquement) |
| `src/main3.cpp` | Point d'entrée : init GLFW/GLAD, boucle de rendu, hashmap de chunks |
| `src/shader/chunk2.vs` | Vertex Shader : Vertex Pulling + unpacking bits |
| `src/shader/chunk2.fs` | Fragment Shader : palette 33 couleurs r/place + face shading |

### Constantes du Chunk (`Chunk2.h`)

```cpp
constexpr uint8_t  CHUNK_SIZE_X = 16;   // Largeur
constexpr uint16_t CHUNK_SIZE_Y = 256;  // Hauteur (0 = bedrock couche)
constexpr uint8_t  CHUNK_SIZE_Z = 16;   // Profondeur
constexpr uint8_t  BEDROCK_LAYER = 0;   // Y = 0 incassable
```

- **Taille mémoire RAM** : `16 × 256 × 16 × 1 octet = 65 536 octets = 64 Ko` par chunk
- **Taille mémoire VRAM** : `faceCount × 4 octets` (1 `uint32_t` par face dans le SSBO)
- Constructeur initialise `blocks` à 0 (air) + couche bedrock (couleur 29, gris foncé) en `y = 0`

### Hashmap de Chunks (`main3.cpp`)

Le monde utilise une `unordered_map<int64_t, Chunk2*>` avec une clé calculée par bit shift :

```cpp
// Clé unique : les 32 bits hauts = cx, les 32 bits bas = cz
inline int64_t chunkKey(int cx, int cz) {
    return ((int64_t)cx << 32) | ((int64_t)cz & 0xFFFFFFFF);
}

// Recherche O(1)
inline Chunk2* getChunkAt(int cx, int cz) {
    auto it = chunkMap.find(chunkKey(cx, cz));
    return (it != chunkMap.end()) ? it->second : nullptr;
}
```

> **Note :** C'est plus simple qu'un `ChunkCoord` struct + hash custom (décrit dans la section Architecture). L'implémentation actuelle est un hash implicite par bit shift.

### Grille de Chunks

Le code actuel génère une grille fixe de **20×20 = 400 chunks** (`cx` de -10 à 9, `cz` de -10 à 9). La génération se fait en deux passes :
1. **Création + terrain** : Tous les chunks sont créés et remplis via `TerrainGenerator::fillChunk()`.
2. **Mesh** : Les meshes sont générés APRÈS, pour que les voisins existent et que le face culling inter-chunk fonctionne.

### Terrain Generator (`TerrainGenerator.h`)

Deux couches de bruit Simplex combinées :

```
Hauteur(x,z) = baseHeight + continent(x,z) × continentAmp + detail(x,z) × detailAmp
```

| Paramètre | Valeur | Rôle |
|-----------|--------|------|
| `baseHeight` | 20 | Hauteur de base du sol |
| `continentAmp` | 15.0f | Amplitude grandes collines (±15 blocs) |
| `detailAmp` | 4.0f | Amplitude micro-variations (±4 blocs) |
| continent freq | 0.005 | Basse fréquence = formes larges |
| detail freq | 0.02 | Haute fréquence = détails fins |

**Couches de blocs** (de bas en haut) :
| Y | Bloc | Couleur |
|---|------|---------|
| 0 | Bedrock | 28 (noir) ou 29 (gris foncé, constructeur) |
| 1 à height-3 | Pierre | 29 (gris foncé) |
| height-2 à height-1 | Terre | 26 (marron) |
| height | Herbe (surface) | Random 1–32 (sauf 28) |

> **Benchmark** : `fillChunkBench()` génère un **damier 3D** (chaque bloc alterne entre solide et air selon `(x+y+z) % 2 == 0`). Utile pour tester le pire cas de face count (toutes les faces visibles).

### LowRes Renderer (Pipeline PS1/Rétro)

Le projet utilise un **framebuffer basse résolution** pour un style pixelisé :

```
Scène 3D → Rendu dans FBO 640×360 → Upscale GL_NEAREST → Écran 1920×1080
```

| Paramètre | Valeur |
|-----------|--------|
| `RENDER_WIDTH` | 640 |
| `RENDER_HEIGHT` | 360 |
| Upscale filter | `GL_NEAREST` (pas d'antialiasing, pixels nets) |
| Écran cible | 1920×1080 |

Le shader d'upscale est un simple quad fullscreen avec `texture(screenTexture, TexCoords)`.

### Face Culling Inter-Chunks

`Chunk2::getBlockOrNeighbor()` vérifie les blocs aux bordures en interrogeant le chunk voisin :

```
Chunk West     This Chunk      Chunk East
┌────────┐   ┌────────────┐   ┌────────┐
│        │   │ x=0 ... 15 │   │        │
│  [15]  │←──│  getBlock   │──→│  [0]   │
│        │   │  (-1,y,z)   │   │(16,y,z)│
└────────┘   └────────────┘   └────────┘
```

Si aucun voisin n'existe (`nullptr`) → retourne 0 (air) → la face est créée (le bord du monde est visible).

### Configuration Fog (désactivée)

```cpp
// main3.cpp — actuellement à 0 (désactivé)
const float FOG_START = 0.0f;
const float FOG_END = 0.0f;
const glm::vec3 FOG_COLOR = glm::vec3(0.6f, 0.7f, 0.9f); // Bleu ciel
```

Le fog linéaire est implémenté dans `chunk2.fs` mais les lignes de calcul sont **commentées**. La `glClearColor` correspond au `FOG_COLOR` pour un fondu seamless quand le fog sera activé.

---

## 4. Optimisations GPU (Pipeline de Rendu)

### Face Culling Avancé
- **Blocs standards :** Ne jamais rendre les faces partagées entre deux blocs opaques adjacents. ✅ *Implémenté dans `meshGenerate()`.*
- **Blocs Custom (Blockbench) :** Si une face touche les limites de la bounding box et qu'un bloc plein est collé contre, la face peut être cullée. *(Futur)*
- **Bordures de Chunk :** Si le chunk voisin est `nullptr`, la face est créée (air). ✅ *Implémenté via `getBlockOrNeighbor()`.*

### Z-Prepass (Depth Prepass)

Réduit l'**Overdraw** (calculs inutiles de pixels cachés) à zéro. Le Fragment Shader est la partie la **plus coûteuse** du rendu ; le Z-Prepass garantit qu'il ne s'exécute que pour les pixels réellement visibles.

**Principe :** On rend toute la géométrie sans couleur (juste le Vertex Shader) pour remplir le Depth Buffer. Puis on re-rend en ne colorant que les pixels dont la profondeur correspond exactement — le GPU saute automatiquement le Fragment Shader pour les fragments qui échouent au test de profondeur.

| Passe | Configuration | Action |
|-------|--------------|--------|
| **1. Profondeur** | `glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE)` | Remplir le Depth Buffer (vertex shader uniquement) |
| **2. Couleur** | `glColorMask(GL_TRUE, ...)` + `glDepthFunc(GL_EQUAL)` | Le Fragment Shader ne calcule QUE les pixels au premier plan |

> ⚠️ **Piège :** Le Z-Prepass exécute le Vertex Shader **deux fois**. Si la géométrie est massive (ex: damier 3D via `fillChunkBench`) et le Fragment Shader très simple, c'est une **perte** de FPS. À tester via un toggle clavier (`F3`).
> ⚠️ **Attention :** Gérer séparément la géométrie transparente.

---

## 5. Optimisations CPU (Types, Cache & Mémoire)

> Piège classique : confondre optimisation du **stockage** (RAM/Cache) et optimisation du **calcul** (CPU/Registres).

### Pourquoi `int` pour les boucles ?

Les processeurs modernes manipulent nativement des mots de **32 ou 64 bits**. C'est la taille de leurs registres.

- **Performance :** Utiliser `uint8_t` dans un `for` force le CPU à effectuer des opérations de masquage supplémentaires à chaque itération pour simuler un comportement 8 bits.
- **Risque d'Overflow :** `for (uint8_t y = 0; y < 256; y++)` → **boucle infinie** ! `y` passe de 255 à 0 (overflow), donc `y < 256` est toujours vrai.

**→ Toujours utiliser `int` pour les compteurs de boucle.** C'est la taille naturelle du registre CPU : rapide et sans risque d'overflow.

> ✅ **VoxPlace le fait correctement** : toutes les boucles dans `Chunk2.h`, `TerrainGenerator.h` utilisent `int` pour les itérateurs `x`, `y`, `z`.

### Pourquoi `uint8_t` pour le stockage ?

Le **cache CPU** (L1, L2) est ultra-rapide mais très petit. Plus les données sont compactes, plus on en charge d'un coup.

| Type de bloc | Taille d'un chunk `[16][256][16]` | Tient en cache ? |
|---|---|---|
| `uint8_t` (1 octet) | **64 Ko** | ✅ Cache L1 ou L2 |
| `int` (4 octets) | **256 Ko** | ❌ Allers-retours RAM |

> ✅ **VoxPlace** : `uint8_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z]` → 64 Ko pile.

### Gestion de la hauteur maximale (Y = 255)

| Option | Description |
|--------|-------------|
| **A — Toit de verre** | Construire jusqu'à 255, ignorer au-dessus. Le moteur physique tolère le joueur "hors tableau". |
| **B — Faux plafond** | Bloquer à 254 (`if (y >= 254) return false;`), air garanti au-dessus. |

> État actuel : `setBlock()` vérifie `y < CHUNK_SIZE_Y` (256), donc on peut placer un bloc en `y = 255`. `getBlock()` retourne 0 (air) pour `y >= 256`. Approche **A** de facto.

### Résumé : Registres vs Cache

| Contexte | Type idéal | Raison |
|----------|-----------|--------|
| **Calcul / Boucles** (Registres CPU) | `int` / `int64_t` | Taille native du registre, pas de masquage |
| **Stockage / Données** (Cache CPU) | `uint8_t` | Maximise la densité en cache, réduit les accès RAM |

> **Le pattern idéal : Stockage compact (`uint8_t`) + Calcul large (`int`).**

---

## 6. Multithreading (CPU)

La pire erreur est de mettre des `mutex` (verrous) partout entre le thread principal et les workers. Cela force les threads à s'attendre, annulant le gain de performance.

### Modèle "Thread Pool" Isolé (Data-Oriented Design)

```
Main Thread                    Thread Pool                    GPU
    │                              │                            │
    │  1. Emballe les données      │                            │
    │─────────────────────────────→│                            │
    │                              │  2. Calcul en isolation    │
    │                              │     (Perlin, Meshing...)   │
    │                              │                            │
    │  3. Récupère le résultat    ←│                            │
    │                              │                            │
    │  4. Upload OpenGL ──────────────────────────────────────→│
    │     (main thread obligatoire)│                            │
```

**Règles :**
1. Le **Main Thread** rassemble toutes les données nécessaires à une tâche.
2. Il envoie ces données formatées à un **Worker** du pool.
3. Le **Worker** travaille en **isolation totale** (ne demande jamais d'infos au Main Thread).
4. Il place le résultat dans un buffer de sortie.
5. Le **Main Thread** récupère et upload au GPU (OpenGL exige le main thread).

> ⚠️ **État actuel :** VoxPlace ne fait **pas** de multithreading. Tout (génération terrain, meshing, upload) se fait sur le main thread, de façon synchrone au démarrage.

---

## 7. Architecture Serveur & Réseau

Le serveur doit être pensé comme un OS gérant des ressources limitées : **budget de 50ms** par boucle pour tenir les **20 Ticks Par Seconde (TPS)**.

### Time Slicing & Throttling

| Ressource | Limite | Raison |
|-----------|--------|--------|
| **Génération de chunks** | 1 chunk / tick | Évite de freeze la logique physique/réseau |
| **Envoi réseau** | 5 chunks / joueur / tick | Évite la saturation des buffers TCP/UDP (→ timeout) |

### Multithreading Spatial (Régions)
- Regrouper les chunks en zones isolées ("Régions").
- Si Joueur A et Joueur B sont très éloignés (régions disjointes), on peut assigner chaque région à un thread séparé → physique et réseau en parallèle, **zéro mutex**.

### Simulation Distance vs Render Distance

| Paramètre | Géré par | Exemple | Rôle |
|-----------|----------|---------|------|
| **Render Distance** | Client | 64 chunks | Affichage visuel uniquement |
| **Simulation Distance** | Serveur | 6 chunks | Physique, Random Tick Updates (pousse, fluides) |

> Ne **jamais** appliquer les Random Tick Updates sur toute la Render Distance. Les chunks éloignés dorment en RAM.

---

## 8. Architecture VoxPlace : Lazy Generation

**Concept Core :** Moteur Client-Serveur où la génération procédurale (Perlin Noise) est pilotée dynamiquement par l'exploration et le placement de blocs par les joueurs. Pas de structures prédéfinies.

### Spatial Hashing (Table de Hachage)

Le monde n'alloue de la mémoire **QUE** pour les chunks qui existent. Recherche en `O(1)`.

**Méthode recommandée (avec struct custom) :**
```cpp
struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const {
        return x == o.x && z == o.z;
    }
};

struct ChunkHash {
    std::size_t operator()(const ChunkCoord& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.z) << 1);
    }
};

std::unordered_map<ChunkCoord, Chunk2*, ChunkHash> activeChunks;
```

**Méthode actuelle (implémentation simplifiée dans `main3.cpp`) :**
```cpp
// Clé int64_t par bit shift — plus simple, pas besoin de struct/hash
std::unordered_map<int64_t, Chunk2*> chunkMap;

inline int64_t chunkKey(int cx, int cz) {
    return ((int64_t)cx << 32) | ((int64_t)cz & 0xFFFFFFFF);
}
```

### Trigger de Génération : `setBlock`
- Le déclencheur n'est **plus** le déplacement, mais la **modification du monde**.
- Si un joueur pose un bloc en limite de chunk (ex: `x == 15`), le serveur vérifie la `unordered_map`. Si le chunk voisin n'existe pas, il est instancié **asynchroneusement** via le Perlin Noise.
- **Gestion du Vide :** Les chunks non générés sont traités comme des **murs invisibles** (Solid Colliders) pour empêcher les joueurs de tomber dans le vide.

> ⚠️ **État actuel :** Pas encore de lazy generation. Tous les 400 chunks sont pré-générés au démarrage de façon synchrone.

---

## 9. Rendu Dynamique & UX des Bordures

L'apparition soudaine d'un chunk provoque un **stutter** (chute brutale de FPS).

### Asynchronous Upload & Time-Slicing (Anti-Stutter)
- **Problème :** `glBufferData` d'un mesh entier en une frame bloque le CPU/GPU.
- **Solution :** File d'attente → upload via `glBufferSubData` par petites tranches (ex: 10% du mesh par frame) ou via `glMapBufferRange`.

### Animations d'Apparition (Vertex Shader)
- Passer un Uniform `float u_spawnTime` au shader lors de la création du chunk.
- Si le temps actuel est proche de `u_spawnTime`, modifier la position Y ou l'échelle → les blocs semblent **"pousser" du sol**.

### Distance Fog
- Brouillard exponentiel (`EXP2`) ou linéaire dans le Fragment Shader pour masquer la frontière du monde.
- La couleur du brouillard doit correspondre à la `glClearColor` (Skybox) pour un fondu parfait.
- ✅ **Implémenté** dans `chunk2.fs` (uniforms `fogStart`, `fogEnd`, `fogColor`) mais **désactivé** (`FOG_START = FOG_END = 0`).

---

## 10. Les "Fausses" Bonnes Idées (À Éviter)

### ❌ Greedy Meshing
Fusionner les faces adjacentes identiques en un grand quad semble optimal, mais :
- **Coût CPU** : Algorithme lourd à recalculer à chaque modification de bloc.
- **Incompatible AO** : Chaque sous-section du quad fusionné aurait besoin d'une ombre différente.
- **Gain négligeable** : Avec le Vertex Pulling, les sommets pèsent 4 octets. Le GPU préfère beaucoup de petits triangles simples.
- **Alternative** : Simple **Face Culling** (ne pas dessiner les faces cachées). Ultra-rapide et suffisant. ✅ *C'est ce que fait VoxPlace.*

### ❌ Génération côté Client
Demander au client de générer les chunks à partir du Seed :
- Bugs de **désynchronisation** massifs (structures qui se chevauchent).
- Un chunk compressé (quelques Ko) se transfère en < 1 seconde via le réseau.

---

## 11. To-Do

- [ ] **Frustum Culling** — Réduira la géométrie envoyée de ~4×. Cas spécial : que se passe-t-il si le joueur est *dans* un cube ?
- [ ] **Z-Prepass** — Implémenter avec toggle `F3`. Potentiel ×2 FPS si pas de deferred renderer.
- [ ] **Génération serveur** — Déplacer la génération du monde côté serveur (lazy generation).
- [ ] **MDI** — Implémenter `glMultiDrawArraysIndirect` pour réduire les draw calls de 400 à 1.
- [ ] **`.reserve()` sur les vecteurs** — Décommenter `packedFaces.reserve(...)` dans `meshGenerate()`.
- [ ] **Activer le fog** — Décommenter le calcul fog dans `chunk2.fs` et ajuster `FOG_START`/`FOG_END`.
- [ ] **Multithreading** — Déplacer `meshGenerate()` et `fillChunk()` dans un thread pool.
- [ ] **Dirty flag rebuild** — Actuellement les chunks sont rebuildés dans la render loop si `needsMeshRebuild`. Optimiser pour qu'un seul chunk soit rebuild par frame.

---

## 12. Liens Utiles

| Ressource | Lien |
|-----------|------|
| Voxel Wiki (Bible du voxel) | https://voxel.wiki/ |
| Article Vertex Pulling | https://voxel.wiki/wiki/vertex-pulling/ |
| Vidéo SSBO/Geometry | https://www.youtube.com/watch?v=IoS5opco9LA&t=1s |
| Tutoriels OpenGL (FR) | https://opengl.developpez.com/tutoriels/apprendre-opengl/ |
| Pipeline diagram | https://opengl.developpez.com/tutoriels/apprendre-opengl/images/pipeline.png |
| Cheat sheet (SO) | https://stackoverflow.com/questions/2772570/opengl-cheat-sheet |
| Cheat sheet (repo) | https://github.com/henkeldi/opengl_cheatsheet |
| Shadertoy | https://www.shadertoy.com/ |

---

## 13. Bonnes Pratiques OpenGL

- Toujours vérifier l'init : `glfwInit()` puis `gladLoadGLLoader()`.
- Utiliser `glfwWindowHint` pour demander la bonne version de contexte (VoxPlace : 4.6 Core).
- Exclure `glad.c` pour les builds WebAssembly (le loader est fourni par Emscripten).
- Pour WASM : préférer WebGL2 et `emscripten_set_main_loop` au lieu d'une boucle `while`.
- Toujours vérifier la compilation/linkage des shaders via `glGetShaderiv` / `glGetProgramiv` et récupérer les logs avec `glGetShaderInfoLog` / `glGetProgramInfoLog`.

---

*Dernière mise à jour : 19 février 2026*
