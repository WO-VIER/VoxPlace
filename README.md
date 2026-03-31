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
--classic-gen         Active la génération de terrain "classic streaming" autour du joueur
--port <port>         Change le port d'écoute du serveur (défaut : 28713)
--db <path>           Fichier SQLite pour la persistance des joueurs
--world-db <path>     Fichier SQLite pour la persistance des chunks du monde
--help                Affiche l'aide
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

Connexion au serveur public :

```bash
./build/VoxPlace 161.35.214.248 28713 MonPseudo
```

Connexion à un serveur local :

```bash
./build/VoxPlace 127.0.0.1 28713 MonPseudo
```

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

Les builds natifs Windows ne sont pas supportés. Utilisez WSL2 avec une image Ubuntu ou Arch pour compiler et lancer le projet sous Windows.
