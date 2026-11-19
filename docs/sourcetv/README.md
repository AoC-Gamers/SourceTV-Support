# Funcionamiento de SourceTV

Support aplica correcciones al servidor SourceTV de Left 4 Dead 2. La extensión de SourceMod
aplica las correcciones en ejecución y el Valve Server Plugin (VSP) prepara las cvars del motor
antes del primer mapa.

## Carga temprana

El motor usa `addons/sourcetvsupport_vsp_l4d2.vdf` para cargar el VSP. Este retira
`FCVAR_DEVELOPMENTONLY` de las cvars de SourceTV y auxiliares que encuentra y registra cuántas
estaban disponibles y cuántas faltaban.

`[STVS] VSP early setup complete` indica que terminó el trabajo y solicitó descargarse. Por eso
el VSP puede quedar fuera de `plugin_print`. `[STVS] VSP setup failed` indica que no pudo
acceder a la interfaz de cvars del motor. Confirma que el servidor arrancó con
`+tv_enable 1` y que acepta espectadores desde el primer mapa.

## Correcciones del servidor

- **Transmisión.** Corrige la actualización de tablas y de la visibilidad para el espectador. El
  bot SourceTV se ubica en el equipo espectador y queda fuera del conteo de jugadores y
  votantes.
- **Conexión.** Permite la conexión directa de espectadores sin depender del sistema de lobbies.
  El tratamiento de la desconexión y de los eventos distingue a los clientes SourceTV de los
  jugadores.
- **Grabación.** Corrige problemas al iniciar demos temprano, incluido el uso de
  `tv_autorecord`. Si un registro de tablas o clases contiene datos inválidos o excede el
  buffer, se omite el registro y se escribe un diagnóstico.
- **Ciclo de vida.** Corrige fugas durante la hibernación y problemas de transición de mapa.
  Controla la sesión Steam de HLTV para conservar la del servidor de juego al cerrar SourceTV.

## Dependencias en ejecución

BinTools permite construir llamadas a funciones del motor. Cuando se retira BinTools, Support
destruye esas llamadas antes de que cierre el proveedor. Support usa SDKTools para obtener
`IServer` al arrancar. El motor conserva la propiedad de ese objeto.

Support mantiene una referencia a `steam_api` mientras sus hooks la necesitan. Tras un inicio de
Steam exitoso, actualiza el hook de `LogOff` para el contexto disponible. Si Steam aún no tiene
contexto, las demás correcciones siguen activas y Support vuelve a intentar enlazar en la
próxima inicialización exitosa.

## Limitaciones

El modo relay sigue fuera del funcionamiento previsto de esta distribución. En las demos pueden
aparecer brazos invisibles o accesorios que parpadean por limitaciones del cliente.

Consulta la [guía de instalación](../installation/README.md) y los
[escenarios de validación en SRCDS](../testing/README.md).
