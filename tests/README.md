# Pruebas de SourceTV-Support

`make test-ci` comprueba procedencia de dependencias, esquema y contenido
del paquete, y el rechazo de entradas inválidas antes de publicar. Usa checkouts
temporales y dobles del publicador, sin descargas ni Sonar.

`make test-public-api` compara el contrato con la fixture congelada y también
se ejecuta desde `make test-ci`. Consulta el [baseline y la evidencia
ABI](native-baseline.md).

`make test-abi` compila SourceHook del checkout de Metamod fijado y ejecuta
un hook manual x86 con un proveedor independiente. Comprueba el slot, receptor,
argumentos por referencia, llamada al original y retirada del hook. Incluye
un slot vecino, una resolución parcial y una escritura de flags que conserva
canarios y bits altos. Requiere el checkout local y multilib de Linux.

`make test-native` ejecuta las políticas de retención de frames, transmisión
y HUD sobre código de producción. Comprueba resultados ordinarios, extremos
numéricos y poda desde el frame más antiguo. Incluye listas de edicts
desordenadas y los bytes de `PZDmgMsg`, con inicio fallido y cierre único.
La prueba de SRCDS cubre actualización de PVS, entidades ausentes y liberación
de frames al cambiar de mapa. En Linux usa un compilador C++17 con multilib
x86 por defecto. Se puede seleccionar el compilador mediante `CXX` y los flags
mediante `NATIVE_CXXFLAGS`. Cada ejecución crea su propio directorio en
`.build/` y guarda el compilador, flags y resultado. La suite pura se ejecuta en CI
mediante `make test-native-sanitized` con ASan y UBSan. Funciona sin SDK y
los proveedores controlados se prueban por separado del motor.

El [punto de entrada](../extension/extension.cpp) define la instancia y los
bindings compartidos. El [ciclo de vida de los hooks](../extension/hooks/runtime_hooks.cpp)
reúne las declaraciones de SourceHook y su registro. Los
[detours](../extension/hooks/detours.cpp) mantienen juntos los callbacks, los
trampolines y la tabla que controla su instalación y retirada. El orden de
headers de allocators de Windows se define en [extension.h](../extension/extension.h).

`make test-native` también ejecuta la serialización de tablas y clases con un
buffer controlado, un lector independiente de los campos y un contador de
escrituras. Cubre listas vacías, orden del payload, userdata, límites,
desbordamiento y destino no disponible. La parte de SendTables que escribe el
motor se representa con un marcador en la prueba.

El handler descarta el registro y deja un diagnóstico ante datos incoherentes,
falta de archivo disponible o desbordamiento. Conserva el límite de 2 MiB,
mantiene el estado del grabador y no invoca el serializador original como alternativa.
Las cantidades usan los mismos campos byte, word o short del formato previo.
Los campos escritos como short deben caber en el rango positivo con signo.
Una demo con un registro descartado puede quedar incompleta. La reproducción
en el cliente L4D2 requiere una prueba de runtime por plataforma.

`make test-native` también ejecuta la orquestación que usa la extensión
para preparar, activar, volver a enlazar HLTV y detener el runtime. Usa
dobles para inyectar fallos de adquisición y habilitación, comprobar la fase
y verificar el rollback y la parada repetida. Comprueba que se destruyan los
call wrappers mientras su proveedor sigue disponible y que el adaptador
de inicialización de Steam llame una vez al original, conserve su resultado y
respete el puerto de consulta, incluidos los extremos.

Para validar el ciclo de vida en SRCDS, comprobar el diagnóstico de
`sm exts info` ante bindings incompletos, descargar y recargar los proveedores
y verificar un reinicio de Steam con y sin SteamWorks, en ambos órdenes de
carga. Confirmar que el cierre de HLTV conserva la sesión Steam del servidor
principal y que este puede cerrarla al terminar.

`make test-vsp` abre primero el VSP Linux x86 con un proceso sin las
bibliotecas SDK precargadas y después prueba su `CreateInterface`.
Prueba una versión de callback desconocida, un factory ausente, un factory sin
`ICvar` y registros de cvars vacío y parcial. Usa `ICvar` y `ConVar` del SDK
fijado con un registro controlado. Comprueba el retorno `false`, los mensajes
de fallo y de salida prevista, y dos ejecuciones sobre las mismas cvars.
Solo se retira `FCVAR_DEVELOPMENTONLY` de las variables previstas. Se conservan
los valores, los demás flags y las variables ajenas.

Para verificar el orden de carga en SRCDS, instala el ZIP de la plataforma con
su VDF y arranca L4D2 x86 con `-hltv +tv_enable 1`. Registra el mensaje
`[STVS] VSP early setup complete` y los recuentos de cvars en el log. Comprueba
`tv_enable` y conecta como espectador durante el primer mapa, antes de cambiar
de nivel. El VSP solicita descargarse al terminar. Comprueba la extensión
persistente con `sm exts list`. Inicia y cierra una grabación, verifica que se
reproduce y repite la prueba de transmisión tras cambiar de mapa.
