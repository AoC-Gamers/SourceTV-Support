# Validación de SourceTV-Support en SRCDS

## Antes de probar

Usa un SRCDS de pruebas con las [dependencias de instalación](../installation/README.md).
Registra SO, versión y build de L4D2, versiones de Metamod:Source y SourceMod, revisión
disponible y SHA-256 de los ZIP y binarios cargados. Conserva logs, transcripciones y demos
fuera del instalador.

Para revisar una demo, copia el archivo `.dem` del servidor al directorio `left4dead2/` del
cliente y ejecuta `playdemo <nombre>` en su consola. Verifica la reproducción hasta el cierre.

## Escenarios

| Escenario | Resultado esperado |
| --- | --- |
| Primer mapa | Con el VDF de Support y `+tv_enable 1`, el log muestra el resultado del VSP y el recuento de cvars. **SourceTV Support** figura en `sm exts list`. Un espectador puede entrar en el primer mapa. |
| Transmisión y grabación | Con un jugador y un espectador, inicia y cierra una demo y comprueba que se reproduce. Repite tras un cambio de mapa y comprueba que SourceTV vuelve a emitir. |
| Convivencia | Carga Support y Manager en ambos órdenes. Descarga y recarga cada extensión mientras la otra sigue activa. Reconecta al espectador y comprueba mensajes locales, grabación y rechazo de pantalla dividida. |
| Dependencias | Retira y restablece BinTools y SDKTools en la instancia de pruebas. Comprueba que Support retira las llamadas a BinTools cuando se descarga y que el servidor conserva el estado esperado. |
| Steam | Comprueba el estado de Steam antes y después de `tv_stop`. Al cerrar SourceTV, el jugador y la sesión Steam del servidor principal deben seguir activos. Si la instalación usa SteamWorks, repite con las dos extensiones cargadas. Registra por separado el cierre de SRCDS. |

En Linux, `make test-vsp` comprueba el binario del VSP y su interfaz de carga con el SDK fijado.
`make test-abi` comprueba un hook manual de SourceHook con un proveedor controlado. Consulta las
[pruebas automatizadas](../../tests/README.md) para sus alcances y requisitos.
