import { useState } from 'react'
import { procesarArchivo } from './api.js'

function App() {
  const [comandos, setComandos] = useState('')
  const [resultados, setResultados] = useState('')
  const [procesando, setProcesando] = useState(false)
  const [archivoSeleccionado, setArchivoSeleccionado] = useState('')

  async function manejarProcesar() {
    if (!comandos.trim() || procesando) return

    setProcesando(true)
    setResultados('Procesando...')

    try {
      const reporte = await procesarArchivo(comandos)
      setResultados(reporte)
    } catch (error) {
      setResultados(
        `[ERROR DE CONEXION] ${error.message}\n\n¿Esta corriendo el servidor de la API? (api_server, puerto 8080)`
      )
    } finally {
      setProcesando(false)
    }
  }

  function limpiar() {
    if (procesando) return
    setComandos('')
    setResultados('')
    setArchivoSeleccionado('')
  }

  function manejarSubirSmia(evento) {
    const archivo = evento.target.files?.[0]
    if (!archivo) return

    if (!archivo.name.toLowerCase().endsWith('.smia')) {
      setResultados('[ERROR] Selecciona un archivo de comandos con extension .smia')
      evento.target.value = ''
      return
    }

    const lector = new FileReader()
    lector.onload = () => {
      setComandos(typeof lector.result === 'string' ? lector.result : '')
      setResultados(`[EXITOSO] Archivo .smia cargado correctamente\nNombre: ${archivo.name}`)
      setArchivoSeleccionado(archivo.name)
    }
    lector.onerror = () => {
      setResultados('[ERROR] No se pudo leer el archivo .smia')
      setArchivoSeleccionado('')
    }
    lector.readAsText(archivo)

    // Permite volver a seleccionar el mismo archivo posteriormente.
    evento.target.value = ''
  }

  return (
    <div className="app">
      <header className="encabezado">
        <h1>Simulador EXT2</h1>
        <p className="subtitulo">
          Simulador de Sistema de Archivos EXT2
        </p>
      </header>

      <main className="contenido">
        <section className="columna">
          <div className="columna-titulo">
            <h2>Comandos</h2>
            <div className="acciones-archivo">
              <label className="boton-secundario boton-archivo">
                Subir .smia
                <input
                  type="file"
                  accept=".smia"
                  onChange={manejarSubirSmia}
                  disabled={procesando}
                />
              </label>
              <button
                className="boton-secundario"
                onClick={limpiar}
                type="button"
                disabled={procesando}
              >
                Limpiar
              </button>
            </div>
          </div>

          <textarea
            className="textarea-comandos"
            value={comandos}
            onChange={(evento) => {
              setComandos(evento.target.value)
              setArchivoSeleccionado('')
            }}
            placeholder={'# escribe uno o varios comandos, uno por linea\nmkdisk -size=5 -unit=M -path=/home/user/Disco1.mia'}
            spellCheck={false}
          />

          <div className="archivo-seleccionado">
            {archivoSeleccionado && `Archivo: ${archivoSeleccionado}`}
          </div>

          <button
            className="boton-procesar"
            onClick={manejarProcesar}
            disabled={procesando || !comandos.trim()}
          >
            {procesando ? 'Procesando...' : 'Procesar'}
          </button>
        </section>

        <section className="columna">
          <div className="columna-titulo">
            <h2>Resultados</h2>
          </div>
          <textarea
            className="textarea-resultados"
            value={resultados}
            readOnly
            placeholder="Resultados."
            spellCheck={false}
          />
        </section>
      </main>
    </div>
  )
}

export default App