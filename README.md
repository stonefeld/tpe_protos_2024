# Entrega del Trabajo Practico Especial de Protocolos de Comunicacion

## Integrantes

- Bendayan, Alberto (Legajo: 62786)
- Boullosa Gutierrez, Juan Cruz (Legajo: 63414)
- Quian Blanco, Francisco (Legajo: 63006)
- Stanfield, Theo (Legajo: 63403)

## Docentes

- Codagnone, Juan Francisco
- Garberoglio, Marcelo Fabio
- Kulesz, Sebastian

## Compilacion

Para compilarlo deben tener la librería uuid instalada (que ayuda a la creación de archivos random por usuario).
Para Ubuntu/Debian se puede instalar corriendo:

```bash
sudo apt install uuid-dev
```

Una vez instalada la dependencia, correr el siguiente comando:

```bash
CC=gcc make clean all
```

## Protocolo SMTP

- Proposito: Enviar emails
- Comandos:
  - `EHLO`
  - `HELO`
  - `MAIL FROM`
  - `RCPT TO`
  - `DATA`
  - `QUIT`

## Protocolo de Supervisión

- Proposito: Brindar informacion del servidor SMTP.
- Credenciales:
  - Usuario: "user"
  - Contraseña: "user"
- Comandos:
  - `historico`: muestra la cantidad de usuarios que se conectaron al servidor smtp.
  - `actual`: muestra la cantidad de usuarios conectados al servidor smtp.
  - `mail`: muestra la cantidad de mails enviados.
  - `bytes`: muestra la cantidad de bytes enviados.
  - `status`: muestra el estado de las transformaciones
  - `transon`: activa las transformaciones
  - `transoff`: desactiva las transformaciones
  - `help`: muestra los comandos disponibles en el protocolo de supervision.
  - `max <cant>`: setea la cantidad maxima de usuarios que se pueden conectar.
  - `cant`: Muestra la cantidad maxima de usuarios que se pueden conectar.
- Conexion al servidor SMTP:
  - `nc -C localhost 1209`
- Conexion al protocolo de Supervision:
  - `nc -C -u localhost 6969`

## Ubicación de archivos

El archivo correspondiente al informe se encuentra en la carpeta `doc/`. El directorio generado `mails/` se crea en la raiz del proyecto.
