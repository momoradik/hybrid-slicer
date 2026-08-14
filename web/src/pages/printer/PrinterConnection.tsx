import { useState, useEffect, useMemo } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import {
  machineConnectionApi, machineProfilesApi,
  type MachineTransport, type ConnectRequest,
} from '../../api/client'
import { useAppStore } from '../../store'
import InfoTip from '../../components/InfoTip'
import DisabledHint from '../../components/DisabledHint'
import type { ApiError } from '../../types'

/**
 * Everything needed to get the software talking to the printer, in one place.
 *
 * Two ways in:
 *   Network — the board has an IP address. Whether it reaches the network over an
 *             Ethernet cable or Wi-Fi makes no difference; it is the same link.
 *   USB     — the board appears as a COM port on this computer.
 *
 * Last-used settings are kept in localStorage so reconnecting is one click.
 */

const LS_KEY = 'hybridslicer.connection'

interface Saved {
  transport: MachineTransport
  firmware: string
  host: string
  port: number
  password: string
  portName: string
  baudRate: number
  machineId: string
}

const DEFAULTS: Saved = {
  transport: 'Network',
  firmware: 'RepRapFirmware',
  host: '',
  port: 80,
  password: '',
  portName: '',
  baudRate: 115200,
  machineId: '',
}

// Duet's USB CDC port ignores the rate, but the OS still needs one; 115200 is the
// conventional choice. The faster rates are here for boards that do care.
const BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 250000, 460800, 921600]

function loadSaved(): Saved {
  try {
    const raw = localStorage.getItem(LS_KEY)
    return raw ? { ...DEFAULTS, ...JSON.parse(raw) } : DEFAULTS
  } catch { return DEFAULTS }
}

export default function PrinterConnection() {
  const qc = useQueryClient()
  const setMachineConnected = useAppStore(s => s.setMachineConnected)

  const [form, setForm] = useState<Saved>(loadSaved)
  const [error, setError] = useState<ApiError | null>(null)

  const set = <K extends keyof Saved>(k: K, v: Saved[K]) =>
    setForm(f => {
      const next = { ...f, [k]: v }
      try { localStorage.setItem(LS_KEY, JSON.stringify(next)) } catch { /* private mode */ }
      return next
    })

  const { data: status, refetch: refetchStatus } = useQuery({
    queryKey: ['machine-connection'],
    queryFn: machineConnectionApi.getStatus,
    refetchInterval: 4000,   // keep the badge honest if the link drops
  })

  const { data: firmwares = [] } = useQuery({
    queryKey: ['machine-firmwares'],
    queryFn: machineConnectionApi.getFirmwares,
  })

  const { data: ports = [], refetch: refetchPorts, isFetching: portsLoading } = useQuery({
    queryKey: ['serial-ports'],
    queryFn: machineConnectionApi.getSerialPorts,
    enabled: form.transport === 'Serial',
  })

  const { data: machines = [] } = useQuery({
    queryKey: ['machines'], queryFn: machineProfilesApi.getAll,
  })

  // Mirror the link state into the app store so the rest of the Printer section
  // (and its polling loop) reacts without needing its own copy of this logic.
  useEffect(() => {
    if (status) setMachineConnected(status.isConnected)
  }, [status?.isConnected, setMachineConnected]) // eslint-disable-line react-hooks/exhaustive-deps

  const connectMutation = useMutation({
    mutationFn: (req: ConnectRequest) => machineConnectionApi.connect(req),
    onSuccess: s => {
      setError(null)
      setMachineConnected(s.isConnected)
      qc.invalidateQueries({ queryKey: ['machine-connection'] })
      refetchStatus()
    },
    onError: (e: any) => setError(
      e?.response?.data ?? { code: 'NETWORK', message: e?.message ?? 'Could not reach the server.' }),
  })

  const disconnectMutation = useMutation({
    mutationFn: machineConnectionApi.disconnect,
    onSuccess: s => {
      setError(null)
      setMachineConnected(s.isConnected)
      qc.invalidateQueries({ queryKey: ['machine-connection'] })
      refetchStatus()
    },
  })

  const selectedFirmware = firmwares.find(f => f.id === form.firmware)
  const firmwareUnsupported = !!selectedFirmware && !selectedFirmware.supported

  const missing = useMemo(() => {
    if (firmwareUnsupported) return `${selectedFirmware?.name} is not supported yet.`
    if (form.transport === 'Network' && !form.host.trim()) return 'Enter the printer’s IP address or hostname.'
    if (form.transport === 'Serial' && !form.portName) return 'Select the COM port the board is on.'
    return null
  }, [form, firmwareUnsupported, selectedFirmware])

  const connect = () => {
    setError(null)
    connectMutation.mutate({
      transport: form.transport,
      firmware: form.firmware,
      host: form.host.trim(),
      port: form.port,
      password: form.password,
      portName: form.portName,
      baudRate: form.baudRate,
    })
  }

  const connected = !!status?.isConnected

  // Picking a machine profile fills in its stored address — the profile is where
  // the IP already lives, so there is no reason to retype it here.
  const applyMachine = (id: string) => {
    set('machineId', id)
    const m = machines.find(x => x.id === id)
    if (m?.ipAddress) set('host', m.ipAddress)
    if (m?.port) set('port', m.port)
  }

  return (
    <div className="max-w-3xl mx-auto space-y-5">
      {/* ── Status ── */}
      <div className={`rounded-xl border p-5 ${
        connected ? 'bg-green-950/30 border-green-800/60' : 'bg-gray-900 border-gray-800'
      }`}>
        <div className="flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-3">
            <span className={`w-2.5 h-2.5 rounded-full ${connected ? 'bg-green-400' : 'bg-gray-600'}`} />
            <div>
              <p className={`font-semibold ${connected ? 'text-green-300' : 'text-gray-300'}`}>
                {connected ? 'Connected' : 'Not connected'}
              </p>
              {connected && (
                <p className="text-xs text-gray-400 mt-0.5">
                  {status?.transport === 'Serial' ? 'USB' : 'Network'} · {status?.endpoint}
                  {status?.firmwareDescription && <> · {status.firmwareDescription}</>}
                </p>
              )}
            </div>
          </div>

          {connected && (
            <button
              onClick={() => disconnectMutation.mutate()}
              disabled={disconnectMutation.isPending}
              className="px-4 py-2 bg-red-900/50 hover:bg-red-900/80 border border-red-800 text-red-200 rounded-lg text-sm disabled:opacity-40"
            >
              {disconnectMutation.isPending ? 'Disconnecting…' : 'Disconnect'}
            </button>
          )}
        </div>

        {connected && status?.transport === 'Serial' && !status.supportsFileTransfer && (
          <p className="mt-3 text-xs text-amber-300/90 bg-amber-950/30 border border-amber-800/40 rounded-lg p-2.5">
            Connected over USB. Job Files and Height Map need the board's HTTP interface,
            which USB does not provide — connect by IP address to use those tabs.
            Dashboard, Control and Console all work over USB.
          </p>
        )}
      </div>

      {/* ── Error ── */}
      {error && (
        <div className="bg-red-950/50 border border-red-800 rounded-xl p-4 space-y-1.5">
          <p className="text-sm font-medium text-red-300">Could not connect</p>
          <p className="text-xs text-red-300/90">{error.message}</p>
          {error.field && (
            <p className="text-[11px] text-red-400/80">
              Field: <span className="font-mono">{error.field}</span>
              {error.providedValue && <> · you entered <span className="font-mono">{error.providedValue}</span></>}
            </p>
          )}
        </div>
      )}

      {/* ── Setup ── */}
      {!connected && (
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-5">
          <h3 className="text-sm font-semibold text-white">Connect to your printer</h3>

          {/* Firmware */}
          <Field label="Firmware" tip="The controller firmware running on the board. Only RepRapFirmware (Duet) is implemented at the moment; the others are listed so you can see what is planned.">
            <select
              className="input w-full"
              value={form.firmware}
              onChange={e => { set('firmware', e.target.value); setError(null) }}
            >
              {firmwares.map(f => (
                <option key={f.id} value={f.id} disabled={!f.supported}>
                  {f.name}{f.supported ? '' : ' — not supported yet'}
                </option>
              ))}
            </select>
            {selectedFirmware && (
              <p className={`text-[10px] mt-1 ${firmwareUnsupported ? 'text-amber-400' : 'text-gray-500'}`}>
                {selectedFirmware.note}
              </p>
            )}
          </Field>

          {/* Transport */}
          <Field label="Connection" tip="How this computer reaches the board. Network works whether the board is wired by Ethernet or on Wi-Fi — either way it has an IP address. USB uses the board's virtual COM port.">
            <div className="flex gap-2">
              <TransportBtn
                active={form.transport === 'Network'}
                onClick={() => { set('transport', 'Network'); setError(null) }}
                title="Network (IP)"
                subtitle="Ethernet or Wi-Fi"
              />
              <TransportBtn
                active={form.transport === 'Serial'}
                onClick={() => { set('transport', 'Serial'); setError(null); refetchPorts() }}
                title="USB (COM port)"
                subtitle="Direct cable"
              />
            </div>
          </Field>

          {form.transport === 'Network' ? (
            <div className="space-y-4">
              {machines.length > 0 && (
                <Field label="Use address from machine profile" tip="Optional. Fills the address in from a saved machine profile so you do not have to retype it.">
                  <select className="input w-full" value={form.machineId} onChange={e => applyMachine(e.target.value)}>
                    <option value="">— enter manually —</option>
                    {machines.map(m => (
                      <option key={m.id} value={m.id}>
                        {m.name}{m.ipAddress ? ` (${m.ipAddress})` : ' — no address saved'}
                      </option>
                    ))}
                  </select>
                </Field>
              )}

              <div className="grid grid-cols-3 gap-4">
                <div className="col-span-2">
                  <Field label="IP address or hostname" tip="The address of the board on your network, e.g. 192.168.1.20. You can find it on the printer's display, or in your router's client list." required>
                    <input
                      className={`input w-full font-mono ${error?.field === 'host' ? 'border-red-600' : ''}`}
                      placeholder="192.168.1.20"
                      value={form.host}
                      onChange={e => set('host', e.target.value)}
                      onKeyDown={e => { if (e.key === 'Enter' && !missing) connect() }}
                    />
                  </Field>
                </div>
                <Field label="Port" tip="HTTP port the board serves on. RepRapFirmware uses 80 unless you changed it.">
                  <input
                    type="number" min={1} max={65535}
                    className="input w-full"
                    value={form.port}
                    onChange={e => set('port', Number(e.target.value))}
                  />
                </Field>
              </div>

              <Field label="Password" tip="Only if you set one on the board (M551). Leave empty otherwise.">
                <input
                  type="password"
                  className="input w-full"
                  placeholder="(usually empty)"
                  value={form.password}
                  onChange={e => set('password', e.target.value)}
                />
              </Field>
            </div>
          ) : (
            <div className="space-y-4">
              <div className="grid grid-cols-3 gap-4">
                <div className="col-span-2">
                  <Field label="COM port" tip="The virtual serial port the board appears as. If you are unsure, unplug the printer, press Refresh, plug it back in and press Refresh again — the port that appears is the printer." required>
                    <div className="flex gap-2">
                      <select
                        className={`input flex-1 ${error?.field === 'portName' ? 'border-red-600' : ''}`}
                        value={form.portName}
                        onChange={e => set('portName', e.target.value)}
                      >
                        <option value="">
                          {ports.length === 0 ? '— no COM ports detected —' : '— select a port —'}
                        </option>
                        {ports.map(p => <option key={p} value={p}>{p}</option>)}
                      </select>
                      <button
                        onClick={() => refetchPorts()}
                        className="px-3 py-2 bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-300 rounded-lg text-xs whitespace-nowrap"
                      >
                        {portsLoading ? '…' : 'Refresh'}
                      </button>
                    </div>
                  </Field>
                </div>
                <Field label="Baud rate" tip="Duet's USB port ignores this, but Windows still requires a value. 115200 is the safe default.">
                  <select className="input w-full" value={form.baudRate} onChange={e => set('baudRate', Number(e.target.value))}>
                    {BAUD_RATES.map(b => <option key={b} value={b}>{b}</option>)}
                  </select>
                </Field>
              </div>

              {ports.length === 0 && (
                <p className="text-xs text-gray-500 bg-gray-950 border border-gray-800 rounded-lg p-3">
                  No COM ports found. Check the USB cable is a data cable (not charge-only),
                  that the board is powered, and that its driver is installed — then press Refresh.
                </p>
              )}
            </div>
          )}

          <DisabledHint when={!!missing} reason={missing ?? ''}>
            <button
              onClick={connect}
              disabled={!!missing || connectMutation.isPending}
              className="w-full py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm font-medium"
            >
              {connectMutation.isPending ? 'Connecting…' : 'Connect'}
            </button>
          </DisabledHint>
        </div>
      )}
    </div>
  )
}

// ── Pieces ──────────────────────────────────────────────────────────────────

function TransportBtn({ active, onClick, title, subtitle }: {
  active: boolean; onClick: () => void; title: string; subtitle: string
}) {
  return (
    <button
      onClick={onClick}
      className={`flex-1 px-4 py-3 rounded-lg border text-left transition ${
        active
          ? 'bg-primary/20 border-primary/60 text-white'
          : 'bg-gray-950 border-gray-800 text-gray-400 hover:border-gray-700'
      }`}
    >
      <span className="block text-sm font-medium">{title}</span>
      <span className="block text-[10px] opacity-70 mt-0.5">{subtitle}</span>
    </button>
  )
}

function Field({ label, tip, required, children }: {
  label: string; tip: string; required?: boolean; children: React.ReactNode
}) {
  return (
    <div className="space-y-1.5">
      <label className="flex items-center gap-1.5 text-xs font-medium text-gray-300">
        {label}
        {required && <span className="text-red-400">*</span>}
        <InfoTip text={tip} />
      </label>
      {children}
    </div>
  )
}
