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

La ejecución manual con `publish_release` publica ambos ZIP del mismo run,
una vez validados Linux y Windows. `develop` actualiza `channel/develop` como
prerelease. `master` y `main` actualizan `channel/latest`. Las actualizaciones
de canal pueden dejar una pausa entre la retirada de la release anterior y
la publicación de la nueva. Un fallo de subida se reintenta desde Actions.

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
