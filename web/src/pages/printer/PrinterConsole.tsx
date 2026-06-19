import { useState, useRef, useEffect } from 'react'
import { useDuetStore } from '../../store/duetStore'
import * as duetApi from '../../services/duetApi'

export default function PrinterConsole() {
  const { consoleLines, pushConsoleLine, clearConsole, commandHistory, pushCommand } = useDuetStore()
  const [cmd, setCmd] = useState('')
  const [historyIdx, setHistoryIdx] = useState(-1)
  const [filter, setFilter] = useState<'all' | 'input' | 'output'>('all')
  const logRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  // Auto-scroll to bottom
  useEffect(() => {
    if (logRef.current) {
      logRef.current.scrollTop = logRef.current.scrollHeight
    }
  }, [consoleLines])

  const sendCommand = async () => {
    const trimmed = cmd.trim()
    if (!trimmed) return

    pushCommand(trimmed)
    pushConsoleLine(`> ${trimmed}`)
    setCmd('')
    setHistoryIdx(-1)

    try {
      const reply = await duetApi.sendGCodeWithReply(trimmed)
      if (reply.trim()) {
        pushConsoleLine(reply.trim())
      }
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err)
      pushConsoleLine(`[ERROR] ${msg}`)
    }
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      sendCommand()
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      if (commandHistory.length === 0) return
      const newIdx = historyIdx < commandHistory.length - 1 ? historyIdx + 1 : historyIdx
      setHistoryIdx(newIdx)
      setCmd(commandHistory[commandHistory.length - 1 - newIdx] ?? '')
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      if (historyIdx <= 0) {
        setHistoryIdx(-1)
        setCmd('')
      } else {
        const newIdx = historyIdx - 1
        setHistoryIdx(newIdx)
        setCmd(commandHistory[commandHistory.length - 1 - newIdx] ?? '')
      }
    }
  }

  const filteredLines = consoleLines.filter(line => {
    if (filter === 'all') return true
    if (filter === 'input') return line.startsWith('>')
    if (filter === 'output') return !line.startsWith('>')
    return true
  })

  const quickCommands = [
    { label: 'M115', cmd: 'M115', desc: 'Firmware info' },
    { label: 'M122', cmd: 'M122', desc: 'Diagnostics' },
    { label: 'M105', cmd: 'M105', desc: 'Temperatures' },
    { label: 'M114', cmd: 'M114', desc: 'Position' },
    { label: 'M503', cmd: 'M503', desc: 'Settings' },
    { label: 'M20', cmd: 'M20', desc: 'List files' },
  ]

  return (
    <div className="flex flex-col h-full space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-white">G-Code Console</h2>
        <div className="flex items-center gap-2">
          {/* Filter buttons */}
          <div className="flex rounded-lg overflow-hidden border border-gray-700">
            {(['all', 'input', 'output'] as const).map(f => (
              <button
                key={f}
                onClick={() => setFilter(f)}
                className={`px-2.5 py-1 text-xs ${
                  filter === f ? 'bg-gray-700 text-white' : 'bg-gray-800 text-gray-500 hover:text-gray-300'
                }`}
              >
                {f.charAt(0).toUpperCase() + f.slice(1)}
              </button>
            ))}
          </div>
          <button onClick={clearConsole} className="px-3 py-1 bg-gray-800 hover:bg-gray-700 text-gray-400 rounded-lg text-xs">
            Clear
          </button>
        </div>
      </div>

      {/* Quick commands */}
      <div className="flex gap-1.5 flex-wrap">
        {quickCommands.map(qc => (
          <button
            key={qc.cmd}
            onClick={() => { setCmd(qc.cmd); inputRef.current?.focus() }}
            className="px-2.5 py-1 bg-gray-800 hover:bg-gray-700 text-gray-400 rounded text-xs border border-gray-700"
            title={qc.desc}
          >
            {qc.label}
          </button>
        ))}
      </div>

      {/* Console output */}
      <div
        ref={logRef}
        className="flex-1 min-h-[400px] max-h-[calc(100vh-300px)] bg-gray-950 rounded-xl border border-gray-800 p-3 overflow-y-auto font-mono text-xs"
      >
        {filteredLines.length === 0 ? (
          <div className="text-gray-700 text-center py-8">
            Console output will appear here...
          </div>
        ) : (
          filteredLines.map((line, i) => (
            <div
              key={i}
              className={`py-0.5 ${
                line.startsWith('>')
                  ? 'text-primary-300'
                  : line.startsWith('[ERROR]')
                  ? 'text-red-400'
                  : 'text-green-400'
              }`}
            >
              {line}
            </div>
          ))
        )}
      </div>

      {/* Input */}
      <div className="flex gap-2">
        <input
          ref={inputRef}
          className="input flex-1 font-mono text-sm"
          value={cmd}
          onChange={e => setCmd(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="Type G-code command (e.g. G28, M105, M114)..."
          autoComplete="off"
          spellCheck={false}
        />
        <button
          onClick={sendCommand}
          className="px-6 py-2 bg-primary/80 hover:bg-primary text-white rounded-lg text-sm font-medium"
        >
          Send
        </button>
      </div>
    </div>
  )
}
