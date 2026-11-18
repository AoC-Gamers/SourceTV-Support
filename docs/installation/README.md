# Instalación de SourceTV-Support

El paquete contiene la extensión de SourceMod, el VSP y los recursos de carga
para Left 4 Dead 2 x86. Extrae el ZIP de la plataforma del servidor en
`left4dead2/`.

1. Instala Metamod:Source y SourceMod.
2. Conserva `addons/sourcetvsupport_vsp_l4d2.vdf` para cargar el VSP.
3. Usa `-hltv` para habilitar transmisiones y `+tv_enable 1` para habilitar
   SourceTV desde el primer mapa. Configura `tv_port` si necesitas otro puerto.
4. Confirma la carga de la extensión con `sm exts list` y del VSP con
   `plugin_print` en la consola del servidor.

Consulta el [repositorio de AoC](https://github.com/AoC-Gamers/SourceTV-Support)
para consultar los artefactos y detalles de compatibilidad.

## Actualización y reversión

Detén SRCDS y guarda los binarios y recursos SourceTV de la instalación actual.
Extrae el ZIP nuevo sobre el directorio del juego y repite la comprobación de
carga. Para revertir, detén el servidor, restaura esos archivos desde la copia y
retira los archivos añadidos por la actualización antes de volver a iniciar SRCDS.
