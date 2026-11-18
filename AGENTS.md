# SourceTV-Support

Usa GNU Make desde la raíz para dependencias, compilación, pruebas, staging,
ZIP y validación. Consulta [el contrato de build](docs/build/README.md).
Conserva las revisiones del lock y las rutas de instalación del mapa
`scripts/package/package-map.json` al modificar el tooling.

- `extension/` contiene la extensión C++ y AMBuild es el backend nativo.
- `addons/` contiene los recursos que acompañan al binario.
- `scripts/` contiene los backends de Make y `tests/` las comprobaciones.
- `docs/` describe instalación, dependencias y mantenimiento.
- Usa `.deps/`, `.build/` y `dist/` para resultados locales.

Ejecuta `make test-ci` y valida el ZIP de cada plataforma que puedas compilar.
Conserva nombres de binarios, API, avisos y recursos. Registra pruebas reales y
límites de plataforma. Publicación y despliegue requieren autorización.
