# esp32 avec cam OV3660

Objectif principal : faire une camera du surveillance connectée.
Langages : arduino & C / C++

## Prompt

**Feature détection de mouvement & photos**

Je veux que tu ajoutes une fonctionnalité de détection de mouvement.
Si la camera détecte un mouvement alors on prend une photo.
On l'enregistre sur la carte sd.

En contrepartie si aucun mouvement il me faut une photo toutes les 5 secondes (et non toutes les 2 secondes)

**Feature suppression anciennes photos**

La carte sd a un stockage de 8go. Fait un algo qui supprime les photos dès que la carte SD dépasse les 7go de mémoires occupées.

**Feature connexion server**

Je vais créer une app server qui transmet la vidéo en temps réel. Il faudrait créer appeler une route de mon API pour transmettre la vidéo. Simule ceci à une URL comme ceci : api.augustin/cams
Il me faut une API key et une camera key stp.
Deux variables à définir.

**Feature notif**

Quand la cam détecte un mouvement elle doit appeler l'api et api.augustin/cams/id 
toujours avec API key et une camera key.
C'est une route POST de notif

