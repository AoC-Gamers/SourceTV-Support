# Baseline de API y evidencia ABI

## Fuentes y contrato público

Baseline de `develop`: `02e6f395baba81710c302463a2348b39ad520d90`, el mismo
SHA fijado en el informe. SteamWorks `1cd402268d8a0ba6f98c3a5463747fc2390d49fb`
sirve de referencia para pruebas y para el futuro análisis de convivencia.

[La fixture congelada](fixtures/public-contract.json) fue tomada del árbol Git
anterior a la extracción. Registra la biblioteca `sourcetvsupport`, ausencia
de registro de natives, cuatro cvars observadas por la extensión, el punto de
entrada del VSP y sus 24 cvars en orden. El VSP obtiene `ICvar`, retira el
flag de desarrollo de las cvars que encuentra y devuelve falso al terminar.
La comprobación de su efecto temprano en runtime queda pendiente.

| Unidad | Responsabilidad y contratos sensibles |
|---|---|
| [Extensión](../extension/extension.cpp) y [header](../extension/extension.h) | Interfaces, activación y teardown, hooks, transmisión, serialización y Steam |
| [Wrappers](../extension/wrappers.h) y [headers de motor](../extension/sdk/engine/clientframe.h) | Bases y slots, netprops, frames, allocators y archivo de demo |
| [VSP](../vsp/plugin_vsp.cpp) | Factory y exposición temprana de cvars |
| [AMBuilder de extensión](../extension/AMBuilder), [AMBuilder del VSP](../vsp/AMBuilder) y [gamedata](../addons/sourcemod/gamedata/sourcetvsupport.txt) | Unidades por producto y resolución por plataforma |

El [mapa de paquete](../scripts/package/package-map.json) define ambos
binarios, VDF, gamedata y `.autoload`. El [lock](../scripts/deps/build-deps.lock)
fija HL2SDK, SourceMod, Metamod y AMBuild. SafetyHook proviene de SourceMod.
El [lock de SDK](../scripts/build/steamworks-headers.lock.json) fija los
headers de Steamworks 1.55. En el HL2SDK fijado, `public/ihltv.h` y
`public/iserver.h` distinguen `IHLTVServer` e `IServer`.
`GetBaseServer()` declara un retorno `IServer*`.

## Evidencia del motor por plataforma

Se inspeccionó estáticamente una copia local de `engine_srv.so` procedente
de una captura de SteamWorks. `readelf` confirmó ELF32 i386 y build ID
`b152439170af081a0d31fc357364a4cd4caa56ac`. SHA-256
`8f8093a18bba14cb73fc11589687bfb8156b00328b7413d697f1a76ed4b7621a`.
La versión del juego del despliegue actual queda por cotejar. Para Windows
queda pendiente obtener `engine.dll` PE x86 identificada con SHA-256 y versión
del juego. La columna Windows describe evidencia de fuente.

| Contrato | Linux x86 | Windows x86 | Verificación pendiente |
|---|---|---|---|
| `ConnectClient` y contenedor | `nm -C` muestra el método de `CHLTVServer` y un thunk con vector de objetos `CLC_SplitPlayerConnect`. El hook usa `CUtlVector<NetMessageCvar_t>&` sobre la ruta de servidor base. | Declaración, registro y selector en fuentes. Módulo sin identificar. | Verificar receptor, slot del servidor base, tipo real de elementos, calling convention y retorno. Correlacionar antes de usar los elementos. |
| `IServer`, `IHLTVServer`, `CHLTVServer` e interfaces de cliente | El símbolo `GetBaseServer` y su thunk existen. SDK, wrappers y ajustes en gamedata son observaciones distintas. | Hay SDK y ajustes de gamedata. Módulo sin identificar. | Trazar cada conversión con el objeto o subobjeto de entrada y comprobar destinatario y lifetime. |
| `CHLTVDemoRecorder`, `CDemoFile` y buffer | Los símbolos muestran `RecordStringTables`, `RecordServerClasses` y los dos writers, que reciben `bf_write*` y tick. | Headers y resolución en fuentes. Módulo sin identificar. | Confirmar campos, slots, longitudes implicadas y ciclo de vida, con oráculo independiente para el formato. |
| `SVC_ServerInfo` | Hay símbolos `FillServerInfo` de `CBaseServer` y `CHLTVServer`, más thunk en este último. El header local declara el campo que modifica el handler. | Declaración local y registro de ruta base. Módulo sin identificar. | Confirmar layout, ancho del campo, receptor y llamada de cada ruta. |
| `CClientFrameManager`, `CClientFrame`, pool y `PackedEntity` | Símbolos de `AddNewFrame`, `CountClientFrames` y `RemoveOldestFrame`. La política C++ probada solo calcula el límite. | Layouts, allocators y gamedata en fuentes. Módulo sin identificar. | Fingerprint del módulo y verificación del layout, refcounts, destructor y correspondencia del CRT. |
| `HitAnnouncement` y `CBasePlayer::m_fFlags` | Declaraciones y resolución SourceMod observadas en fuente. Falta comprobar las representaciones efectivas. | Misma evidencia a nivel de fuente. | Contrastar campos, anchos, notificaciones y ciclo de vida con la versión de cada plataforma. |
| Steam init, puertos y LogOff | Declaraciones del SDK y consumidores en fuentes. | Declaraciones del SDK y consumidores en fuentes. | Identidad y versión de biblioteca y objeto, disponibilidad tras el teardown y cadena de originales cuando SteamWorks coexiste. |

Los símbolos respaldan nombres y los parámetros que codifican. La aceptación
de invocaciones, layouts y efectos requiere módulos identificados por plataforma,
probes x86 con proveedores independientes y ensayos acotados de SourceHook,
BinTools y SRCDS. Un perfil de Windows no se deriva del binario Linux.

## Comprobaciones locales

| Nivel | Resultado |
|---|---|
| `make test-ci` | 15 pruebas de paquete y una comparación de contrato público |
| `make test-native` | 6 casos de retención con resultados esperados explícitos, GCC 13.3.0, C++17 x86. Una mutación aislada que elimina el mínimo hizo fallar el caso de snapshot rate cero. |
| Build y paquete Linux | `build-exts-linux`, `package-exts-linux`, `release-linux` y `validate-artifact-linux` pasaron sobre la extracción. |
| Windows, SourceHook/BinTools, VM y SRCDS | Pendientes para la extracción. |


## Bindings tipados de gamedata

La resolución distingue desplazamientos de campo, desplazamiento del
subobjeto e índices de vtable. Un valor negativo o una clave ausente impide
la carga y el error identifica la entrada. Cero sigue siendo un valor válido.
Los campos se publican después de resolver las firmas y las direcciones. El
offset de `m_fFlags` lo sigue proporcionando SourceMod mediante
`actual_offset`; la escritura conserva los 32 bits y notifica al edict.

| Contrato | L4D2 Linux x86 | L4D2 Windows x86 |
|---|---|---|
| `CHLTVServer::CBaseServer` | `4`, correlacionado con `GetBaseServer()` y con el encabezado de la vtable secundaria del ELF identificado arriba. | `8`, valor existente en gamedata. PE pendiente de corroborar. |
| `GetChallengeNr`, `GetChallengeType`, `ReplyChallenge` | Slots `54`, `55`, `60`. En la tabla del subobjeto se observan `CBaseServer::GetChallengeNr`, el thunk de `CHLTVServer::GetChallengeType` y `CBaseServer::ReplyChallenge`. | Slots `53`, `54`, `59`, valores existentes en gamedata. PE pendiente de corroborar. |
| `m_fFlags` | El harness comprueba la escritura de cuatro bytes con canarios. La escritura y notificación en una entidad SRCDS quedan pendientes. | Mismo acceso fuente y resolución de SourceMod. El campo real queda pendiente de contrastar. |

El probe de SourceHook usa dos slots de prueba y verifica que un índice
vecino no intercepte el método esperado. Los métodos de desafío, el envío de
la respuesta y el efecto sobre la entidad requieren verificación en SRCDS.
Se conservan las declaraciones actuales de `ConnectClient`, los mirrors de
demo y frames y la resolución relativa de Windows.
