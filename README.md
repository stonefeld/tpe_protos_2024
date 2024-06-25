#Entrega del Trabajo Practico Especial de Protocolos de Comunicacion

##Integrantes
-Bendayan, Alberto (Legajo: 62786)
-Boullosa Gutierrez, Juan Cruz (Legajo: 63414)
-Quian Blanco, Francisco (Legajo: 63006)
-Stanfield, Theo (Legajo: 63403) 

##Docentes
-Codagnone, Juan Francisco
-Garberoglio, Marcelo Fabio
-Kulesz, Sebastian 

##Compilacion

##Protocolo SMTP
-Proposito: Enviar emails

-Comandos:
    EHLO
    HELO
    MAIL FROM
    RCPT TO
    DATA
    QUIT

##Protocolo de Supervisión
-Proposito: Brindar informacion del servidor SMTP.
-Credenciales:
    Usuario: "user"
    Contraseña: "user"

-Comandos:
    historico: muestra la cantidad de usuarios que se conectaron al servidor smtp.
    actual: muestra la cantidad de usuarios conectados al servidor smtp.
    mail: muestra la cantidad de mails enviados.
    bytes: muestra la cantidad de bytes enviados.
    help: muestra los comandos disponibles en el protocolo de supervision.


-Conexion al servidor SMTP:
    nc -C localhost 1209

-Conxion al protocolo de Supervision:
    nc -C localhost 6969

