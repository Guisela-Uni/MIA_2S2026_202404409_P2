import { useCallback, useEffect, useMemo, useState } from 'react'
import {
  comprobarApi,
  obtenerEstado,
  obtenerSesion,
  iniciarSesion,
  cerrarSesion,
  ejecutarComando,
  procesarArchivo,
  listarDirectorio,
  leerArchivo,
  obtenerJournaling,
} from './api.js'

function errorTexto(error) {
  return error?.message || 'Ocurrió un error inesperado.'
}

function bytes(valor) {
  const cantidad = Number(valor) || 0
  if (cantidad < 1024) return `${cantidad} B`
  if (cantidad < 1024 ** 2) return `${(cantidad / 1024).toFixed(1)} KB`
  if (cantidad < 1024 ** 3) return `${(cantidad / (1024 ** 2)).toFixed(1)} MB`
  return `${(cantidad / (1024 ** 3)).toFixed(2)} GB`
}

function fecha(valor) {
  const marca = Number(valor)
  if (!Number.isFinite(marca) || marca <= 0) return 'Fecha no disponible'
  return new Date(marca * 1000).toLocaleString()
}

function tipoParticion(tipo) {
  const tipos = { p: 'Primaria', e: 'Extendida', l: 'Lógica' }
  return tipos[String(tipo || '').toLowerCase()] || 'Desconocido'
}

function fitTexto(fit) {
  const valores = { f: 'First Fit', b: 'Best Fit', w: 'Worst Fit' }
  return valores[String(fit || '').toLowerCase()] || fit || '—'
}

function permisos(valor) {
  return String(valor ?? 0).padStart(3, '0')
}

function padre(ruta) {
  const partes = String(ruta || '/').split('/').filter(Boolean)
  partes.pop()
  return partes.length ? `/${partes.join('/')}` : '/'
}

function App() {
  const [apiDisponible, setApiDisponible] = useState(false)
  const [mensajeApi, setMensajeApi] = useState('Conectando con el backend...')
  const [cargando, setCargando] = useState(true)
  const [estado, setEstado] = useState({ discos: [], montadas: [] })
  const [sesion, setSesion] = useState(null)
  const [pagina, setPagina] = useState('terminal')

  const [comandos, setComandos] = useState('')
  const [salida, setSalida] = useState('')
  const [ejecutando, setEjecutando] = useState(false)
  const [archivoSmia, setArchivoSmia] = useState('')
  const [confirmacion, setConfirmacion] = useState(null)

  const [usuario, setUsuario] = useState('root')
  const [password, setPassword] = useState('')
  const [idLogin, setIdLogin] = useState('')
  const [cargandoLogin, setCargandoLogin] = useState(false)
  const [errorLogin, setErrorLogin] = useState('')

  const [rutaActual, setRutaActual] = useState('/')
  const [elementos, setElementos] = useState([])
  const [cargandoDirectorio, setCargandoDirectorio] = useState(false)
  const [filtro, setFiltro] = useState('')
  const [archivoActivo, setArchivoActivo] = useState(null)
  const [contenidoArchivo, setContenidoArchivo] = useState('')
  const [cargandoArchivo, setCargandoArchivo] = useState(false)
  const [errorExplorador, setErrorExplorador] = useState('')

  const [entradasJournal, setEntradasJournal] = useState([])
  const [errorJournal, setErrorJournal] = useState('')
  const [cargandoJournal, setCargandoJournal] = useState(false)

  const discos = estado.discos || []
  const montadas = estado.montadas || []
  const particionesParaLogin = useMemo(
    () => montadas.filter((particion) => particion.montada && particion.formateada && particion.id),
    [montadas],
  )
  const particionActiva = montadas.find((particion) => particion.id === sesion?.id) || null
  const discosConParticionActiva = useMemo(() => {
    const ruta = particionActiva?.rutaDisco
    return discos.map((disco) => ({
      ...disco,
      tieneSesion: disco.path === ruta,
      particiones: disco.particiones || [],
    }))
  }, [discos, particionActiva?.rutaDisco])

  const actualizarEstado = useCallback(async () => {
    try {
      const datos = await obtenerEstado()
      setEstado({ discos: [], montadas: [], ...datos })
      setApiDisponible(true)
      setMensajeApi('Backend conectado')
      return datos
    } catch (error) {
      setApiDisponible(false)
      setMensajeApi(errorTexto(error))
      throw error
    }
  }, [])

  const cargarDirectorio = useCallback(async (ruta = '/') => {
    setCargandoDirectorio(true)
    setErrorExplorador('')
    setArchivoActivo(null)
    setContenidoArchivo('')
    try {
      const respuesta = await listarDirectorio(ruta)
      setRutaActual(respuesta.path || ruta)
      setElementos(respuesta.items || [])
    } catch (error) {
      setElementos([])
      setErrorExplorador(errorTexto(error))
    } finally {
      setCargandoDirectorio(false)
    }
  }, [])

  const cargarArchivo = useCallback(async (archivo) => {
    if (!archivo.readable) {
      setErrorExplorador('No tienes permiso de lectura para este archivo.')
      return
    }
    setArchivoActivo(archivo)
    setContenidoArchivo('')
    setCargandoArchivo(true)
    setErrorExplorador('')
    try {
      const respuesta = await leerArchivo(archivo.path)
      setContenidoArchivo(respuesta.content ?? '')
    } catch (error) {
      setErrorExplorador(errorTexto(error))
    } finally {
      setCargandoArchivo(false)
    }
  }, [])

  const cargarJournal = useCallback(async () => {
    if (!sesion?.id) return
    setCargandoJournal(true)
    setErrorJournal('')
    try {
      const respuesta = await obtenerJournaling(sesion.id)
      setEntradasJournal(respuesta.entries || [])
    } catch (error) {
      setEntradasJournal([])
      setErrorJournal(errorTexto(error))
    } finally {
      setCargandoJournal(false)
    }
  }, [sesion?.id])

  useEffect(() => {
    let cancelado = false
    async function inicializar() {
      try {
        await comprobarApi()
        const [datosEstado, datosSesion] = await Promise.all([obtenerEstado(), obtenerSesion()])
        if (cancelado) return
        setEstado({ discos: [], montadas: [], ...datosEstado })
        setApiDisponible(true)
        setMensajeApi('Backend conectado')
        const actual = datosSesion.sesion
        if (actual?.activa) {
          setSesion(actual)
          setPagina('explorador')
          const lista = datosEstado.montadas || []
          if (!lista.some((item) => item.id === actual.id && item.formateada)) {
            setErrorExplorador('La sesión está activa, pero no se encontró un filesystem válido para explorar.')
          } else {
            await cargarDirectorio('/')
          }
        }
      } catch (error) {
        if (!cancelado) {
          setApiDisponible(false)
          setMensajeApi(errorTexto(error))
        }
      } finally {
        if (!cancelado) setCargando(false)
      }
    }
    inicializar()
    return () => { cancelado = true }
  }, [cargarDirectorio])

  useEffect(() => {
    if (particionesParaLogin.length === 0) {
      setIdLogin('')
    } else if (!particionesParaLogin.some((item) => item.id === idLogin)) {
      setIdLogin(particionesParaLogin[0].id)
    }
  }, [particionesParaLogin, idLogin])

  async function ejecutarTerminal(texto = comandos, opciones = {}) {
    const lineasValidas = String(texto)
      .split(/\r?\n/)
      .map((linea) => linea.trim())
      .filter((linea) => linea && !linea.startsWith('#'))

    if (lineasValidas.length === 0 || ejecutando) return

    const comando = lineasValidas.join('\n')
    setEjecutando(true)
    setConfirmacion(null)
    try {
      let resultado
      const lineasValidas = comando.split(/\r?\n/).filter((linea) => linea.trim() && !linea.trim().startsWith('#'))
      if (lineasValidas.length > 1) {
        resultado = await procesarArchivo(comando)
      } else {
        const respuesta = await ejecutarComando(comando)
        resultado = respuesta.resultado || ''
        if (!opciones.omitirConfirmacion && resultado.includes('[CONFIRMACION]')) {
          const tipo = /\bfdisk\b.*-delete\s*=/i.test(comando) ? 'delete' :
            /\bmkfile\b/i.test(comando) ? 'overwrite' : 'manual'
          setConfirmacion({ comando, tipo, mensaje: resultado })
        }
      }

      setSalida((previo) =>
          `${previo}${previo && resultado ? '\n' : ''}${opciones.confirmado ? '[CONFIRMADO]\n' : ''}${resultado}`
      )

      if (/\b(mkdisk|rmdisk|fdisk|mount|unmount|mkfs)\b/i.test(comando)) {
        const nuevoEstado = await actualizarEstado()
        const disponibles = (nuevoEstado.montadas || []).filter((item) => item.montada && item.formateada)
        if (!idLogin && disponibles.length) setIdLogin(disponibles[0].id)
      } else if (/\b(loss|logout)\b/i.test(comando)) {
        try {
          const actual = await obtenerSesion()
          if (!actual.sesion?.activa) {
            setSesion(null)
            setPagina('terminal')
          }
        } catch { /* Se conserva la salida del comando. */ }
        await actualizarEstado().catch(() => {})
      }
      if (sesion && /\b(mkdir|mkfile|remove|rename|copy|move|chown|chmod|mkusr|rmusr|mkgrp|rmgrp|chgrp|loss)\b/i.test(comando)) {
        await cargarDirectorio(rutaActual)
        if (pagina === 'journaling') await cargarJournal()
      }
    } catch (error) {
      setSalida((previo) =>
          `${previo}${previo ? '\n' : ''}[ERROR] ${errorTexto(error)}`
      )
    } finally {
      setEjecutando(false)
    }
  }

  async function confirmarComando() {
    if (!confirmacion) return
    let comando = confirmacion.comando
    if (confirmacion.tipo === 'delete') {
      comando = /-confirm\s*=/i.test(comando)
        ? comando.replace(/-confirm\s*=\s*\S+/i, '-confirm=yes')
        : `${comando} -confirm=yes`
    } else if (confirmacion.tipo === 'overwrite') {
      comando = /-overwrite\s*=/i.test(comando)
        ? comando.replace(/-overwrite\s*=\s*\S+/i, '-overwrite=true')
        : `${comando} -overwrite=true`
    } else {
      setSalida((previo) => `${previo}\n[AVISO] Revisa el parámetro de confirmación requerido por el comando.`)
      setConfirmacion(null)
      return
    }
    setComandos(comando)
    setConfirmacion(null)
    await ejecutarTerminal(comando, { confirmado: true, omitirConfirmacion: true })
  }

  function manejarArchivoSmia(evento) {
    const archivo = evento.target.files?.[0]
    evento.target.value = ''
    if (!archivo) return
    if (!archivo.name.toLowerCase().endsWith('.smia')) {
      setSalida('[ERROR] Selecciona un archivo con extensión .smia.')
      return
    }
    const lector = new FileReader()
    lector.onload = () => {
      setComandos(typeof lector.result === 'string' ? lector.result : '')
      setArchivoSmia(archivo.name)
      setSalida(`[ARCHIVO CARGADO] ${archivo.name}\nRevisa los comandos y pulsa Ejecutar.`)
    }
    lector.onerror = () => setSalida('[ERROR] No se pudo leer el archivo .smia.')
    lector.readAsText(archivo)
  }

  async function manejarLogin(evento) {
    evento.preventDefault()
    setErrorLogin('')
    if (!usuario.trim() || !password || !idLogin) {
      setErrorLogin('Ingresa usuario, contraseña y selecciona una partición montada.')
      return
    }
    setCargandoLogin(true)
    try {
      const respuesta = await iniciarSesion(usuario.trim(), password, idLogin)
      if (!respuesta.ok || !respuesta.sesion?.activa) throw new Error(respuesta.resultado || 'Credenciales incorrectas.')
      setSesion(respuesta.sesion)
      setPassword('')
      setPagina('explorador')
      setRutaActual('/')
      await actualizarEstado()
      await cargarDirectorio('/')
    } catch (error) {
      setErrorLogin(errorTexto(error))
    } finally {
      setCargandoLogin(false)
    }
  }

  async function manejarLogout() {
    try { await cerrarSesion() } catch { /* La sesión local se limpia igualmente. */ }
    setSesion(null)
    setPagina('terminal')
    setRutaActual('/')
    setElementos([])
    setArchivoActivo(null)
    setContenidoArchivo('')
    setEntradasJournal([])
    setFiltro('')
    await actualizarEstado().catch(() => {})
  }

  async function solicitarCambioParticion(id) {
    await manejarLogout()
    setIdLogin(id)
    setErrorLogin('')
    setPagina('login')
  }

  async function abrirParticion(particion) {
    if (!sesion || particion.id !== sesion.id) {
      setErrorExplorador(`La sesión activa utiliza el ID ${sesion?.id || '—'}. Para abrir otra partición, cierra sesión e inicia sesión usando su ID de montaje.`)
      return
    }
    if (!particion.formateada || !particion.montada) {
      setErrorExplorador('La partición debe estar montada y formateada como EXT2 o EXT3.')
      return
    }
    setErrorExplorador('')
    setRutaActual('/')
    await cargarDirectorio('/')
  }

  function manejarElemento(elemento) {
    setErrorExplorador('')
    if (elemento.type === 'directory') {
      if (!elemento.readable) {
        setErrorExplorador('No tienes permiso para leer este directorio.')
        return
      }
      setRutaActual(elemento.path)
      cargarDirectorio(elemento.path)
    } else {
      cargarArchivo(elemento)
    }
  }

  const elementosFiltrados = elementos.filter((elemento) =>
    String(elemento.name || '').toLowerCase().includes(filtro.trim().toLowerCase()),
  )

  if (cargando) {
    return <main className="contenedor"><h1>Simulador de sistema de archivos</h1><p>Conectando con el backend...</p></main>
  }

  return (
    <div className="app">
      <header className="encabezado">
        <div>
          <h1>C++ DISK ONLINE</h1>
          <p className="subtitulo">Sistema de archivos EXT2 / EXT3</p>
        </div>
        <div className="encabezado-acciones">
          <span className={apiDisponible ? 'estado api-ok' : 'estado api-error'}>{apiDisponible ? 'Backend conectado' : 'Backend desconectado'}</span>
          {sesion ? (
            <>
              <span className="usuario-actual">{sesion.usuario} · {sesion.id}</span>
              <button className="boton-secundario" type="button" onClick={manejarLogout}>Cerrar sesión</button>
            </>
          ) : (
            <button className="boton-procesar" type="button" onClick={() => { setPagina('login'); actualizarEstado().catch(() => {}) }}>Iniciar sesión</button>
          )}
        </div>
      </header>

      {sesion && (
        <nav className="navegacion">
          <button className={pagina === 'terminal' ? 'nav-activo' : ''} type="button" onClick={() => setPagina('terminal')}>Terminal</button>
          <button className={pagina === 'explorador' ? 'nav-activo' : ''} type="button" onClick={() => setPagina('explorador')}>Discos y archivos</button>
          <button className={pagina === 'journaling' ? 'nav-activo' : ''} type="button" onClick={() => { setPagina('journaling'); cargarJournal() }}>Journaling</button>
        </nav>
      )}

      <main className="contenido">
        {!apiDisponible && (
          <div className="mensaje-api"><strong>No se puede conectar con la API.</strong> {mensajeApi} <code>cd Backend && ./api_server</code></div>
        )}

        {pagina === 'terminal' && (
          <div className="columnas">
            <section className="columna">
              <div className="columna-titulo">
                <h2>Comandos</h2>
                <div className="acciones-archivo">
                  <label className="boton-secundario boton-archivo">Subir .smia<input type="file" accept=".smia" onChange={manejarArchivoSmia} disabled={ejecutando} /></label>
                  <button className="boton-secundario" type="button" disabled={ejecutando} onClick={() => { setComandos(''); setSalida(''); setArchivoSmia(''); setConfirmacion(null) }}>Limpiar</button>
                </div>
              </div>
              <textarea className="textarea-comandos" value={comandos} onChange={(evento) => { setComandos(evento.target.value); setArchivoSmia(''); setConfirmacion(null) }} spellCheck={false} placeholder={'mkdisk -size=10 -unit=m -path="/ruta/Disco1.mia"\nfdisk -size=8 -unit=m -path="/ruta/Disco1.mia" -name=principal\nmount -path="/ruta/Disco1.mia" -name=principal\n# Usa el ID que realmente imprime MOUNT; 091A es solo un ejemplo\nmkfs -id=091A -fs=3fs'} />
              <div className="archivo-seleccionado">{archivoSmia || (sesion ? `Sesión activa: ${sesion.usuario} en ${sesion.id}` : 'Primero crea, monta y formatea una partición para iniciar sesión.')}</div>
              <button className="boton-procesar" type="button" onClick={() => ejecutarTerminal()} disabled={ejecutando || !comandos.trim()}>{ejecutando ? 'Procesando...' : 'Ejecutar'}</button>
              {confirmacion && <div className="confirmacion"><p>{confirmacion.mensaje}</p><button className="boton-procesar" type="button" onClick={confirmarComando}>Confirmar operación</button><button className="boton-secundario" type="button" onClick={() => setConfirmacion(null)}>Cancelar</button></div>}
            </section>
            <section className="columna">
              <div className="columna-titulo"><h2>Resultados</h2><button className="boton-secundario" type="button" onClick={() => setSalida('')}>Borrar salida</button></div>
              <textarea className="textarea-resultados" value={salida} readOnly placeholder="Los resultados de los comandos aparecerán aquí." spellCheck={false} />
            </section>
          </div>
        )}

        {pagina === 'login' && !sesion && (
          <section className="panel-login">
            <h2>Iniciar sesión</h2>
            <p>Selecciona una partición montada e ingresa tus credenciales.</p>
            <form onSubmit={manejarLogin}>
              <label>Partición
                <select value={idLogin} onChange={(evento) => setIdLogin(evento.target.value)} disabled={!particionesParaLogin.length}>
                  <option value="">Selecciona una partición</option>
                  {particionesParaLogin.map((particion) => (
                    <option key={particion.id} value={particion.id}>{particion.disco} / {particion.particion} — {particion.id} — {particion.filesystem} — {bytes(particion.size)}</option>
                  ))}
                </select>
              </label>
              <label>Usuario<input value={usuario} onChange={(evento) => setUsuario(evento.target.value)} autoComplete="username" /></label>
              <label>Contraseña<input type="password" value={password} onChange={(evento) => setPassword(evento.target.value)} autoComplete="current-password" /></label>
              {particionesParaLogin.length === 0 && <p className="mensaje-ayuda">No hay particiones montadas y formateadas. Vuelve a Terminal y ejecuta MOUNT y MKFS.</p>}
              {errorLogin && <p className="mensaje-error">{errorLogin}</p>}
              <div className="botones-formulario"><button className="boton-procesar" type="submit" disabled={!apiDisponible || cargandoLogin || !idLogin}>{cargandoLogin ? 'Validando...' : 'Ingresar'}</button><button className="boton-secundario" type="button" onClick={() => setPagina('terminal')}>Volver a terminal</button></div>
            </form>
          </section>
        )}

        {pagina === 'explorador' && sesion && (
          <section className="seccion-explorador">
            <div className="columna-titulo"><div><h2>Discos y sistema de archivos</h2><p className="nota">Selecciona un disco, después una partición y navega sus carpetas.</p></div><button className="boton-secundario" type="button" onClick={() => actualizarEstado().catch(() => {})}>Actualizar discos</button></div>
            {errorExplorador && <p className="mensaje-error">{errorExplorador}</p>}
            {discos.length === 0 ? <p>No se han registrado discos. Usa la terminal para ejecutar MKDISK.</p> : (
              <div className="arbol-discos">
                {discosConParticionActiva.map((disco) => (
                  <details key={disco.path} open={disco.tieneSesion} className="nodo-disco">
                    <summary><strong>{disco.nombre}</strong><span>{bytes(disco.tamano)} · FIT {fitTexto(disco.fit)} · {(disco.particiones || []).length} partición(es) · {disco.particionesMontadas || 0} montada(s) · {fecha(disco.fechaCreacion)}</span><small>{disco.path}</small></summary>
                    <div className="lista-particiones">
                      {(disco.particiones || []).length === 0 ? <p className="nota">Este disco todavía no contiene particiones.</p> : (disco.particiones || []).map((particion) => (
                        <div key={`${disco.path}-${particion.nombre}`} className={`nodo-particion ${particion.id === sesion.id ? 'particion-activa' : ''}`}>
                          <div className="fila-particion">
                            <div className="particion-info">
                              <strong>{particion.nombre}</strong><span>{tipoParticion(particion.tipo)} · {bytes(particion.size)} · {fitTexto(particion.fit)}</span>
                              <small>{particion.estado || (particion.montada ? 'Montada' : 'Sin montar')} · {particion.formateada ? particion.filesystem : 'Sin formato'} · ID {particion.id || '—'}</small>
                            </div>
                            {particion.tipo !== 'e' && particion.id === sesion.id && particion.formateada && <button className="boton-secundario" type="button" onClick={() => abrirParticion(particion)}>Abrir /</button>}
                            {particion.id && particion.id !== sesion.id && particion.montada && particion.formateada && <button className="boton-secundario" type="button" onClick={() => solicitarCambioParticion(particion.id)}>Usar ID</button>}
                          </div>
                          {particion.id === sesion.id && particion.formateada && (
                            <div className="contenido-particion">
                              <div className="ruta-actual">
                                <span>Ruta: <code>{rutaActual}</code></span>
                                <button className="boton-secundario" type="button" disabled={rutaActual === '/' || cargandoDirectorio} onClick={() => cargarDirectorio(padre(rutaActual))}>Subir</button>
                                <button className="boton-secundario" type="button" disabled={cargandoDirectorio} onClick={() => cargarDirectorio(rutaActual)}>Actualizar</button>
                              </div>
                              <label className="filtro-archivos">Buscar en esta carpeta<input value={filtro} onChange={(evento) => setFiltro(evento.target.value)} placeholder="Nombre de archivo o carpeta" /></label>
                              <div className="tabla-contenedor">
                                <table className="tabla-archivos"><thead><tr><th>Nombre</th><th>Tipo</th><th>Tamaño</th><th>Permisos</th></tr></thead><tbody>
                                  {cargandoDirectorio ? <tr><td colSpan="4">Cargando...</td></tr> : elementosFiltrados.length === 0 ? <tr><td colSpan="4">{errorExplorador || (elementos.length && filtro.trim() ? 'No hay coincidencias en esta carpeta.' : 'La carpeta está vacía.')}</td></tr> : elementosFiltrados.map((elemento) => (
                                    <tr key={elemento.path}>
                                      <td><button className="enlace-archivo" type="button" onClick={() => manejarElemento(elemento)} disabled={elemento.type === 'file' && !elemento.readable}>{elemento.type === 'directory' ? '[Carpeta]' : '[Archivo]'} {elemento.name}</button></td>
                                      <td>{elemento.type === 'directory' ? 'Directorio' : 'Archivo'}</td>
                                      <td>{elemento.type === 'directory' ? '—' : bytes(elemento.size)}</td>
                                      <td><code>{permisos(elemento.permissions)}</code></td>
                                    </tr>
                                  ))}
                                </tbody></table>
                              </div>
                              {archivoActivo && <div className="visor-archivo"><div className="columna-titulo"><h3>{archivoActivo.name}</h3><button className="boton-secundario" type="button" onClick={() => { setArchivoActivo(null); setContenidoArchivo('') }}>Cerrar</button></div><p className="nota">{archivoActivo.path} · {bytes(archivoActivo.size)} · permisos {permisos(archivoActivo.permissions)}</p><pre>{cargandoArchivo ? 'Leyendo archivo...' : contenidoArchivo || '(Archivo vacío)'}</pre></div>}
                            </div>
                          )}
                        </div>
                      ))}
                    </div>
                  </details>
                ))}
              </div>
            )}
          </section>
        )}

        {pagina === 'journaling' && sesion && (
          <section className="seccion-explorador">
            <div className="columna-titulo"><h2>Journaling de {sesion.id}</h2><button className="boton-secundario" type="button" onClick={cargarJournal} disabled={cargandoJournal}>Actualizar</button></div>
            <p className="nota">El historial está disponible para particiones EXT3. Capacidad configurada: 50 registros.</p>
            {errorJournal && <p className="mensaje-error">{errorJournal}</p>}
            <div className="tabla-contenedor"><table className="tabla-archivos"><thead><tr><th>#</th><th>Operación</th><th>Ruta</th><th>Contenido</th><th>Fecha</th></tr></thead><tbody>
              {cargandoJournal ? <tr><td colSpan="5">Cargando journaling...</td></tr> : entradasJournal.length === 0 ? <tr><td colSpan="5">No hay registros disponibles.</td></tr> : entradasJournal.map((entrada, indice) => <tr key={`${entrada.count}-${indice}`}><td>{entrada.count}</td><td>{entrada.operation}</td><td><code>{entrada.path}</code></td><td>{entrada.content || '—'}</td><td>{entrada.datetime || entrada.timestamp}</td></tr>)}
            </tbody></table></div>
          </section>
        )}
      </main>
    </div>
  )
}

export default App
