#!/usr/bin/env python3
"""
Generate Arduino config.h from mycam-api/.env
Run this before compiling the Arduino sketch
"""

import os
import re

# Read .env file from mycam-api
env_file = "../mycam-api/.env"
config_h = "CameraWebServer/config.h"

# Default values
defaults = {
    "WIFI_SSID": "",
    "WIFI_PASSWORD": "",
    "SERVER_API_URL": "",
    "API_KEY": "",
    "CAMERA_KEY": "",
    "SAVE_INTERVAL": "5",
}

env_vars = defaults.copy()

# Read and parse .env file
if os.path.exists(env_file):
    try:
        with open(env_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    key, value = line.split('=', 1)
                    key = key.strip()
                    value = value.strip().strip('"\'')
                    
                    # Map .env keys to config keys
                    if key == "API_KEY":
                        env_vars["API_KEY"] = value
                    elif key == "CAMERA_KEY":
                        env_vars["CAMERA_KEY"] = value
                    elif key == "SERVER_API_URL":
                        env_vars["SERVER_API_URL"] = value
        print(f"✓ Read {env_file}")
    except Exception as e:
        print(f"⚠ Could not read {env_file}: {e}")
        print("Using default values...")
else:
    print(f"⚠ {env_file} not found, using defaults")

# Generate config.h
header = '''#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// AUTO-GENERATED CONFIG FROM mycam-api/.env
// Run: python3 generate_config.py
// ============================================

// WiFi Credentials
#define WIFI_SSID "%(WIFI_SSID)s"
#define WIFI_PASSWORD "%(WIFI_PASSWORD)s"

// Server API Configuration
#define SERVER_API_URL_CONFIG "%(SERVER_API_URL)s"
#define API_KEY_CONFIG "%(API_KEY)s"
#define CAMERA_KEY_CONFIG "%(CAMERA_KEY)s"

// Image save interval (seconds)
#define SAVE_INTERVAL %(SAVE_INTERVAL)s

#endif // CONFIG_H
''' % env_vars

with open(config_h, 'w') as f:
    f.write(header)

print(f"✓ Generated {config_h}")
print("\nConfiguration:")
print(f"  WIFI_SSID: {env_vars['WIFI_SSID']}")
print(f"  API_KEY: {env_vars['API_KEY'][:20]}...")
print(f"  CAMERA_KEY: {env_vars['CAMERA_KEY']}")
print(f"  SERVER_API_URL: {env_vars['SERVER_API_URL']}")
