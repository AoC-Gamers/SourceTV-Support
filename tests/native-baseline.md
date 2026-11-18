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
La copia corresponde a L4D2 2.2.4.3, build 10097. Para Windows se
identificó una DLL del cliente. Falta el módulo de SRCDS dedicado Windows con
SHA-256 y versión del juego. La columna Windows describe evidencia de fuente.

| Contrato | Linux x86 | Windows x86 | Verificación pendiente |
|---|---|---|---|
| `ConnectClient` | Slot 51, ajuste del thunk de −4 y argumentos de pila i386. El binding usa dirección mutable y vector de objetos. | Declaración y selector heredados. ABI dedicada pendiente. | Ejecutar los dos handlers con el motor y un cliente y contrastar el perfil Windows por separado. |
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

### Correlación de receptor y slots en el ELF identificado

En L4D2 2.2.4.3, build 10097, el ELF anterior tiene `ConnectClient` en
el slot 51 del subobjeto `CBaseServer`. La entrada de `CHLTVServer` en
`0x2a2620` apunta al thunk `0x1645e0`, que resta 4 al receptor en la pila.
`CHLTVServer::ConnectClient` suma 4 al receptor antes de llamar a
`CBaseServer::ConnectClient`. El desensamblado muestra parámetros en la pila
y retorno en EAX. El símbolo identifica referencias a `netadr_s` mutable y
a un vector de objetos `CLC_SplitPlayerConnect`. Se reconfirmaron el hash,
el build ID y los símbolos con `sha256sum`, `readelf` y `nm`. En el
desensamblado, `CBaseServer::ConnectClient` carga el noveno argumento de
`0x2c(%ebp)` y compara su contador en `0xc` (`0xfb07b` y `0xfb088`).
Coincide con el prefijo de `CUtlVector` del SDK x86 fijado. Dirección y
vector son préstamos durante la llamada. Los hooks dejan al motor el
ownership del cliente retornado y del almacenamiento del vector.

El proveedor independiente x86 de `make test-coexistence` en Manager
valida los diez argumentos, el retorno y la continuación de SourceHook.
Ese probe usa su propia vtable y un prefijo de vector de prueba. La
convivencia de los dos handlers productivos y el uso de un cliente real
siguen pendientes. Ninguno de los dos bindings requiere acceder a los
elementos del vector.

En la vtable de `CHLTVDemoRecorder`, el address point `0x2a16e8` contiene
`GetDemoFile` en el slot 0, `GetRecordingTick` en el 1, `StartRecording`
en el 2, `IsRecording` en el 4 y `StopRecording` en el 7. Los slots 12 y 13
son `RecordServerClasses` y `RecordStringTables`. `GetDemoFile` devuelve
el subobjeto en `this + 4`, que también recibe `CDemoFile::Close`.
`CDemoFile::Open` copia el nombre al offset 0 con límite de 260 bytes y
usa el stream buffer en `0x534` en esa copia del motor. Se requiere
corroboración separada del tamaño completo de las estructuras y frames.
El recorder conserva la propiedad de `CDemoFile`. La extensión usa su
dirección prestada durante la operación y conserva por copia los datos de
la notificación de cierre que deban sobrevivir a esa operación.

La DLL PE32 x86 de la instalación local del cliente lleva SHA-256
`8a6d468059394bf4cfce32f11dcba12ef45de88da1373084d5a2916d0bf7ba86`. El
contrato del SRCDS dedicado Windows sigue pendiente de identificar. Se
conservan las firmas Windows y los offsets de gamedata de ambas plataformas.

## Resultados históricos de la extracción

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
Se conservan los mirrors de demo y frames y la resolución relativa de Windows.
