# John Carmack Tier: Debug & Optimisation en C++

Bienvenue dans le domaine des vrais ingénieurs système. Voici les principes d'optimisation et d'analyse que préconise John Carmack (id Software), adaptés à ton moteur Voxel (VoxPlace).

## 1. Compiler de manière "Stricte" (Le compilateur est ton ami)
Carmack l'a souvent dit : "*Treat warnings as errors*".
J'ai mis à jour ton `CMakeLists.txt` pour inclure :
`-Wall -Wextra -pedantic -Wformat=2 -Wno-unused-parameter`
Ces flags te forcent à écrire du code plus propre. Tu détecteras des variables non initialisées ou des conversions implicites dangereuses avant même de lancer le programme.

## 2. Address Sanitizer (ASan) : Ta nouvelle vue aux rayons X
Plutôt que de galérer avec des `Segmentation Fault` silencieux, on utilise ASan.
J'ai configuré `CMakeLists.txt` avec `option(ENABLE_ASAN "Enable Address Sanitizer" OFF)` et j'ai ajouté un profil de lancement ("Lancer (gdb) - Native (ASan)") dans `.vscode/launch.json`.

**Comment l'utiliser :**
Dans le menu Debug de VS Code, sélectionne **"C/C++: Lancer (gdb) - Native (ASan)"** et lance. 
Si ton code tente d'écrire en dehors d'un `Chunk` ou lit un voxel d'un `Chunk` déjà supprimé, le programme va planter *immédiatement* et te dire la ligne exacte où la mémoire a été corrompue et _quand_ elle avait été allouée. C'est magique.

## 3. Comprendre le "Padding" et l'agencement mémoire
Tu as utilisé `pahole`, c'est parfait pour voir le padding. L'architecture moderne de CPU est limitée par les *Cache Misses* et non la vitesse de calcul en FLOPs (Floating Point Operations).
*Dans ton programme :*
Assure-toi que les variables fréquemment accédées ensemble dans `Chunk2` ou tes voxels sont contiguës en mémoire et tiennent dans une **ligne de cache de 64 octets** ! 
Organise tes `structs` de tes variables les plus grosses (8 bytes = `double`, `size_t`, pointeurs) aux plus petites (1 byte = `bool`, `uint8_t`) pour que le compilateur réduise le padding.

## 4. Outils de l'éditeur VS Code à Installer :
*   **"Hex Editor" (ms-vscode.hexeditor) :** Indispensable. Mets un point d'arrêt, fais un clic droit sur un pointeur/tableau `uint8_t blocks[16][256][16]` et clique sur **"View Memory"**. Tu repéreras visuellement les motifs si tu corromps des données.
*   **Lien avec gdb :** Dans `.vscode/launch.json`, j'ai vérifié que tu avais `-enable-pretty-printing`. C'est l'option qui permet à GDB d'afficher proprement tes structures std::vector au lieu d'afficher l'en-tête mémoire de la classe.

## 5. Les "Data Breakpoints" (Points d'arrêt sur la donnée)
Plutôt que de faire `F10` et `F11` (Step Over/Into) sans fin...
1. Lance le debugger "Native" classique (pas forcément ASan).
2. Va dans l'onglet **Variables**, déploie ton `Chunk2`.
3. Clique droit sur la variable `needsMeshRebuild` ou un `bool isEmpty`.
4. Sélectionne **"Break on Value Change"**. Le debugger arrêtera le runtime instantanément au moment où n'importe qui dans tes 15000 lignes de codes change cette variable spécifique.

*“If you’re typing in a command repeatedly, you are doing something fundamentally wrong.” – John Carmack*
Si tu debug, fais-le en traquant la donnée (Data-Oriented), pas le flux d'instructions.
