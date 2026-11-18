# Dependencias

[`scripts/deps/build-deps.lock`](../../scripts/deps/build-deps.lock) fija
SourceMod y Metamod:Source 1.12, el HL2SDK de L4D2 y AMBuild 2.2.
SourceTV-Support usa el manifiesto de HL2SDK, AMTL, SourcePawn y SafetyHook
fijados por los submódulos de SourceMod. AMBuild enlaza SafetyHook estáticamente.

El build requiere Steamworks SDK 1.55. Coloca la carpeta `sdk_155/` en la raíz
del repositorio o configura `STEAMWORKS_SDK_DIR` con su ruta.
El backend verifica los headers contra
[`steamworks-headers.lock.json`](../../scripts/build/steamworks-headers.lock.json).

El job de build usa el secreto `STEAMWORKS_SDK_TOKEN` con acceso de lectura al SDK de AoC. Los pull requests
ejecutan `make test-ci`. El build completo está disponible en pushes a ramas
de integración y ejecuciones manuales de Actions.

## Verificación del build y del paquete

Los SDK de Git y AMBuild deben ser checkouts completos de las revisiones del
lock, con submódulos inicializados y el árbol limpio, incluidos archivos
ignorados. Un directorio de fuentes sin metadatos Git no acredita esas
revisiones. El build ejecuta AMBuild desde su checkout fijado con el intérprete
de `VENV_DIR`. Si usas rutas alternativas (`SOURCEMOD_DIR`, `MMSOURCE_DIR`,
`HL2SDK_DIR` o `AMBUILD_DIR`), pásalas también a los targets de empaquetado
y validación. Los recibos antiguos deben regenerarse con un nuevo build.

Aplica la misma regla de rutas a `STEAMWORKS_SDK_DIR`. El inventario de
`public/steam/` conserva la comprobación de headers con normalización CRLF/LF.
La extensión usa SafetyHook directamente desde el checkout de SourceMod.

## Uso y distribución

| Dependencia | Uso | Distribución |
| --- | --- | --- |
| SourceMod 1.12 y Metamod:Source 1.12 | SDK de compilación y prerrequisitos del servidor | Instalaciones externas |
| HL2SDK del juego | Headers y bibliotecas de enlace según la plataforma | El SDK se obtiene mediante el lock y queda fuera del ZIP |
| AMBuild y Python | Configuración, compilación y empaquetado | Herramientas de desarrollo |
| SafetyHook, Zydis y Zycore | Código incorporado por enlace estático | Avisos en `NOTICE.md` |
| Steamworks SDK 1.55 | Headers del soporte SourceTV | Fuera del ZIP; hashes comprobados en el build |

Los commits concretos están en el lock o en los submódulos asociados. Los
resultados de compilación describen la matriz probada del toolchain. La
compatibilidad mínima de carga se comprueba por juego y versión en SRCDS.


## Ciclo de vida en el servidor

BinTools mantiene los call wrappers durante la ejecución. Si se descarga,
Support los destruye en la notificación de SourceMod, antes de que se cierre
el módulo de BinTools. SDKTools solo resuelve `IServer` durante el arranque.
El motor es dueño del servidor, por lo que Support puede seguir activo si
SDKTools se descarga después de que el puntero quede resuelto.

Support retiene una referencia a la biblioteca `steam_api` del juego desde
que resuelve sus símbolos hasta que retira los hooks y detours. Steam conserva
la propiedad de las interfaces. Un inicio de Steam exitoso reemplaza el hook
de `LogOff` por el del contexto que se pueda resolver. Si el original falla,
Support retira el hook anterior y propaga el fallo. Si Steam aún carece de
contexto, Support activa el resto de sus correcciones y vuelve a intentar el
binding en la próxima inicialización exitosa. El puerto de consulta conserva
la regla existente, incluido el valor reservado al compartir el socket de juego.
