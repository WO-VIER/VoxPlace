# VoxPlace

Expérience r/place en 3D

Développé en C++23, OpenGL 4.6, et ENet.

## Prérequis

### Linux

- CMake >= 3.20
- GCC >= 13 ou Clang >= 17 (support C++23 requis)
- pkg-config
- GPU et pilotes compatibles OpenGL 4.6 (Mesa ou propriétaires)

Arch / CachyOS / Manjaro :

```bash
sudo pacman -S cmake gcc pkgconf glfw enet libsodium sqlite zstd curl mesa
```

Ubuntu / Debian (24.04+) :

```bash
sudo apt install cmake g++ pkg-config libglfw3-dev libenet-dev libsodium-dev libsqlite3-dev libzstd-dev libcurl4-openssl-dev libgl-dev
```

### Windows

- CMake >= 3.20 : `winget install Kitware.CMake`
- MSVC Build Tools ou LLVM/Clang

Les dépendances sont téléchargées automatiquement au premier build via CMake FetchContent.

## Compilation

### Linux

```bash
# Debug (par défaut)
make debug

# Release
make release

# Nettoyage
make clean
make fclean
```

Les exécutables sont générés dans `build_debug/` ou `build_release/`.

### Windows

Ouvrir **x64 Native Tools Command Prompt for VS 2022** (ou équivalent Clang) à la racine du projet :

```powershell
# Debug
.\build.ps1

# Release
.\build.ps1 -Config Release

# Clean et rebuild
.\build.ps1 -Clean
```

Les exécutables sont générés dans `build\win-release\Release\`.

## Utilisation

### Serveur

Lancez d'abord le serveur :

```bash
# Linux
./build_debug/VoxPlaceServer [options]
./build_release/VoxPlaceServer [options]

# Windows
.\build\win-release\Release\VoxPlaceServer.exe
```

Options :

```text
--classic-gen           Active la génération de terrain "classic streaming" autour du joueur
--port <port>           Change le port d'écoute du serveur (défaut : 28713)
--db <path>             Fichier SQLite pour la persistance des joueurs
--world-db <path>       Fichier SQLite pour la persistance des chunks du monde
--full-db               Persiste aussi les chunks générés (défaut : mode modified-only)
--help                  Affiche l'aide
```

Variables d'environnement :

```text
VOXPLACE_SERVER_WORKERS=<n>     Change le nombre de threads de calcul
VOXPLACE_PROFILE_WORKERS=1      Active l'affichage du profiling des workers
VOXPLACE_PROFILE_JSON=1         Client: émet des snapshots JSON combinés client+serveur dans stdout et logs/voxplace_profile.jsonl
VOXPLACE_PROFILE_JSON_PATH=<p>  Client: remplace le chemin du fichier JSONL de profiling
VOXPLACE_ADMIN_USERS=<names>    Bootstrap admin: pseudos séparés par virgule/espace, persistés en DB au login
VOXPLACE_SERVER_KEY_PATH=<p>    Serveur: remplace le chemin de la clé privée/libsodium persistée
VOXPLACE_SERVER_PROOF_PATH=<p>  Serveur: écrit le JSON .proof officiel à ce chemin si défini
VOXPLACE_SERVER_PROOF_HOST=<h>  Serveur: host publié dans le JSON .proof (défaut: play.voxplace.codes)
VOXPLACE_SERVER_PROOF_URL=<url> Client: remplace l'URL HTTPS .proof du serveur officiel
```

Le compte bootstrap `Admin` avec le mot de passe `admin` est aussi promu admin
au login et persiste ce droit en base de données.

Exemple :

```bash
./build_debug/VoxPlaceServer --classic-gen --port 28713
```

Mode de persistance du monde :

- **modified-only** par défaut
  - seuls les chunks effectivement modifiés par les joueurs sont persistés
- **full-db** avec `--full-db`
  - les chunks générés sont aussi persistés dans la DB monde

### Serveur public

Un serveur de démonstration est disponible pour tester le projet :

```text
Adresse : play.voxplace.codes
Port    : 28713 (UDP)
Mode    : ClassicStreaming
```

### Client

Pour se connecter à un serveur en cours d'exécution :

```bash
# Linux
./build_debug/VoxPlace

# Windows
.\build\win-release\Release\VoxPlace.exe
```

Le client se connecte à `localhost:28713` par défaut. L'adresse du serveur peut être modifiée depuis l'écran de connexion en jeu.

Un **mot de passe est obligatoire** dès qu'un username est utilisé.

Le serveur fonctionne avec la règle suivante :

- un `username` correspond à un compte unique côté serveur
- si le compte n'existe pas encore, il est créé avec le couple `username + password`
- si le compte existe déjà, le même `username` doit être fourni avec le bon mot de passe

Sécurité de l'authentification :

- les mots de passe de `LoginRequest` et `AccountDeleteRequest` ne transitent pas en clair ;
- le serveur expose une clé publique libsodium persistée et un challenge unique dans `Hello` ;
- le client chiffre le mot de passe avec `crypto_box` avant de l'envoyer sur ENet ;
- pour `play.voxplace.codes`, le client compare la clé reçue avec le fingerprint publié en HTTPS sur `https://voxplace.codes/.proof/voxplace-server.json` si le fichier est disponible ;
- pour les serveurs locaux ou personnalisés, le client utilise un modèle TOFU similaire à SSH et mémorise la première clé vue pour `host:port`.

Connexion au serveur public :

```bash
./build_debug/VoxPlace play.voxplace.codes 28713 MonPseudo MonMotDePasse
```

Connexion à un serveur local :

```bash
./build_debug/VoxPlace 127.0.0.1 28713 MonPseudo MonMotDePasse
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

Par défaut, le serveur fonctionne en mode **modified-only**.

Si vous voulez persister aussi les chunks générés, lancez le serveur avec `--full-db`.

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
thirdparty/        Bibliothèques tierces intégrées (imgui, FastNoiseLite, Tracy)
dependencies/      Chargeur OpenGL (glad, KHR)
assets/            Textures et shaders
scripts/           Scripts d'aide à la compilation et déploiement
```
