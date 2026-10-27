#!/bin/sh
# Genera una chiave HMAC casuale (base64, 32 byte) per NEWS_HMAC_KEY / "NEWS KEY".
python3 -c "import os,base64;print(base64.b64encode(os.urandom(32)).decode())"
