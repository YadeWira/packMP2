# VM Windows 10 LTSC — Guía de conexión para la IA

Esta máquina virtual existe para **probar compatibilidad de software en Windows**
y dar a los agentes de IA un entorno Windows real para varios proyectos.
Corre en un contenedor Docker (`dockur/windows`) sobre el host Linux, con
aceleración KVM. Tú (la IA) te conectas por **SSH** desde el host.

> **Provisionada para agentes (2026-06-08):** auth por clave SSH (sin
> contraseña), **PowerShell 7 como shell por defecto**, git + Chocolatey,
> Microsoft Store + winget, Windows actualizado (build 19044.7291), Defender
> con `C:\Users\ia` excluido, y un profile que silencia las barras de progreso
> (evita los errores "Access denied reading console output buffer" sobre SSH).
> Detalles e inventario en §3; gotchas headless en §4b.

---

## 0. Ciclo de vida — APAGADA POR DEFECTO (LÉEME PRIMERO)

Esta VM está **detenida por defecto** para no consumir RAM del host (~8 GB). El
contenedor tiene `restart=no`: **no arranca solo**. Como agente, tú gestionas su
ciclo de vida:

1. **Antes de usarla — arráncala:**
   ```bash
   sudo docker start win10
   ```
   Luego **espera a que el SSH responda** (~30-90 s) sondeando con `ssh` real:
   ```bash
   for i in $(seq 1 20); do
     ssh -i ~/.ssh/winvm -p 2222 -o StrictHostKeyChecking=no -o BatchMode=yes \
       -o ConnectTimeout=8 ia@localhost "echo UP" 2>/dev/null | grep -q UP && { echo listo; break; }
     sleep 6
   done
   ```

2. **Úsala** para tu tarea (ver §2).

3. **Cuando termines POR COMPLETO — apágala** para liberar la RAM:
   ```bash
   sudo docker stop win10
   ```

> **Reglas:**
> - No la dejes encendida si ya no la necesitas — el host necesita la RAM.
> - **Antes de apagar, comprueba que ningún otro agente la esté usando**
>   (`sudo docker ps` para ver si está arriba; si no la arrancaste tú en esta
>   sesión, no asumas que puedes pararla). Apágala solo cuando estés seguro de que
>   nadie más la necesita.
> - Apaga solo cuando hayas **terminado completamente**, no entre pasos.

---

## 1. Datos de conexión

| Dato | Valor |
|---|---|
| Protocolo | SSH |
| Host | `localhost` (desde el mismo servidor) |
| Puerto | `2222` |
| Usuario | `ia` (administrador) |
| **Auth preferida** | **clave SSH** `~/.ssh/winvm` (sin contraseña) |
| Auth alternativa | contraseña `WinTest.2026` |
| **Shell por defecto** | **PowerShell 7 (`pwsh`)** — ya NO es `cmd.exe` |
| Sistema | Windows 10 Enterprise LTSC 2021 Eval (x64, build **19044.7291**, actualizado 2026-06-08) |
| Recursos | 4 vCPU · 8 GB RAM · 64 GB disco |

> El puerto `2222` del host se reenvía al puerto `22` de la VM.

---

## 2. Conectarse (recomendado: clave, sin contraseña)

```bash
# Comando único (lo más útil para automatizar) — SIN contraseña
ssh -i ~/.ssh/winvm ia@localhost -p 2222 'git --version'

# Sesión interactiva
ssh -i ~/.ssh/winvm ia@localhost -p 2222

# Fallback con contraseña (si la clave falla): requiere 'sshpass'
sshpass -p 'WinTest.2026' ssh -o PreferredAuthentications=password ia@localhost -p 2222 'echo hola'
```

**El shell remoto es PowerShell 7.** Los comandos que envíes son **PowerShell**,
no `cmd`. Ejemplos:

```bash
ssh -i ~/.ssh/winvm ia@localhost -p 2222 'Get-ComputerInfo | Select OsName,OsVersion'
ssh -i ~/.ssh/winvm ia@localhost -p 2222 '$PSVersionTable.PSVersion'
```

### ⚠️ Quoting: usa `-File` para scripts no triviales
El paso de comandos complejos por `ssh "..."` → `pwsh -c "..."` puede mancharse
con el quoting de bash + PowerShell. Para cualquier cosa con comillas, espacios
o varias líneas, **sube un `.ps1` y ejecútalo con `-File`**:

```bash
scp -i ~/.ssh/winvm -P 2222 script.ps1 ia@localhost:C:/Users/ia/script.ps1
ssh -i ~/.ssh/winvm ia@localhost -p 2222 'pwsh -NoProfile -File C:\Users\ia\script.ps1'
```

### Copiar archivos (host ↔ Windows)
```bash
# Subir (usa rutas SIMPLES sin espacios como destino; luego mueve con pwsh si hace falta)
scp -i ~/.ssh/winvm -P 2222 ./build.exe ia@localhost:C:/Users/ia/build.exe
# Bajar
scp -i ~/.ssh/winvm -P 2222 ia@localhost:C:/Users/ia/salida.log ./
```
> `scp` a rutas con espacios (ej. `C:/Program Files/...`) falla con shell pwsh;
> sube a `C:/Users/ia/` y luego `Copy-Item` con un `pwsh -File`.

---

## 3. Herramientas instaladas

| Herramienta | Versión | Notas |
|---|---|---|
| PowerShell 7 | 7.6.2 | shell por defecto de SSH; `C:\Program Files\PowerShell\7\pwsh.exe` |
| Windows PowerShell | 5.1 | integrado, sigue disponible como `powershell` |
| git | 2.54.0 | en PATH |
| Chocolatey | 2.7.2 | `choco install -y <pkg>` para añadir más (fiable headless) |
| Microsoft Store | 22603.1401.14.0 | añadido a LTSC (no venía); + Xbox Identity Provider |
| winget | 1.28.240 | instalado, pero **solo funciona por RDP/interactivo** — headless da "Access denied" (limitación MSIX). Usa choco por SSH. |
| Defender | activo | Tamper Protection ON (no se puede apagar headless); `C:\Users\ia` excluido del escaneo |

**Profile all-users de pwsh** (`C:\Program Files\PowerShell\7\profile.ps1`):
```powershell
$ProgressPreference = "SilentlyContinue"   # mata el error de buffer de consola sobre SSH
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
```
El refresh de PATH hace que las herramientas nuevas (instaladas con choco) aparezcan
en sesiones SSH **sin reiniciar sshd**.

> Instalar algo nuevo: `ssh -i ~/.ssh/winvm ia@localhost -p 2222 'choco install -y <paquete>'`.
> Aparecerá en PATH en la siguiente sesión gracias al profile.

---

## 4. Tips de compatibilidad / testing

```bash
# Versión / arquitectura
ssh -i ~/.ssh/winvm ia@localhost -p 2222 '[Environment]::OSVersion.Version; $env:PROCESSOR_ARCHITECTURE'

# Ejecutar un .exe y capturar el código de salida
ssh -i ~/.ssh/winvm ia@localhost -p 2222 'C:\Users\ia\build.exe; "EXIT=$LASTEXITCODE"'

# Hash de un archivo
ssh -i ~/.ssh/winvm ia@localhost -p 2222 '(Get-FileHash C:\Users\ia\x.bin -Algorithm SHA1).Hash'
```

> **Gotcha de procesos colgados:** un `.exe` que crashea puede dejar procesos
> zombie que bloquean el archivo (no se puede re-subir). Límpialos con
> `Stop-Process -Name miexe -Force -ErrorAction SilentlyContinue`. Si no se dejan
> matar (sesión Services), `sudo docker restart win10` es el reset fiable.

---

## 4b. Gotchas headless aprendidos (importante para agentes)

- **MSIX/empaquetado por SSH = "Access is denied"**: `winget` y cualquier app MSIX
  necesitan token de sesión interactiva. Por SSH (logon de red/sesión 0) fallan.
  → Para paquetes CLI usa **Chocolatey** (funciona headless). winget solo por RDP.

- **Windows Update headless**: `Microsoft.Update.Session` → `Installer.Install()`
  da `Access is denied (0x80070005)` por SSH. **Solución: correrlo como SYSTEM via
  tarea programada.** Patrón verificado:
  ```bash
  # 1) sube un wu_install.ps1 que use el COM Microsoft.Update.Session (Search/Download/Install)
  # 2) regístralo y dispáralo como SYSTEM:
  ssh -i ~/.ssh/winvm ia@localhost -p 2222 'schtasks /create /tn WUInstall /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\ia\wu_install.ps1" /sc once /st 00:00 /ru SYSTEM /rl HIGHEST /f; schtasks /run /tn WUInstall'
  # 3) sondea el log; el cumulative reinicia → el contenedor queda Exited:
  #    sudo docker start win10  y espera ~4 min a que finalice updates al bootear.
  ```

- **AMSI bloquea scripts "anti-AV"**: un `.ps1` que contenga `Set-MpPreference
  -DisableRealtimeMonitoring` o incluso `Add-MpPreference -Exclusion...` puede ser
  bloqueado como *"This script contains malicious content"*. La exclusión de
  `C:\Users\ia` ya está puesta. Real-time NO se puede apagar headless (Tamper
  Protection ON) — requiere RDP → Windows Security → Tamper Protection Off.

- **Tantos de Windows PowerShell 5.1 vs pwsh 7**: el módulo `Appx`
  (`Add-AppxPackage`, `Get-AppxPackage`) **NO carga en pwsh 7** (`0x80131539`).
  Para appx/MSIX usa `powershell.exe` (5.1): `powershell -NoProfile -File x.ps1`.

---

## 5. Otros accesos (opcionales, para un humano)

| Acceso | Dónde | Para qué |
|---|---|---|
| Visor web | `http://<IP-del-host>:8006` | Ver la pantalla de Windows en el navegador |
| RDP | `<IP-del-host>:3389` (user `ia`) | Escritorio remoto completo |

---

## 6. Gestión del contenedor (en el host Linux)

```bash
sudo docker ps                 # ¿está corriendo?
sudo docker logs -f win10      # logs en vivo
sudo docker stop win10         # apagar (estado se conserva)
sudo docker start win10        # encender
sudo docker restart win10      # reiniciar (reset fiable si SSH/procesos se traban)
sudo docker rm -f win10        # eliminar el contenedor (el disco persiste en ./storage)
```

- **Proyecto:** `/home/forum/winvm/`
- **Disco persistente:** `/home/forum/winvm/storage/`
- **Script de arranque (activa SSH):** `/home/forum/winvm/oem/install.bat`

> **La VM está apagada por defecto** (`restart=no`, no arranca sola — ver §0). Si
> SSH no responde casi siempre es porque está parada: `sudo docker start win10` y
> espera ~30-90 s. **Acuérdate de `docker stop win10` al terminar.**

### Recrear desde cero
Borra `storage/` y vuelve a lanzar el contenedor; reinstala Windows
automáticamente y el SSH se reactiva por `oem/install.bat`. **Nota:** tras
recrear hay que **re-provisionar** (clave SSH, pwsh por defecto, git, choco) —
ver §3 y la clave en `~/.ssh/winvm.pub`.

---

## 7. Si el SSH no responde

1. ¿Contenedor arriba? → `sudo docker ps` (si `Exited`: `sudo docker start win10`)
2. Probar el puerto: `nc -zv localhost 2222`
3. Esperar a que termine el arranque (~30-90 s) y reintentar.
4. Entrar por RDP/visor web y verificar: `Get-Service sshd` debe estar `Running`
   (`Start-Service sshd; Set-Service sshd -StartupType Automatic`).

---

*Host `forum` (Debian 13 + KVM). Auth por clave en `~/.ssh/winvm`. La contraseña
inicial `WinTest.2026` sigue activa como fallback — cámbiala si esta VM deja de
ser de usar y tirar. Este archivo está en `.gitignore` (contiene credenciales).*
