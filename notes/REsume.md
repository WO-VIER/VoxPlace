# 🚀 Architecture & Optimisations : Moteur Voxel Dynamique (Projet VoxPlace)

**Concept Core :** Moteur Client-Serveur où la génération procédurale (Perlin Noise) est pilotée dynamiquement par l'exploration et le placement de blocs par les joueurs ("Lazy Generation"). Pas de structures prédéfinies.

---

## 1. 🗺️ Lazy Generation & Gestion Mémoire (Côté Serveur)

Puisque le monde n'est pas généré en cercle autour du joueur mais de façon organique via les constructions, un tableau 2D ou 3D classique gaspillerait énormément de RAM avec des "trous" vides.

### Spatial Hashing (Table de Hachage)
Le monde n'alloue de la mémoire QUE pour les chunks qui existent.
* **Technique :** Utilisation de `std::unordered_map` avec une fonction de hachage bitwise custom pour des recherches en temps constant `O(1)`.
* **Implémentation C++ :**

```cpp
struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
};

struct ChunkHash {
    std::size_t operator()(const ChunkCoord& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.z) << 1);
    }
};

// Seuls les chunks existants prennent de la place en RAM
std::unordered_map<ChunkCoord, Chunk2*, ChunkHash> activeChunks;

### Trigger de Génération (Le `setBlock`)
* Le déclencheur n'est plus le déplacement, mais la fonction de modification du monde.
* Si un joueur pose un bloc en limite de chunk (ex: `x == 15`), le serveur vérifie la `unordered_map`. Si le chunk voisin (Est) n'existe pas, il est instancié asynchroneusement via le Perlin Noise.
* **Gestion du Vide (Physique) :** Les chunks non générés sont traités par le moteur physique comme des murs invisibles (Solid Colliders infinis) pour empêcher les joueurs de tomber dans le vide avant de construire.

---

## 2. 🎨 Rendu Dynamique & UX des Bordures de Map

L'apparition soudaine d'un chunk de 65 536 blocs provoque un "stutter" (chute brutale de FPS). Au lieu de faire du frame skipping (qui donne une impression de lag), voici les techniques fluides :

### Asynchronous Upload & Time-Slicing (Anti-Stutter)
* **Problème :** Envoyer des mégaoctets au VBO/SSBO via `glBufferData` en une frame bloque le CPU/GPU.
* **Solution :** Mettre le mesh du nouveau chunk dans une file d'attente (Queue). Le thread principal upload via `glBufferSubData` par petites tranches (ex: 10% du mesh par frame) ou utilise des buffers mappés (`glMapBufferRange`) pour ne jamais dépasser le budget de 16ms par frame.

### Animations d'Apparition (Vertex Shader)
Pour rendre la découverte organique, on anime l'apparition des blocs directement dans le GPU (zéro coût CPU).
* **Technique :** Lors de la création du chunk, on passe un Uniform `float u_spawnTime` au Shader.
* **Shader Magic :** Dans le Vertex Shader, si le temps actuel est proche de `u_spawnTime`, on modifie la position Y locale du sommet ou son échelle (Scale). Les blocs peuvent donner l'impression de "pousser" du sol ou de s'assembler brique par brique.

### Fog of War / Distance Fog
* Masquer la frontière abrupte du monde en utilisant un brouillard exponentiel (`EXP2`) dans le Fragment Shader.
* La couleur du brouillard doit correspondre exactement à la `glClearColor` de ton ciel (Skybox) pour que les chunks non générés se fondent parfaitement dans le vide atmosphérique.

---

## 3. 🖥️ Pipeline de Rendu & Optimisations GPU

### Face Culling Avancé
* **Principe :** Ne jamais générer la géométrie des faces cachées entre deux blocs opaques.
* **Bordures de Chunk :** Si un voisin dans la `unordered_map` retourne `nullptr` (vide), le Culling force la création d'une face pour "fermer" le mesh visuellement aux abords du vide.

### Le Z-Prepass (Depth Prepass)
Permet de réduire l'Overdraw (calculs inutiles de pixels cachés par d'autres blocs) à zéro.

* **Passe 1 (Profondeur uniquement) :**
  Désactivation des couleurs `glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);`. On rend toute la géométrie pour remplir le Depth Buffer.
* **Passe 2 (Couleur stricte) :**
  Réactivation des couleurs. On modifie le test de profondeur `glDepthFunc(GL_EQUAL);` et on relance le rendu. Le Fragment Shader (AO, Lumière) ne calcule QUE les pixels parfaitement au premier plan.
* ⚠️ **Piège :** Force l'exécution du Vertex Shader deux fois. Si la géométrie est absurde et le Shader simple, c'est une perte de FPS. (À tester via un toggle clavier `F3`).

### Vertex Pooling (Instanced Rendering)
L'optimisation VRAM absolue. Au lieu d'envoyer 4 sommets (xyz, uv, normales) = ~100+ octets par face :
* **Technique :** On envoie un seul `uint32_t` (ou 16 octets max) contenant toutes les infos bit-packées (Position locale 0-15, Hauteur 0-255, FaceID, Couleur/Texture).
* Dans le Vertex Shader, on utilise `gl_VertexID` (0 à 3) pour déduire et étirer le point mathématiquement en un carré parfait.

---

## 4. ⚙️ Multithreading (CPU) & Architecture Serveur

### Modèle "Thread Pool" Isolé (Data-Oriented Design)
* **L'ennemi :** Les `mutex` (verrous) qui forcent les threads à s'attendre et détruisent les perfs.
* **Le Flow :** Le Thread Principal emballe les données requises -> Envoie la tâche à un Worker inactif du Pool -> Le Worker calcule (ex: Perlin Noise, Meshing) en isolation totale -> Écrit dans un buffer de sortie -> Le Thread Principal récupère et upload (OpenGL exigeant d'être sur le main thread).

### Throttling & Time-Slicing Serveur (Objectif 20 TPS)
Le serveur a un budget strict de 50ms par boucle (Tick).
* **Génération :** Strictement limitée à 1 chunk par Tick pour ne pas freeze la logique physique/réseau.
* **Envoi Réseau (ENet/Sockets) :** Limité à 5 chunks envoyés par joueur par Tick. Évite la saturation des buffers TCP/UDP qui provoquerait le Timeout des joueurs.

### Simulation Distance vs Render Distance
* **Render Distance :** Le client affiche les chunks (ex: 64 chunks).
* **Simulation Distance :** Le serveur n'applique la physique et les "Random Tick Updates" (pousse, fluides) QUE dans un rayon très restreint (ex: 6 chunks) autour des joueurs. Les chunks éloignés dorment en mémoire (RAM).
