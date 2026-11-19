# Compilación y distribución

Ejecuta GNU Make desde la raíz del repositorio. Necesitas Git y Python 3. En Linux necesitas
GCC/G++ con multilib x86.
En Windows necesitas Visual Studio 2022 con MSVC x86 y Bash de Git para Windows.
Consulta las [dependencias fijadas](dependencies.md).

Antes de compilar Support, prepara Steamworks SDK 1.55 en `sdk_155/` o indica su ruta con
`STEAMWORKS_SDK_DIR`.

```sh
make deps-exts-linux
make build-exts-linux
make package-exts-linux
make release-linux
make validate-artifact-linux
make test-ci
```

En Windows, usa los targets con sufijo `-windows` desde Bash de Git para Windows. Añade
`PYTHON=python` si el intérprete está disponible con ese nombre. Cada fase tiene su propio
target.
Ejecuta las fases en el orden indicado y repite el build si cambian las fuentes.
Make usa `.deps/exts-<plataforma>/` para dependencias y `.build/` para AMBuild.
El staging queda en `dist/<plataforma>/artifact/` y el ZIP en `dist/release/`.
Para los canales de distribución, usa `RELEASE_CHANNEL=develop` (predeterminado) o
`RELEASE_CHANNEL=latest`. Mantén el mismo valor al generar y validar el ZIP.

## CI

La matriz de build toma los runners de las variables de organización
`L4D2_EXT_LINUX_RUNNER` y `L4D2_EXT_WINDOWS_RUNNER`. Si alguna variable
requerida falta, está vacía o solo contiene espacios, la validación
previa falla con un mensaje antes de asignar los runners de compilación. Los últimos ZIP Develop
verificados se construyeron con `ubuntu-26.04` y `windows-2022`. Consulta la
[compatibilidad en Linux](../installation/README.md#compatibilidad-en-linux).

Los pull requests ejecutan las pruebas del contrato. Las compilaciones Linux y Windows requieren
acceso de lectura al SDK de Steamworks.
El recibo de compilación vincula las fuentes, las dependencias efectivas y
los binarios mediante SHA-256. El ZIP incluye el README del proyecto e
identifica la revisión o las fuentes locales usadas. Los enlaces a `docs/` apuntan a la revisión
del build. Si hay cambios locales,
los enlaces usan el commit base.
Los ZIP de runs de `develop` usan el canal Develop. Los runs de `master` o
`main` usan Latest. Actions conserva los ZIP como artefactos verificables
del run. El target `release-*` genera el archivo local. Los tags `v*` generan
ZIP versionados. Los ZIP que se generen en otras ramas usan el SHA corto como identificador.

Cada push a `develop` publica ambos ZIP del mismo run en la prerelease
`channel/develop` después de validar los paquetes de Linux y Windows.
La ejecución manual con `publish_release` también permite actualizar
`channel/develop`, actualizar `channel/latest` desde `master` o `main`,
y publicar un tag versionado. Las actualizaciones de canal pueden dejar una
pausa entre la retirada de la release anterior y la publicación de la nueva.
Si falla la subida, puedes reintentar la ejecución de Actions.

## Cobertura local

Después de preparar las dependencias del build Linux, ejecuta
`make test-coverage COVERAGE_PYTHON=/ruta/al/python` con GCC/gcov x86 y
coverage.py 7.13.5 disponible en ese intérprete. Usa el mismo `BUILD_DIR`
al generar el recibo y al analizar. Las mediciones C++ y Python se guardan
por separado en `.build/coverage/`. El baseline C++ conserva con cero hits
las líneas ejecutables del build que las pruebas nativas no alcanzaron.
La prueba Python incluye los subprocesos de las pruebas de paquete.

Para adjuntar las mediciones al análisis explícito, configura
`SONAR_COVERAGE_RECEIPT` con la ruta de `receipt.json`. El backend comprueba
las huellas de fuentes, pruebas, headers transitivos, submódulos, órdenes
efectivas, toolchain e informes antes de procesar el recibo y antes de lanzar
el scanner. Usa `NATIVE_CXXFLAGS` para seleccionar flags de las pruebas
nativas. La cobertura añade sus propios flags a ese perfil. Si usas un GCC
distinto, indica el gcov correspondiente mediante `GCOV`.
`make test-coverage-tooling` verifica el recibo y la importación simulada.
El análisis y la cobertura quedan fuera de CI y del ZIP.

Para incluir una sesión Linux x86 de SRCDS en la medición, usa un segundo
directorio de build y un servidor de pruebas aislado.

```bash
make build-coverage-linux BUILD_DIR="$PWD/.build/runtime-coverage"
```

Instala en esa instancia los binarios y recursos de `package/addons/` del build
instrumentado. Ejecuta los escenarios de [aceptación](../testing/README.md) que
se quieran medir y cierra SRCDS normalmente para escribir los contadores GCC.
Si usas `GCOV_PREFIX`, devuelve cada archivo `.gcda` a la ruta de su `.gcno`
en el build que lo generó. Conserva logs del escenario y usa un directorio nuevo
para cada candidato. Para incluir la prueba autónoma del VSP en los mismos
contadores, ejecuta `make test-vsp COVERAGE_BUILD=1 BUILD_DIR="$PWD/.build/runtime-coverage"`.

```bash
make test-coverage COVERAGE_RUNTIME_BUILD="$PWD/.build/runtime-coverage"
```

`BUILD_DIR` sigue señalando el build Linux normal en esta última orden.
La medición verifica que las fuentes, dependencias y órdenes coincidan con el
build instrumentado, salvo los flags de cobertura. El empaquetador rechaza
el build instrumentado.

## Sonar local

Tras un build Linux, `make sonar-cppcheck` usa las órdenes reales de compilación
L4D2 x86 y escribe el informe en `.build/sonar/`. Instala Cppcheck con soporte
para bases de compilación. `make sonar` añade SonarScanner y requiere un servidor
SonarQube con sonar-cxx, `SONAR_HOST_URL` y `SONAR_TOKEN`. Configura el análisis con
[`.env.example`](../../.env.example) y [`.env.secret.example`](../../.env.secret.example) como
referencia. El informe refleja análisis estático.
`make test-package-coverage` mide las pruebas del empaquetador con coverage.py
y genera el informe local en `.build/package-coverage/`. El análisis en el
servidor SonarQube y la cobertura nativa requieren sus propias ejecuciones. CI ejecuta las
pruebas del producto sin llamar a Sonar.
