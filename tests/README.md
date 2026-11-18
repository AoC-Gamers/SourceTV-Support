# Pruebas de SourceTV-Support

`make test-ci` ejecuta las pruebas del contrato de empaquetado.

En instalaciones de prueba de L4D2 x86, instala el ZIP de la plataforma y
conserva el VDF del juego. Arranca SRCDS con `-hltv +tv_enable 1`. Comprueba la
extensión con `sm exts list` y el VSP con `plugin_print`. Inicia y cierra una
grabación de SourceTV, comprueba que la demo se reproduce e intenta conectarte
desde un cliente espectador para validar la transmisión. Repite tras cambiar
de mapa y registra por juego, plataforma y versión de SourceMod.
