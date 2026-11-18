# Aceptación de SourceTV en SRCDS

Prueba cada plataforma en una instalación aislada con Metamod y SourceMod.
Registra la versión de L4D2, la plataforma, la versión de SourceMod y las
huellas de los módulos del juego y de los ZIP instalados. Conserva logs y demos
de pruebas fuera del paquete.

| Escenario | Comprobación |
| --- | --- |
| Primer mapa | Con el VDF de Support y `-hltv +tv_enable 1`, verifica el diagnóstico del VSP y que SourceTV emita en ese mapa. Comprueba ambas extensiones con `sm exts list`. |
| Coexistencia | En una sesión carga Manager antes de Support y en otra Support antes de Manager. Descarga y recarga cada una con la otra presente. Comprueba grabación, conexión de un espectador, mensajes locales y rechazo de pantalla dividida. |
| Grabación y cambio de mapa | Inicia una demo, toma un snapshot, intenta pararla con un token ajeno y confirma que sigue activa. Detén con el propietario. Cambia de mapa, verifica la nueva instancia y reproduce las demos en un cliente L4D2 compatible. |
| Cambio de dependencias | En la instancia de pruebas, retira y restablece BinTools y SDKTools. Comprueba disponibilidad, reenganche de hooks y ausencia de accesos al proveedor descargado. |
| Steam | Repite los escenarios con SteamWorks si forma parte de la instalación. Comprueba inicialización, reinicio y `LogOff`. Cerrar HLTV debe conservar la sesión del servidor principal mientras este siga activo. |

En Linux, `make test-vsp` valida el binario y el factory con el SDK fijado.
`make test-abi` prueba un hook manual con SourceHook y un proveedor controlado.

Las pruebas con proveedores controlados validan los escenarios del harness.
Para aceptar la coexistencia completa, registra por separado los escenarios
ejecutados en SRCDS Linux y Windows y los que queden pendientes.
