# VM Windows 7 Ultimate — Guía de conexión para la IA

VM para **probar compatibilidad de software en Windows 7**. Corre en un contenedor
Docker (`dockur/windows`) sobre el host Linux con KVM. Te conectas por **SSH**.

> **Estado (2026-06-08):** ✅ OPERATIVA. SSH por **clave** (sin contraseña), Git 2.46.
> Montar Win7 + SSH fue NO trivial — lee §4 "Cómo está montado" antes de tocar nada,
> porque el mecanismo es distinto al de la Win10.

---

## 0. Ciclo de vida — APAGADA POR DEFECTO (LÉEME PRIMERO)

Esta VM está **detenida por defecto** para no consumir RAM del host (~6 GB). El
contenedor tiene `restart=no`: **no arranca solo**. Como agente, tú gestionas su
ciclo de vida:

1. **Antes de usarla — arráncala** (el servicio sshd levanta solo en ~24 s):
   ```bash
   sudo docker start win7
   for i in $(seq 1 20); do
     ssh -i ~/.ssh/winvm -p 2223 -o StrictHostKeyChecking=no -o BatchMode=yes \
       -o ConnectTimeout=8 ia@localhost "echo UP" 2>/dev/null | grep -q UP && { echo listo; break; }
     sleep 6
   done
   ```

2. **Úsala** para tu tarea (ver §2).

3. **Cuando termines POR COMPLETO — apágala** para liberar la RAM:
   ```bash
   sudo docker stop win7
   ```

> **Reglas:**
> - No la dejes encendida si ya no la necesitas — el host necesita la RAM.
> - **Antes de apagar, comprueba que ningún otro agente la esté usando**
>   (`sudo docker ps`). Si no la arrancaste tú en esta sesión, no asumas que puedes
>   pararla.
> - Apaga solo cuando hayas **terminado completamente**, no entre pasos.
> - Para comprobar SSH usa el comando `ssh` real, **no** sondas `head -c` (dan
>   falsos negativos; ver §7).

---

## 1. Datos de conexión

| Dato | Valor |
|---|---|
| Protocolo | SSH |
| Host | `localhost` (mismo servidor) |
| Puerto | **2223** |
| Usuario | `ia` (administrador; host real `IAWINDO-N0RPQCJ`) |
| **Auth** | **clave SSH** `~/.ssh/winvm` (la misma que la Win10) |
| Auth alternativa | contraseña `WinTest.2026` |
| Shell remoto | **`cmd.exe`** (PowerShell **2.0** disponible, muy limitado) |
| Sistema | Windows 7 Ultimate SP1 x64 (build 6.1.7601) |
| Recursos | 4 vCPU · 6 GB RAM · 48 GB disco |
| Proyecto en host | `/home/forum/win7vm/` |

Convive con la Win10 (puerto 2222) sin pisarse.

---

## 2. Conectarse

```bash
# Comando único (lo más útil para automatizar)
ssh -i ~/.ssh/winvm -p 2223 ia@localhost "whoami & ver"

# Sesión interactiva
ssh -i ~/.ssh/winvm -p 2223 ia@localhost

# Fallback con contraseña
sshpass -p 'WinTest.2026' ssh -o PreferredAuthentications=password ia@localhost -p 2223 "whoami"
```

> **El shell es `cmd`, NO PowerShell.** Los comandos van en sintaxis `cmd`
> (`echo %VAR%`, `&` para encadenar, `dir`). PowerShell 2.0 existe como
> `powershell -Command "..."` pero le faltan cmdlets modernos (`Expand-Archive`,
> `*-NetFirewall*`, `$PSScriptRoot`, etc.) — evítalo salvo necesidad.

### Copiar archivos
```bash
scp -i ~/.ssh/winvm -P 2223 ./build.exe ia@localhost:C:/Users/ia/build.exe
scp -i ~/.ssh/winvm -P 2223 ia@localhost:C:/Users/ia/salida.log ./
```

---

## 3. Herramientas instaladas

| Herramienta | Versión | Notas |
|---|---|---|
| OpenSSH server | 8.1.0.0 | en `C:\Program Files\OpenSSH`. **NO** es el servicio default; ver §4 |
| git | 2.46.0 | portable en `C:\Git`; binario en `C:\Git\cmd\git.exe` (última rama que soporta Win7) |
| Universal C Runtime | KB2999226 | necesario para que los binarios modernos arranquen en Win7 |
| Firewall | **desactivado** | es VM de pruebas (`netsh advfirewall set allprofiles state off`) |

> Para añadir tools en Win7: **descárgalas en el host Linux** y cópialas al disco
> (Win7 no puede bajar de GitHub por su TLS antiguo; choco/winget no funcionan sin
> .NET 4.x + TLS 1.2 extra). El material de instalación vive en `C:\OEM\` y en
> `/home/forum/win7vm/oem/` (host).

---

## 4. Cómo está montado (LÉEME antes de tocar)

Win7 NO tiene OpenSSH nativo y el equipo Win32-OpenSSH abandonó Win7. Montarlo
requirió sortear **cuatro** problemas encadenados. Así quedó la solución final:

1. **Binarios offline**: OpenSSH v8.1 + Git + UCRT se metieron vía la carpeta
   `oem/` que dockur copia a `C:\OEM` y ejecuta `install.bat` al final de la
   instalación desatendida.

2. **El servicio de Windows de OpenSSH NO se instala con `install-sshd.ps1`**
   (requiere PowerShell ≥5; Win7 trae 2.0). El servicio `sshd` se creó a mano con
   `sc create`.

3. **El servicio corre como LocalSystem y exige permisos estrictos en las host
   keys.** Esta fue LA causa raíz de los fallos (error 1067 / "no hostkeys
   available"). Las claves deben quedar:
   - **dueño**: `BUILTIN\Administrators`
   - **ACL**: SÓLO `NT AUTHORITY\SYSTEM:(F)` y `BUILTIN\Administrators:(F)`, herencia desactivada.
   - Si alguna vez se rompe, hay que hacer **`takeown` primero** (si el dueño es
     SYSTEM con Admins en solo-lectura, ni un admin puede reescribir la ACL).

4. **Arranque automático**: un `.bat` en la carpeta *Inicio de Todos los usuarios*
   (`C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup\fixssh.bat`)
   reasegura permisos y arranca el servicio en cada login (UAC está OFF y `ia` es
   admin, así que corre con privilegios). El servicio `sshd` además es auto-start.

> **Verificado**: SSH levanta solo ~24 s tras `docker restart win7`, sin intervención.
> Conexiones múltiples estables (5/5). NO uses `sshd -D` suelto: muere con
> "signal 8" tras la primera conexión — el **servicio** es el modo correcto.

### Script de arreglo de permisos (referencia, correr DENTRO de Win7 como admin)
```bat
for %K in (C:\ProgramData\ssh\ssh_host_*_key) do (
  takeown /F "%K"
  icacls "%K" /inheritance:r /grant:r "*S-1-5-18:(F)" "*S-1-5-32-544:(F)"
  icacls "%K" /setowner "*S-1-5-32-544"
)
sc config sshd start= auto & net start sshd
```

---

## 5. Otros accesos (para un humano)

| Acceso | Dónde |
|---|---|
| Visor web | `http://<IP-host>:8007` |
| RDP | `<IP-host>:3390` (user `ia`) |

---

## 6. Gestión del contenedor (host Linux)

```bash
sudo docker ps                 # estado
sudo docker logs -f win7       # logs del arranque/instalación
sudo docker stop win7          # apagar (estado se conserva)
sudo docker start win7         # encender (sshd levanta solo ~24s)
sudo docker restart win7       # reset fiable
```

- **Disco persistente:** `/home/forum/win7vm/storage/data.img`
- **Material de instalación (host):** `/home/forum/win7vm/oem/`

### Inspeccionar/arreglar el disco desde el host (VM apagada)
```bash
sudo docker stop win7
sudo losetup -fP /home/forum/win7vm/storage/data.img        # -> /dev/loop0
sudo mount -t ntfs3 -o ro /dev/loop0p2 /mnt/win7            # RO (o rw para editar)
# ... leer C:\fixssh.log, C:\ProgramData\ssh\..., etc.
sudo umount /mnt/win7 && sudo losetup -d /dev/loop0
sudo docker start win7
```
> Logs útiles dentro del guest: `C:\fixssh.log` (arranque de sshd) y, si se fuerza
> modo daemon, `C:\sshd_run.log`.

---

## 7. Si el SSH no responde

1. `sudo docker ps` — ¿contenedor arriba?
2. Conéctate por RDP (`:3390`) o visor web (`:8007`) y revisa `C:\fixssh.log`.
3. Comprueba el servicio: en `cmd` → `sc query sshd` (debe estar RUNNING) y
   `netstat -ano | find ":22"` (debe LISTENING).
4. Si las host keys salen "too open": corre el script de §4 (con `takeown`).
5. Reset fiable: `sudo docker restart win7`.

> ⚠️ La sonda `head -c N` sobre el puerto da **falsos negativos** (sshd manda ~33
> bytes de banner y espera al cliente; `head` se bloquea). Para comprobar SSH usa
> el **comando `ssh` real**, no un test de bytes crudos.

---

*Host `forum` (Debian 13 + KVM). Cambia la contraseña inicial `WinTest.2026`.*
