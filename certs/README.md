# Development certificates

The private key and certificate in this directory are generated locally and
are intentionally ignored by Git.

To generate a self-signed certificate for local development:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout certs/server.key \
  -out certs/server.crt \
  -days 365 \
  -subj "/CN=localhost"
```

On Windows PowerShell, use the equivalent `openssl.exe` command.

Never commit `certs/server.key`.
