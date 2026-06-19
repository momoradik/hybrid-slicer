import { useState, useEffect, useRef } from 'react'
import * as duetApi from '../../services/duetApi'
import type { DuetFileInfo } from '../../services/duetApi'

type FileSection = 'jobs' | 'macros' | 'system'
const SECTION_DIRS: Record<FileSection, string> = {
  jobs: '0:/gcodes',
  macros: '0:/macros',
  system: '0:/sys',
}

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

function formatDate(dateStr: string): string {
  try {
    return new Date(dateStr).toLocaleString()
  } catch {
    return dateStr
  }
}

interface FileRowProps {
  file: DuetFileInfo
  basePath: string
  section: FileSection
  onNavigate: (dir: string) => void
  onRefresh: () => void
}

function FileRow({ file, basePath, section, onNavigate, onRefresh }: FileRowProps) {
  const fullPath = `${basePath}/${file.name}`
  const isDir = file.type === 'd'
  const isGCode = /\.(gcode|g|gc|nc)$/i.test(file.name)
  const isMacro = section === 'macros' && !isDir

  const handleClick = () => {
    if (isDir) {
      onNavigate(fullPath)
    }
  }

  const handlePrint = async () => {
    if (!confirm(`Start printing ${file.name}?`)) return
    await duetApi.sendGCode(`M32 "${fullPath}"`)
  }

  const handleRunMacro = async () => {
    await duetApi.sendGCode(`M98 P"${fullPath}"`)
  }

  const handleDownload = async () => {
    try {
      const blob = await duetApi.downloadFile(fullPath)
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = file.name
      a.click()
      URL.revokeObjectURL(url)
    } catch (err) {
      alert(`Download failed: ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  const handleDelete = async () => {
    if (!confirm(`Delete ${file.name}?`)) return
    try {
      await duetApi.deleteFile(fullPath)
      onRefresh()
    } catch (err) {
      alert(`Delete failed: ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  return (
    <tr className="border-t border-gray-800 hover:bg-gray-800/50">
      <td className="py-2 px-3">
        <button
          onClick={handleClick}
          className={`flex items-center gap-2 text-sm ${
            isDir ? 'text-blue-400 hover:text-blue-300 cursor-pointer' : 'text-gray-300 cursor-default'
          }`}
          disabled={!isDir}
        >
          <span>{isDir ? '📁' : isGCode ? '📄' : '📝'}</span>
          {file.name}
        </button>
      </td>
      <td className="py-2 px-3 text-right text-xs text-gray-500 font-mono">
        {isDir ? '-' : formatSize(file.size)}
      </td>
      <td className="py-2 px-3 text-right text-xs text-gray-500">
        {formatDate(file.date)}
      </td>
      <td className="py-2 px-3 text-right">
        <div className="flex gap-1 justify-end">
          {isGCode && section === 'jobs' && (
            <button onClick={handlePrint} className="px-2 py-0.5 bg-green-900/50 hover:bg-green-800/50 text-green-300 rounded text-xs">Print</button>
          )}
          {isMacro && (
            <button onClick={handleRunMacro} className="px-2 py-0.5 bg-blue-900/50 hover:bg-blue-800/50 text-blue-300 rounded text-xs">Run</button>
          )}
          {!isDir && (
            <button onClick={handleDownload} className="px-2 py-0.5 bg-gray-700 hover:bg-gray-600 text-gray-300 rounded text-xs">Download</button>
          )}
          {!isDir && (
            <button onClick={handleDelete} className="px-2 py-0.5 bg-red-900/50 hover:bg-red-800/50 text-red-300 rounded text-xs">Delete</button>
          )}
        </div>
      </td>
    </tr>
  )
}

function FileViewer({ path, onClose }: { path: string; onClose: () => void }) {
  const [content, setContent] = useState('')
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    (async () => {
      try {
        const blob = await duetApi.downloadFile(path)
        setContent(await blob.text())
      } catch (err) {
        setContent(`Error: ${err instanceof Error ? err.message : String(err)}`)
      } finally {
        setLoading(false)
      }
    })()
  }, [path])

  return (
    <div className="fixed inset-0 bg-black/70 flex items-center justify-center z-50">
      <div className="bg-gray-900 border border-gray-700 rounded-xl w-full max-w-4xl max-h-[80vh] flex flex-col shadow-2xl">
        <div className="flex items-center justify-between px-4 py-3 border-b border-gray-800">
          <h3 className="text-sm font-medium text-white truncate">{path}</h3>
          <button onClick={onClose} className="text-gray-400 hover:text-white text-lg">&times;</button>
        </div>
        <div className="flex-1 overflow-auto p-4">
          {loading ? (
            <div className="text-gray-500 text-sm">Loading...</div>
          ) : (
            <pre className="text-xs font-mono text-green-400 whitespace-pre-wrap">{content}</pre>
          )}
        </div>
      </div>
    </div>
  )
}

export default function PrinterJobFiles() {
  const [section, setSection] = useState<FileSection>('jobs')
  const [currentDir, setCurrentDir] = useState(SECTION_DIRS.jobs)
  const [files, setFiles] = useState<DuetFileInfo[]>([])
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const [viewFile, setViewFile] = useState<string | null>(null)
  const fileInputRef = useRef<HTMLInputElement>(null)

  const loadFiles = async (dir: string) => {
    setLoading(true)
    setError('')
    try {
      const result = await duetApi.listFiles(dir)
      if (result.err) throw new Error(`Error listing directory: ${result.err}`)
      // Sort: directories first, then alphabetical
      const sorted = [...result.files].sort((a, b) => {
        if (a.type !== b.type) return a.type === 'd' ? -1 : 1
        return a.name.localeCompare(b.name)
      })
      setFiles(sorted)
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : String(err))
      setFiles([])
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    const dir = SECTION_DIRS[section]
    setCurrentDir(dir)
    loadFiles(dir)
  }, [section])

  const navigateTo = (dir: string) => {
    setCurrentDir(dir)
    loadFiles(dir)
  }

  const goUp = () => {
    const baseDir = SECTION_DIRS[section]
    if (currentDir === baseDir) return
    const parent = currentDir.substring(0, currentDir.lastIndexOf('/'))
    navigateTo(parent || baseDir)
  }

  const handleUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (!file) return
    try {
      const buffer = await file.arrayBuffer()
      const destPath = `${currentDir}/${file.name}`
      await duetApi.uploadFile(destPath, buffer)
      loadFiles(currentDir)
    } catch (err) {
      alert(`Upload failed: ${err instanceof Error ? err.message : String(err)}`)
    }
    if (fileInputRef.current) fileInputRef.current.value = ''
  }

  const handleNewFolder = async () => {
    const name = prompt('New folder name:')
    if (!name) return
    try {
      await duetApi.makeDirectory(`${currentDir}/${name}`)
      loadFiles(currentDir)
    } catch (err) {
      alert(`Failed: ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  const breadcrumbs = () => {
    const baseDir = SECTION_DIRS[section]
    const relative = currentDir.substring(baseDir.length)
    const parts = relative.split('/').filter(Boolean)
    const crumbs = [{ label: section.charAt(0).toUpperCase() + section.slice(1), path: baseDir }]
    let pathAcc = baseDir
    for (const part of parts) {
      pathAcc += '/' + part
      crumbs.push({ label: part, path: pathAcc })
    }
    return crumbs
  }

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-white">Job Files</h2>
      </div>

      {/* Section tabs */}
      <div className="flex border-b border-gray-800">
        {([['jobs', 'Jobs'], ['macros', 'Macros'], ['system', 'System']] as [FileSection, string][]).map(([id, label]) => (
          <button
            key={id}
            onClick={() => setSection(id)}
            className={`px-4 py-2 text-sm font-medium border-b-2 -mb-px transition ${
              section === id
                ? 'border-primary-400 text-primary-300'
                : 'border-transparent text-gray-500 hover:text-gray-300'
            }`}
          >
            {label}
          </button>
        ))}
      </div>

      {/* Breadcrumb + actions */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-1 text-sm">
          {currentDir !== SECTION_DIRS[section] && (
            <button onClick={goUp} className="px-2 py-0.5 bg-gray-800 hover:bg-gray-700 rounded text-gray-400 text-xs mr-1">⬆ Up</button>
          )}
          {breadcrumbs().map((crumb, i) => (
            <span key={crumb.path} className="flex items-center gap-1">
              {i > 0 && <span className="text-gray-600">/</span>}
              <button
                onClick={() => navigateTo(crumb.path)}
                className={`hover:text-primary-300 ${i === breadcrumbs().length - 1 ? 'text-white' : 'text-gray-400'}`}
              >
                {crumb.label}
              </button>
            </span>
          ))}
        </div>
        <div className="flex gap-2">
          <button onClick={handleNewFolder} className="px-3 py-1 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">New Folder</button>
          <input ref={fileInputRef} type="file" className="hidden" onChange={handleUpload} />
          <button onClick={() => fileInputRef.current?.click()} className="px-3 py-1 bg-primary/80 hover:bg-primary text-white rounded-lg text-xs">Upload</button>
          <button onClick={() => loadFiles(currentDir)} className="px-3 py-1 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Refresh</button>
        </div>
      </div>

      {error && (
        <div className="bg-red-950/40 border border-red-800 rounded-lg px-4 py-2 text-sm text-red-400">{error}</div>
      )}

      {/* File table */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
        {loading ? (
          <div className="p-8 text-center text-gray-500 text-sm">Loading files...</div>
        ) : files.length === 0 ? (
          <div className="p-8 text-center text-gray-600 text-sm">
            <p>This directory is empty</p>
          </div>
        ) : (
          <table className="w-full">
            <thead>
              <tr className="text-gray-500 text-xs border-b border-gray-800">
                <th className="text-left py-2 px-3 font-medium">Name</th>
                <th className="text-right py-2 px-3 font-medium w-24">Size</th>
                <th className="text-right py-2 px-3 font-medium w-40">Modified</th>
                <th className="text-right py-2 px-3 font-medium w-48">Actions</th>
              </tr>
            </thead>
            <tbody>
              {files.map(file => (
                <FileRow
                  key={file.name}
                  file={file}
                  basePath={currentDir}
                  section={section}
                  onNavigate={navigateTo}
                  onRefresh={() => loadFiles(currentDir)}
                />
              ))}
            </tbody>
          </table>
        )}
      </div>

      {/* File viewer modal */}
      {viewFile && <FileViewer path={viewFile} onClose={() => setViewFile(null)} />}
    </div>
  )
}
