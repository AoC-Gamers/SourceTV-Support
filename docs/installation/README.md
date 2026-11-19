# Instalación de SourceTV-Support

Usa un servidor dedicado de Left 4 Dead 2 x86. SourceMod 1.12 y Metamod:Source 1.12 son la
referencia de compilación. Conserva BinTools y SDKTools de SourceMod y usa el gamedata del mismo
paquete que el binario.

## Compatibilidad en Linux

El ZIP Linux de Develop inspeccionado se compiló con Ubuntu 26.04. La extensión y el VSP de ese
ZIP requieren el símbolo `GLIBC_2.43`. Ubuntu 24.04 incluye glibc 2.39. Si mantienes un servidor
en Ubuntu 24.04, compila el ZIP en un entorno compatible con ese servidor y comprueba la carga
de ambos binarios.
[Notas de Ubuntu 24.04](https://documentation.ubuntu.com/release-notes/24.04/#toolchain-upgrades).
[Ejecución del build inspeccionado](https://github.com/AoC-Gamers/SourceTV-Support/actions/runs/44411469644).

## Instalar y verificar

1. Descarga `sourcetvsupport-develop-linux.zip` o `sourcetvsupport-develop-windows.zip`, según
   el servidor.
   [Release Develop](https://github.com/AoC-Gamers/SourceTV-Support/releases/tag/channel/develop).
2. Detén SRCDS y copia el contenido de `addons/` del ZIP a `left4dead2/addons/` sin renombrar
   archivos.
3. Inicia SRCDS con `+tv_enable 1`. Para cambiar el puerto de espectadores, añade
   `+tv_port <puerto>` a la línea de arranque. El puerto predeterminado de SourceTV es UDP
   27020.
4. Ejecuta `sm exts list` y confirma que **SourceTV Support** figure cargado. Busca
   `[STVS] VSP early setup complete` en el log y verifica que un espectador pueda entrar en el
   primer mapa.

El ZIP incluye el binario de SourceMod, `sourcetvsupport.autoload`, su gamedata y el VSP.
Conserva `addons/sourcetvsupport_vsp_l4d2.vdf` y `addons/sourcetvsupport/bin/` para la
inicialización temprana. Consulta el
[funcionamiento de SourceTV](../sourcetv/README.md#carga-temprana).

## Actualizar o revertir

Detén SRCDS y guarda una copia de los archivos que reemplazarás. Instala juntos el binario y los
recursos del ZIP nuevo y repite las comprobaciones de carga y funcionamiento. Para revertir,
detén SRCDS, restaura el conjunto anterior y retira únicamente los archivos añadidos por la
actualización.

Para compilar desde fuentes, consulta la [guía de compilación](../build/README.md).
