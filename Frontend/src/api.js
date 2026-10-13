const URL_BASE = (import.meta.env.VITE_API_URL || 'http://localhost:8080').replace(/\/$/, '')

async function solicitud(ruta, opciones = {}) {
  const respuesta = await fetch(`${URL_BASE}${ruta}`, {
    cache: 'no-store',
    ...opciones,
  })

  let datos = {}
  const texto = await respuesta.text()
  if (texto) {
    try {
      datos = JSON.parse(texto)
    } catch {
      throw new Error(`La API devolvió una respuesta no válida (HTTP ${respuesta.status}).`)
    }
  }

  if (!respuesta.ok) {
    const mensaje = datos.error || datos.resultado || datos.message || `Error HTTP ${respuesta.status}`
    const error = new Error(mensaje)
    error.status = respuesta.status
    error.data = datos
    throw error
  }

  return datos
}

function enviarJson(ruta, metodo, datos = {}) {
  return solicitud(ruta, {
    method: metodo,
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(datos),
  })
}

export const URL_API = URL_BASE

export function comprobarApi() {
  return solicitud('/api/health')
}

export function obtenerEstado() {
  return solicitud('/api/estado')
}

export function obtenerSesion() {
  return solicitud('/api/sesion')
}

export function iniciarSesion(user, password, id) {
  return enviarJson('/api/login', 'POST', { user, password, id })
}

export function cerrarSesion() {
  return enviarJson('/api/logout', 'POST', {})
}

export function ejecutarComando(comando) {
  return enviarJson('/api/comando', 'POST', { comando })
}

export async function procesarArchivo(contenido) {
  const datos = await enviarJson('/api/procesar', 'POST', { contenido })
  return datos.reporte
}

export function listarDirectorio(path) {
  return enviarJson('/api/fs/listar', 'POST', { path })
}

export function leerArchivo(path) {
  return enviarJson('/api/fs/archivo', 'POST', { path })
}

export function obtenerJournaling(id) {
  return enviarJson('/api/journaling', 'POST', id ? { id } : {})
}
