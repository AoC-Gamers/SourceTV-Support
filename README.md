# SourceTV-Support

Extensión de SourceMod que corrige problemas de transmisión, conexión de espectadores y grabación de demos de SourceTV en servidores dedicados de Left 4 Dead 2.

Gracias a Evgeniy «shqke» Kazakov y a quienes contribuyeron al [proyecto original `shqke/sourcetvsupport`](https://github.com/shqke/sourcetvsupport). El trabajo de AoC-Gamers se centra en la calidad del código, las pruebas y la distribución de paquetes instalables para Linux y Windows.

> **Linux x86.** Los ZIP Develop de AoC verificados se compilaron con Ubuntu 26.04. El paquete Develop verificado requiere glibc 2.43, superior a la 2.39 incluida en Ubuntu 24.04. Consulta los [requisitos del servidor](docs/installation/README.md#compatibilidad-en-linux).

## Instalación rápida

1. Usa un servidor dedicado de Left 4 Dead 2 de 32 bits con Metamod:Source y SourceMod. Las versiones de referencia son 1.12 de ambos.
2. Descarga el ZIP de Linux o Windows desde [Releases](https://github.com/AoC-Gamers/SourceTV-Support/releases).
3. Con el servidor detenido, copia el árbol `addons/` del ZIP a `left4dead2/`, conservando las rutas.
4. Inicia el servidor con `+tv_enable 1`. Verifica la extensión con `sm exts list` y busca `[STVS] VSP early setup complete` en el log de arranque.

## Documentación

- [Guía de instalación](docs/installation/README.md) con verificación, actualización y reversión.
- [Funcionamiento de SourceTV](docs/sourcetv/README.md).
- [Índice de documentación](docs/README.md) con compilación, dependencias y pruebas.

Consulta la [licencia](LICENSE), las [atribuciones](NOTICE.md) y el [historial de cambios](CHANGELOG.md).
