# Gatitos Tec

Implementacion estatica del flujo principal de la interfaz de alimentacion remota, usando como base los nodos de Inicio, Dispensadores e Historial del diseno de Figma.

## Archivos

- `index.html`: estructura principal del flujo movil con vistas de Inicio, Dispensadores e Historial.
- `styles.css`: estilos compartidos del shell y de las tres pantallas del flujo.
- `script.js`: navegacion entre vistas, control manual, actualizacion de reserva y renderizado del historial.

## Vista local

Abre `index.html` directamente en un navegador. No requiere dependencias ni proceso de build.

## Notas

- La interfaz usa los assets remotos expuestos por Figma en `http://localhost:3845`.
- El entorno no tiene `node` ni `npm`, por eso la implementacion se resolvio con HTML, CSS y JavaScript sin dependencias.
- El flujo actual permite registrar una alimentacion en Inicio y verla reflejada de inmediato en Historial.
