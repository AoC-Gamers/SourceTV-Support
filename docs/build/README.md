# Compilación y distribución

Usa GNU Make, Git y Python 3. En Linux necesitas GCC/G++ con multilib x86.
En Windows necesitas Visual Studio 2022 con MSVC x86 y Bash de Git para Windows.
Consulta las [dependencias fijadas](dependencies.md).

```sh
make deps-exts-linux
make build-exts-linux
make package-exts-linux
make release-linux
make validate-artifact-linux
make test-ci
```

En Windows usa los targets con sufijo `-windows` y `PYTHON=python` si tu
instalación expone Python con ese nombre. Cada fase tiene su propio target.
Ejecuta las fases en el orden indicado y repite el build si cambian las fuentes.
Make usa `.deps/exts-<plataforma>/` para dependencias y `.build/` para AMBuild.
El staging queda en `dist/<plataforma>/artifact/` y el ZIP en `dist/release/`.
`RELEASE_CHANNEL` acepta `develop` (por defecto) y `latest`.

## CI

La matriz de build toma los runners de las variables de organización
`L4D2_EXT_LINUX_RUNNER` y `L4D2_EXT_WINDOWS_RUNNER`. Si alguna variable
requerida falta, está vacía o solo contiene espacios, la validación
previa falla con un mensaje antes de asignar los runners de compilación.

GitHub Actions ejecuta las pruebas del contrato y compila en Linux y Windows.
El recibo de compilación vincula las fuentes, las dependencias efectivas y
los binarios mediante SHA-256. El ZIP incluye el README del proyecto e
identifica la revisión o las fuentes locales usadas. Los enlaces a la guía
de compilación apuntan al commit base cuando el build contiene cambios locales.
Los ZIP de runs de `develop` usan el canal Develop. Los runs de `master` o
`main` usan Latest. Actions conserva los ZIP como artefactos verificables
del run. El target `release-*` genera el archivo local. Los tags `v*` generan
ZIP versionados. Los PR y otras ramas usan el SHA corto como identificador.

Cada push a `develop` publica ambos ZIP del mismo run en la prerelease
`channel/develop` después de validar los paquetes de Linux y Windows.
La ejecución manual con `publish_release` también permite actualizar
`channel/develop`, actualizar `channel/latest` desde `master` o `main`,
y publicar un tag versionado. Las actualizaciones de canal pueden dejar una
pausa entre la retirada de la release anterior y la publicación de la nueva.
Un fallo de subida se reintenta desde Actions.

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
SonarQube con sonar-cxx, `SONAR_HOST_URL` y `SONAR_TOKEN`. Usa los archivos de
ejemplo para configurar el análisis. El informe refleja análisis estático.
`make test-package-coverage` mide las pruebas del empaquetador con coverage.py
y genera el informe local en `.build/package-coverage/`. El análisis en el
servidor SonarQube y la cobertura nativa requieren sus propias ejecuciones. CI ejecuta las pruebas del producto sin llamar a Sonar.

Las compilaciones completas de CI necesitan acceso de lectura al SDK 1.55.
Los pull requests ejecutan las pruebas del contrato sin esa credencial.
