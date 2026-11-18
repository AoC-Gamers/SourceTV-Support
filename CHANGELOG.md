# Changelog

## [Unreleased]

### Cambiado

- **Distribución.** ZIP Linux y Windows para L4D2 x86 con la extensión, el VSP y
  los recursos de carga. El ZIP incluye el README del proyecto y se verifica
  contra los binarios y dependencias de la compilación.

### Corregido

- **Demos.** Un registro de tablas o clases que excede el buffer o contiene
  datos inválidos se descarta con un diagnóstico antes de escribir al archivo.

- **Transmisión.** La actualización de PVS de jugadores para SourceTV
  admite listas de edicts desordenadas. La retención de frames acota entradas
  extremas y los mensajes de daño omiten los valores que exceden su formato.

- **Carga.** Un fallo de arranque retira los hooks y bindings adquiridos.
  La biblioteca se anuncia tras activar las correcciones, y los reinicios
  de Steam sustituyen el hook de `LogOff` por el del contexto vigente.

- **Bindings.** La carga identifica y rechaza campos, subobjetos o slots
  obligatorios ausentes o con valores negativos antes de activar los hooks.

- **VSP.** El log de arranque distingue el fallo de la interfaz de cvars de
  la salida prevista y resume cuántas cvars estaban disponibles o faltaban.
  En Linux, la carga temprana resuelve sus símbolos de tier0 directamente.

### Eliminado

- **INCOMPATIBLE.** Se retira Left 4 Dead del build y de los instaladores. Usa este ZIP únicamente con Left 4 Dead 2.

### Desarrollo

- Cada push a `develop` publica la prerelease con los ZIP validados de Linux y Windows.

- GNU Make centraliza la preparación de dependencias, la compilación, las pruebas y el empaquetado local y en CI.
