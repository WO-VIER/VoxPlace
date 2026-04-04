# Récapitulatif TFE : Optimisation et Profilage Réseau/Multithreading de VoxPlace

Ce document résume le travail itératif, les expérimentations, et les décisions architecturales prises concernant la gestion des threads et l'optimisation réseau du client et du serveur VoxPlace. Ces notes sont conçues pour être directement intégrées ou servir de base à la rédaction du rapport de Travail de Fin d'Études (TFE).

---

## 1. Contexte et Objectif Initial
- **Situation :** Le projet VoxPlace est passé d'une architecture "Serveur Local" (le serveur tourne sur la machine du joueur) à une architecture "Client Only" connecté à un Serveur Distant (VPS : 2 vCPUs, 2 Go RAM, SSD à Francfort).
- **Problème initial :** Comment configurer les "Mesh Workers" (threads de calcul de la géométrie 3D) côté client pour maximiser les FPS et minimiser la latence (stuttering) sans surcharger le CPU (oversubscription) ?

---

## 2. Phase 1 : L'Oversubscription et l'Allocation Dynamique des Workers

### L'Analyse (Le "Sweet Spot")
Nous avons lancé plusieurs benchmarks (`bench_mesh_workers.sh`) en faisant varier le nombre de workers (1, 2, 4, 8) sur un processeur local à 8 cœurs.
- **1 ou 2 Workers :** Le CPU client n'est pas exploité à fond, le chargement visuel de la carte est lent.
- **8 Workers :** Oversubscription. Les 8 workers saturent le processeur et entrent en conflit avec le Thread Principal (rendu OpenGL) et les threads de fond (ex: Profiler Tracy). Résultat : Le framerate (FPS) chute drastiquement.
- **4 Workers :** C'est le **"Sweet Spot"**. La géométrie est calculée extrêmement vite, tout en laissant suffisamment de cœurs libres pour garantir un rendu fluide (>60 FPS).

### L'Implémentation (La Solution)
Plutôt que d'avoir un nombre de workers fixe, nous avons implémenté une **heuristique d'allocation dynamique** dans `ClientChunkMesher::computeWorkerCount()` :
- **Si connexion en Localhost (`127.0.0.1`) :** Le client reste très conservateur (1 ou 2 workers max) pour laisser la puissance CPU au serveur local (qui doit calculer le terrain avec FastNoise).
- **Si connexion Distante (VPS) :** Le client sait qu'il a la machine pour lui seul. Il s'alloue automatiquement **50% des cœurs disponibles** (capé à 4 pour préserver les FPS). *Note : Le code respecte la norme 42 (pas de ternaires, structures if/else claires).*

---

## 3. Phase 2 : Le Goulot d'Étranglement Réseau (Bottleneck) et l'Architecture "..."

### Le Problème du "Stuttering" (Micro-saccades)
Lors des vols rapides (Fly), le client recevait d'énormes quantités de données (chunks). L'ancienne architecture lisait les paquets ENet dans une boucle `while` infinie sur le Main Thread. 
- **Conséquence :** Si 500 paquets arrivaient, le jeu figeait le temps de les traiter, provoquant des chutes de FPS vertigineuses.

### La solution : Le Time-Slicing (Packet Capping)
- **La solution :** Remplacer la lecture illimitée par une boucle limitée à **50 paquets maximum par frame**. 
- **L'Explication :** En limitant le traitement réseau par frame, on s'assure que le rendu graphique garde toujours la priorité. Inspiré par des architectures éprouvées (...), cette approche garantit que le protocole ENet (interrogé de manière non-bloquante avec `timeout = 0`) ne bloque jamais le rendu OpenGL, rendant l'architecture **Frame-Rate Independent**.

---

## 4. Phase 3 : L'Optimisation Extrême du Serveur (Le Débridage)

Malgré les optimisations client, la bande passante globale stagnait autour de 35-40 chunks par seconde. Le problème venait des limites hardcodées du serveur.

### Le Débridage des Fenêtres ENet
Le serveur (`WorldServer.cpp`) limitait l'envoi de paquets "En Vol" (In-Flight) à 12 par client. À cause du Ping vers Francfort, le serveur passait son temps à attendre les accusés de réception.
- **La solution :** Augmentation massive des limites (passage de 12 à 512 `MAX_PENDING_CHUNK_SNAPSHOTS`).
- **Résultat :** Le débit réseau a **explosé**, atteignant jusqu'à **75 chunks par seconde** en plein téléchargement de nouvelle zone.

### La Génération Procédurale (FastNoise) et l'I/O (Disque)
Nous avons testé le serveur dans ses pires conditions (téléportation à 100 000 blocs du centre) pour forcer la génération 100% procédurale au CPU, sans aide de la base SQLite.
- **La Force de l'architecture Multi-thread Serveur :** Grâce à un pool de workers dédié à la génération du terrain (et le thread `saveWorker` séparé), le petit vCPU du VPS n'a jamais dépassé les **15% d'utilisation**.
- **La Compression :** L'utilisation de **Zstd Level 1** (asymétrique : compression rapide côté serveur, décompression instantanée côté client) s'est avérée être un choix parfait pour maximiser le débit.
- **Le mode `--modified-only` :** Le serveur ne sauvegarde les chunks sur le disque (I/O) QUE si un joueur a altéré le terrain (posé/détruit un bloc). Le reste du monde est considéré comme éphémère et recalculé à la volée. C'est une optimisation drastique pour préserver le SSD du VPS et éviter les bases de données obèses.

---

## 5. Phase 4 : Analyse des Latences Résiduelles (Le Pipeline)

### Le phénomène de Latence au "Demi-Tour"
Lorsqu'un joueur se retourne subitement vers une zone non-générée, un léger délai d'attente se fait sentir avant l'apparition brutale et ultra-rapide des chunks.

**L'Explication technique pour le TFE :**
C'est la différence fondamentale entre la **Latence (Ping)** et le **Débit (Throughput)**.
1. **Priorité du Frustum :** Le client est codé pour exiger en priorité les chunks situés dans son champ de vision immédiat.
2. **La Latence (Le Ping) :** Les 16 premières requêtes de chunks doivent traverser l'Europe jusqu'au VPS, être générées, compressées, et renvoyées. Ce délai physique (le Ping de ~50-100ms) crée le trou visuel initial.
3. **Le Débit (Le Pipeline plein) :** Dès que le premier paquet revient, le pipeline réseau se débloque. Le client envoie la suite des requêtes, et le flux massif de **65 chunks/sec** prend le relais pour "mitrailler" l'affichage grâce à la vitesse des 4 Mesh Workers.

---

## Conclusion Globale de l'Ingénierie
L'architecture VoxPlace a été empiriquement prouvée comme étant hautement résiliente. Le goulot d'étranglement (Bottleneck) a été déplacé avec succès du **CPU Client** (grâce à l'allocation dynamique), vers le **Code Réseau Bloquant** (grâce au Time-Slicing), pour finalement se heurter à la **Limite Physique de l'Internet** (Bande passante et Ping du VPS). 

Le dernier bottleneck mineur identifié par le profiler reste le temps de rendu GPU (`❌ Le GPU est trop lent pour afficher la scène`), ce qui ouvre la porte à de futures améliorations graphiques telles que l'Occlusion Culling.
