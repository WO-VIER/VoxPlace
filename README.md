# VoxPlace

Expérience r/place en 3D

Développé en C++23, OpenGL 4.6, et ENet.

## Prérequis

- Linux (testé sur Arch)
- CMake >= 3.10
- GCC >= 13 ou Clang >= 17 (support C++23 requis)
- pkg-config
- GPU et pilotes compatibles OpenGL 4.6 (Mesa ou propriétaires)

### Dépendances système

Arch / CachyOS / Manjaro :

```bash
sudo pacman -S cmake gcc pkgconf glfw enet libsodium sqlite zstd mesa
```

Ubuntu / Debian (24.04+) :

```bash
sudo apt install cmake g++ pkg-config libglfw3-dev libenet-dev libsodium-dev libsqlite3-dev libzstd-dev libgl-dev
```


## Compilation

Build de Debug (par défaut) :
- Flags : `-g3`, `-O0`, `-DEBUG`

```bash
make native
```

Build de Release (Optimisé) :
- Flags : `-O3`, `-march=native`, `-flto`, `-DNDEBUG`

```bash
make native-release
```

Les exécutables sont générés dans `build/` ou `build_release/`.

## Utilisation

### Serveur

Lancez d'abord le serveur :

```bash
./build/VoxPlaceServer [options]
```

ou

```bash
./build_release/VoxPlaceServer [options]
```

Options :

```text
--classic-gen           Active la génération de terrain "classic streaming" autour du joueur
--port <port>           Change le port d'écoute du serveur (défaut : 28713)
--db <path>             Fichier SQLite pour la persistance des joueurs
--world-db <path>       Fichier SQLite pour la persistance des chunks du monde
--modified-only-world   Ne persiste que les chunks effectivement modifiés par les joueurs
--help                  Affiche l'aide
```

Variables d'environnement :

```text
VOXPLACE_SERVER_WORKERS=<n>     Change le nombre de threads de calcul
VOXPLACE_PROFILE_WORKERS=1      Active l'affichage du profiling des workers
```

Exemple :

```bash
./build/VoxPlaceServer --classic-gen --port 28713
```

### Serveur public

Un serveur de démonstration est disponible pour tester le projet :

```text
Adresse : 161.35.214.248
Port    : 28713 (UDP)
Mode    : ClassicStreaming
```

### Client

Pour se connecter à un serveur en cours d'exécution :

```bash
./build/VoxPlace
```

Le client se connecte à `localhost:28713` par défaut. L'adresse du serveur peut être modifiée depuis l'écran de connexion en jeu.

Un **mot de passe est obligatoire** dès qu'un username est utilisé.

Le serveur fonctionne avec la règle suivante :

- un `username` correspond à un compte unique côté serveur
- si le compte n'existe pas encore, il est créé avec le couple `username + password`
- si le compte existe déjà, le même `username` doit être fourni avec le bon mot de passe

Connexion au serveur public :

```bash
./build/VoxPlace 161.35.214.248 28713 MonPseudo MonMotDePasse
```

Connexion à un serveur local :

```bash
./build/VoxPlace 127.0.0.1 28713 MonPseudo MonMotDePasse
```

Exemple en release :

```bash
./build_release/VoxPlace 161.35.214.248 28713 MonPseudo MonMotDePasse
```

### Persistance SQLite

Le projet utilise actuellement SQLite pour deux choses distinctes :

- **DB joueurs** (`--db`)
  - compte joueur
  - hash du mot de passe
  - position
  - cooldown d'action bloc
- **DB monde** (`--world-db`)
  - chunks du monde persistés
  - payload compressé en `Zstd` niveau 3
  - stratégie `load or generate`

En `classic-gen`, les chemins par défaut sont :

- `voxplace_players_classic_gen.sqlite3`
- `voxplace_world_classic_gen.sqlite3`

En mode non `classic-gen`, les chemins par défaut sont :

- `voxplace_players_classic_voxplace.sqlite3`
- `voxplace_world_classic_voxplace.sqlite3`

## Structure du projet

```text
src/
  client/          Code côté client (rendu, entrées, UI)
  server/          Code côté serveur (génération, réseau, persistance)
include/           Headers publics
thirdparty/        Bibliothèques tierces intégrées (imgui, FastNoiseLite)
dependencies/      Chargeur OpenGL (glad, KHR)
assets/            Textures et shaders
scripts/           Scripts d'aide à la compilation
```

## Windows

### Méthode simple (recommandée)

Un seul script PowerShell fait tout : vérifie les deps, installe vcpkg, configure et build.

**1. Installer un compilateur (au choix) :**

```powershell
# Option A : MSVC Build Tools (~500MB, pas besoin de VS)
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools"

# Option B : LLVM/Clang (~200MB)
winget install LLVM.LLVM
```

**2. Installer CMake et Ninja (si pas déjà fait) :**

```powershell
winget install Kitware.CMake Ninja-build.Ninja
```

**3. Ouvrir un terminal et build :**

```powershell
# Debug
.\build.ps1

# Release
.\build.ps1 -Config Release

# Clean et rebuild
.\build.ps1 -Clean
```

Le script clone vcpkg automatiquement s'il n'est pas présent. Aucune configuration manuelle nécessaire.

### Méthode manuelle (CMake presets)

Pour ceux qui veulent contrôler le processus :

```powershell
# MSVC (depuis x64 Native Tools Command Prompt)
cmake --preset windows-debug
cmake --build --preset windows-debug

# Clang
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-release
```

### Utilisation

```powershell
.\build\win-debug\VoxPlace.exe
.\build\win-debug\VoxPlaceServer.exe
```
