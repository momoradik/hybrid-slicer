import { useState, useEffect } from 'react'
import { useDuetStore } from '../../store/duetStore'
import * as duetApi from '../../services/duetApi'

type SettingsSection = 'general' | 'machine' | 'network' | 'plugins'

function GeneralSettings() {
  const model = useDuetStore(s => s.model)
  const boards = model?.boards ?? []
  const network = model?.network

  return (
    <div className="space-y-4">
      {/* Firmware Info */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
        <h3 className="text-sm font-medium text-white mb-3">Firmware Information</h3>
        <div className="space-y-2">
          {boards.length > 0 && (
            <>
              <InfoRow label="Board" value={boards[0].name || boards[0].shortName || 'Unknown'} />
              <InfoRow label="Firmware" value={boards[0].firmwareName || 'Unknown'} />
              <InfoRow label="Firmware Version" value={boards[0].firmwareVersion || 'Unknown'} />
            </>
          )}
          {network && (
            <>
              <InfoRow label="Machine Name" value={network.name || 'Not set'} />
              {network.interfaces?.map((iface, i) => (
                <InfoRow key={i} label={`Network (${iface.type})`} value={iface.actualIP || 'N/A'} />
              ))}
            </>
          )}
          <InfoRow label="Uptime" value={model?.state?.upTime ? formatUptime(model.state.upTime) : 'N/A'} />
        </div>
      </div>

      {/* Machine State */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
        <h3 className="text-sm font-medium text-white mb-3">Machine State</h3>
        <div className="space-y-2">
          <InfoRow label="Status" value={duetApi.statusLabel(model?.state?.status ?? 'idle')} />
          <InfoRow label="Machine Mode" value={model?.state?.machineMode ?? 'FFF'} />
          <InfoRow label="Current Tool" value={model?.state?.currentTool !== undefined && model.state.currentTool >= 0 ? `T${model.state.currentTool}` : 'None'} />
          {model?.state?.displayMessage && (
            <InfoRow label="Message" value={model.state.displayMessage} />
          )}
        </div>
      </div>

      {/* Storage */}
      {model?.volumes && model.volumes.length > 0 && (
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Storage</h3>
          <div className="space-y-3">
            {model.volumes.map((vol, i) => (
              <div key={i} className="space-y-1">
                <div className="flex justify-between text-sm">
                  <span className="text-gray-400">{vol.path || `Volume ${i}`}</span>
                  <span className={`text-xs ${vol.mounted ? 'text-green-400' : 'text-red-400'}`}>
                    {vol.mounted ? 'Mounted' : 'Not mounted'}
                  </span>
                </div>
                {vol.mounted && vol.totalSpace > 0 && (
                  <>
                    <div className="w-full bg-gray-800 rounded-full h-1.5">
                      <div
                        className="bg-primary-400 h-1.5 rounded-full"
                        style={{ width: `${((vol.totalSpace - vol.freeSpace) / vol.totalSpace) * 100}%` }}
                      />
                    </div>
                    <div className="text-xs text-gray-500">
                      {formatSize(vol.freeSpace)} free of {formatSize(vol.totalSpace)}
                    </div>
                  </>
                )}
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}

function MachineSettings() {
  const [configContent, setConfigContent] = useState('')
  const [configLoading, setConfigLoading] = useState(false)
  const [activeFile, setActiveFile] = useState('config.g')
  const [editing, setEditing] = useState(false)
  const [editContent, setEditContent] = useState('')
  const [saving, setSaving] = useState(false)

  const systemFiles = [
    'config.g',
    'config-override.g',
    'bed.g',
    'homeall.g',
    'homex.g',
    'homey.g',
    'homez.g',
    'pause.g',
    'resume.g',
    'cancel.g',
    'stop.g',
    'sleep.g',
    'daemon.g',
    'tfree0.g',
    'tpre0.g',
    'tpost0.g',
  ]

  const loadFile = async (name: string) => {
    setConfigLoading(true)
    setEditing(false)
    try {
      const blob = await duetApi.downloadFile(`0:/sys/${name}`)
      const text = await blob.text()
      setConfigContent(text)
      setActiveFile(name)
    } catch {
      setConfigContent(`; File not found or cannot be read: ${name}`)
    } finally {
      setConfigLoading(false)
    }
  }

  useEffect(() => { loadFile('config.g') }, [])

  const startEditing = () => {
    setEditContent(configContent)
    setEditing(true)
  }

  const saveFile = async () => {
    setSaving(true)
    try {
      const blob = new Blob([editContent], { type: 'text/plain' })
      await duetApi.uploadFile(`0:/sys/${activeFile}`, await blob.arrayBuffer())
      setConfigContent(editContent)
      setEditing(false)
    } catch (err) {
      alert(`Save failed: ${err instanceof Error ? err.message : String(err)}`)
    } finally {
      setSaving(false)
    }
  }

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 lg:grid-cols-4 gap-4">
        {/* File list */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-3">
          <h3 className="text-sm font-medium text-white mb-2">System Files</h3>
          <div className="space-y-0.5 max-h-96 overflow-y-auto">
            {systemFiles.map(f => (
              <button
                key={f}
                onClick={() => loadFile(f)}
                className={`w-full text-left px-2 py-1.5 rounded text-xs font-mono transition ${
                  activeFile === f ? 'bg-primary/20 text-primary-300' : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800'
                }`}
              >
                {f}
              </button>
            ))}
          </div>
        </div>

        {/* File content */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 lg:col-span-3">
          <div className="flex items-center justify-between mb-3">
            <h3 className="text-sm font-medium text-white font-mono">{activeFile}</h3>
            <div className="flex gap-2">
              {editing ? (
                <>
                  <button onClick={() => setEditing(false)} className="px-3 py-1 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded text-xs">Cancel</button>
                  <button onClick={saveFile} disabled={saving} className="px-3 py-1 bg-green-800 hover:bg-green-700 text-green-300 rounded text-xs disabled:opacity-50">
                    {saving ? 'Saving...' : 'Save'}
                  </button>
                </>
              ) : (
                <>
                  <button onClick={() => loadFile(activeFile)} className="px-3 py-1 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded text-xs">Reload</button>
                  <button onClick={startEditing} className="px-3 py-1 bg-primary/60 hover:bg-primary/80 text-white rounded text-xs">Edit</button>
                </>
              )}
            </div>
          </div>
          {configLoading ? (
            <div className="text-gray-500 text-sm py-8 text-center">Loading...</div>
          ) : editing ? (
            <textarea
              className="w-full h-96 bg-gray-950 border border-gray-700 rounded-lg p-3 text-xs font-mono text-green-400 resize-y"
              value={editContent}
              onChange={e => setEditContent(e.target.value)}
              spellCheck={false}
            />
          ) : (
            <pre className="text-xs font-mono text-green-400 bg-gray-950 rounded-lg p-3 max-h-96 overflow-auto whitespace-pre-wrap">
              {configContent}
            </pre>
          )}
        </div>
      </div>
    </div>
  )
}

function NetworkSettings() {
  const model = useDuetStore(s => s.model)
  const network = model?.network

  const [newName, setNewName] = useState('')

  const setMachineName = async () => {
    if (!newName.trim()) return
    await duetApi.sendGCode(`M550 P"${newName.trim()}"`)
    setNewName('')
  }

  return (
    <div className="space-y-4">
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
        <h3 className="text-sm font-medium text-white mb-3">Network Configuration</h3>
        <div className="space-y-2">
          <InfoRow label="Machine Name" value={network?.name ?? 'Not set'} />
          {network?.interfaces?.map((iface, i) => (
            <div key={i} className="space-y-1">
              <InfoRow label={`Interface ${i} Type`} value={iface.type} />
              <InfoRow label={`Interface ${i} IP`} value={iface.actualIP} />
              {iface.firmwareVersion && <InfoRow label={`Interface ${i} FW`} value={iface.firmwareVersion} />}
            </div>
          ))}
        </div>
      </div>

      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
        <h3 className="text-sm font-medium text-white mb-3">Set Machine Name</h3>
        <div className="flex gap-2">
          <input
            className="input flex-1 text-sm"
            value={newName}
            onChange={e => setNewName(e.target.value)}
            placeholder="New machine name..."
            onKeyDown={e => e.key === 'Enter' && setMachineName()}
          />
          <button onClick={setMachineName} className="px-4 py-2 bg-primary/80 hover:bg-primary text-white rounded-lg text-sm">Set</button>
        </div>
      </div>
    </div>
  )
}

function PluginsSection() {
  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
      <h3 className="text-sm font-medium text-white mb-3">Plugins</h3>
      <p className="text-sm text-gray-500">
        Plugin management is available through the Duet board's built-in plugin system.
        Use the System files editor to configure plugins via config.g.
      </p>
      <div className="mt-4 space-y-2">
        <button
          onClick={() => duetApi.sendGCode('M997 S1')}
          className="w-full py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-sm"
        >
          Update Firmware (M997)
        </button>
        <button
          onClick={() => duetApi.sendGCode('M999')}
          className="w-full py-2 bg-yellow-900/50 hover:bg-yellow-800/50 text-yellow-300 rounded-lg text-sm"
        >
          Reset Board (M999)
        </button>
      </div>
    </div>
  )
}

function InfoRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between items-center">
      <span className="text-sm text-gray-400">{label}</span>
      <span className="text-sm text-gray-200 font-mono">{value}</span>
    </div>
  )
}

function formatUptime(seconds: number): string {
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (d > 0) return `${d}d ${h}h ${m}m`
  if (h > 0) return `${h}h ${m}m`
  return `${m}m`
}

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`
}

export default function PrinterSettings() {
  const [section, setSection] = useState<SettingsSection>('general')

  return (
    <div className="space-y-4">
      <h2 className="text-xl font-semibold text-white">Settings</h2>

      {/* Section tabs */}
      <div className="flex border-b border-gray-800">
        {([
          ['general', 'General'],
          ['machine', 'Machine Specific'],
          ['network', 'Network'],
          ['plugins', 'Plugins'],
        ] as [SettingsSection, string][]).map(([id, label]) => (
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

      {/* Content */}
      {section === 'general' && <GeneralSettings />}
      {section === 'machine' && <MachineSettings />}
      {section === 'network' && <NetworkSettings />}
      {section === 'plugins' && <PluginsSection />}
    </div>
  )
}
