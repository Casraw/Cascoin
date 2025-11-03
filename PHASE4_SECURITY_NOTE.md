# ⚠️ WICHTIG: CVM Dashboard Security

## 🔒 Dashboard ist standardmäßig DEAKTIVIERT!

Aus Sicherheitsgründen ist der CVM Dashboard Web-Server **OPT-IN**:

### Aktivierung erforderlich:

```bash
# In cascoin.conf:
cvmdashboard=1

# Oder Kommandozeile:
./cascoind -cvmdashboard=1
```

### Warum disabled by default?

1. **Security First** - Kein ungewollter Web-Server
2. **Privacy** - Nur wenn User es will
3. **Ressourcen** - Spart RAM wenn nicht genutzt
4. **Best Practice** - Opt-In für zusätzliche Services

### Sicherheits-Features:

- ✅ Nur localhost (127.0.0.1)
- ✅ RPC Authentication
- ✅ Read-Only Operations
- ✅ Directory Traversal Protection
- ✅ No external access

**Das System ist sicher by design!** 🛡️
