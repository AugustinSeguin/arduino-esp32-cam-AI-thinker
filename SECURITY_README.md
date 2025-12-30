# 🔐 Configuration de Sécurité ESP32-CAM

## Mesures de sécurité implémentées

### ✅ 1. Point d'Accès WiFi Protégé

- **Mot de passe WPA2** : `ESP32CAM2025`
- Connexion sécurisée au réseau "ESP32-CAM-XXXXXX"
- Configuration dans `config.h` : `#define AP_PASSWORD`

### ✅ 2. Chiffrement des Credentials (AES-128)

- Les credentials WiFi sont **chiffrés avec AES-128** avant stockage
- Utilisation de `mbedtls` pour le chiffrement/déchiffrement
- Clé de chiffrement configurable dans `config.h` : `#define AES_KEY`
- **⚠️ Important** : Changez la clé AES par défaut en production!

### ✅ 3. Authentification du Portail Web

- **Mot de passe obligatoire** : `admin123` (par défaut)
- Configuration dans `config.h` : `#define PORTAL_PASSWORD`
- Protection contre les accès non autorisés au portail de configuration

### ✅ 4. Protection CSRF (Cross-Site Request Forgery)

- Token CSRF généré aléatoirement à chaque session
- Validation du token pour chaque soumission de formulaire
- Empêche les attaques par formulaire malveillant

### ✅ 5. Rate Limiting

- **Maximum 3 tentatives de connexion**
- Blocage de 60 secondes après 3 échecs
- Protection contre les attaques par force brute

### ✅ 6. Validation Stricte des Entrées

- **SSID** : 1-32 caractères ASCII imprimables
- **Mot de passe WiFi** : 8-63 caractères (norme WPA2)
- Validation côté serveur avant sauvegarde
- Protection contre les injections et buffer overflow

### ✅ 7. Logs Conditionnels

- Flag `DEBUG_MODE` dans `config.h`
- Mettre à `false` en production pour désactiver les logs
- Réduit l'exposition d'informations sensibles

### ✅ 8. Captive Portal Sécurisé

- Redirection automatique vers la page de login
- Compatible avec Android, iOS, Windows
- DNS captif pour forcer l'ouverture du navigateur

## Configuration Recommandée

### 🔧 Avant le déploiement en production

1. **Modifier les mots de passe** dans `config.h` :

```cpp
#define AP_PASSWORD "VotreMotDePasseWiFi2025!"  // Min 8 caractères
#define PORTAL_PASSWORD "VotreMotDePassePortail!"
#define AES_KEY "VotreCleAES16chr"  // EXACTEMENT 16 caractères
```

2. **Désactiver le mode debug** :

```cpp
#define DEBUG_MODE false
```

3. **Utiliser des mots de passe forts** :

- AP_PASSWORD : Minimum 12 caractères, mélange lettres/chiffres/symboles
- PORTAL_PASSWORD : Minimum 12 caractères
- AES_KEY : 16 caractères aléatoires

## Utilisation

### Première connexion

1. **Connectez-vous au WiFi** :

   - Réseau : `ESP32-CAM-XXXXXX` (XXXXXX = 6 derniers caractères du MAC)
   - Mot de passe : `ESP32CAM2025` (par défaut)

2. **Captive Portal** :

   - Votre navigateur devrait s'ouvrir automatiquement
   - Sinon, allez sur `http://192.168.4.1`

3. **Authentification** :

   - Entrez le mot de passe du portail : `admin123` (par défaut)

4. **Configuration WiFi** :

   - SSID : Nom de votre réseau WiFi
   - Mot de passe : Mot de passe de votre réseau (min 8 caractères)
   - Cliquez sur "Sauvegarder et Redémarrer"

5. **Redémarrage** :
   - L'ESP32-CAM va redémarrer et se connecter à votre réseau

## Sécurité Avancée (TODO)

Les mesures suivantes peuvent être ajoutées pour renforcer encore la sécurité :

- [ ] HTTPS avec certificat auto-signé (WebServerSecure)
- [ ] Rotation automatique de l'API Key
- [ ] Authentification à deux facteurs (2FA)
- [ ] Liste blanche d'adresses MAC
- [ ] Chiffrement de bout en bout pour le stream vidéo
- [ ] Mise à jour OTA sécurisée avec signature
- [ ] Journalisation des événements de sécurité

## Avertissements

⚠️ **WiFi 4 (802.11n)** :

- Vulnérable aux attaques KRACK sur WPA2
- Pas de support WPA3 sur l'ESP32 classique
- Limité aux réseaux 2.4GHz

⚠️ **HTTP vs HTTPS** :

- Le portail utilise HTTP (non chiffré)
- Pour HTTPS, il faut utiliser `WebServerSecure` (plus de ressources)

⚠️ **Chiffrement AES** :

- AES-128 ECB mode (simple mais moins sécurisé que CBC/GCM)
- Pour une sécurité renforcée, utiliser AES-256 en mode CBC avec IV

## Support

Pour toute question de sécurité, contactez : **Augustin SEGUIN**

---

**Dernière mise à jour** : 30 décembre 2025
