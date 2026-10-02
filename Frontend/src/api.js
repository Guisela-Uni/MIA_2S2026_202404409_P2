const URL_BASE = 'http://localhost:8080'

export async function procesarArchivo(contenido) {
  const respuesta = await fetch(`${URL_BASE}/api/procesar`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ contenido }),
  })

  const datos = await respuesta.json()

  if (!respuesta.ok) {
    throw new Error(datos.error || 'Error desconocido del backend')
  }

  return datos.reporte
}
