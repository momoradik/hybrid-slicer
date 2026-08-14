import { useState, useEffect, useRef, useCallback } from 'react'
import { useAppStore } from '../store'
import { useDuetStore } from '../store/duetStore'
import * as duetApi from '../services/duetApi'
import PrinterDashboard from './printer/PrinterDashboard'
import PrinterControl from './printer/PrinterControl'
import PrinterConsole from './printer/PrinterConsole'
import PrinterHeightMap from './printer/PrinterHeightMap'
import PrinterJobFiles from './printer/PrinterJobFiles'
import PrinterSettings from './printer/PrinterSettings'
import PrinterConnection from './printer/PrinterConnection'
import { machineConnectionApi } from '../api/client'
import { useQuery } from '@tanstack/react-query'

const TABS = [
  { id: 'connection', label: 'Connection' },
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'control',   label: 'Control' },
  { id: 'console',   label: 'Console' },
  { id: 'heightmap', label: 'Height Map' },
  { id: 'files',     label: 'Job Files' },
  { id: 'settings',  label: 'Settings' },
] as const

/** Tabs that need the board's HTTP file endpoints, which USB cannot provide. */
const NEEDS_FILE_TRANSFER: readonly string[] = ['heightmap', 'files']

type TabId = (typeof TABS)[number]['id']

export default function Printer() {
  const machineConnected = useAppStore(s => s.machineConnected)
  const [activeTab, setActiveTab] = useState<TabId>('connection')
  const { setModel, pushTempSample, polling, setPolling } = useDuetStore()
  const pollRef = useRef<ReturnType<typeof setInterval>>()

  // Drives the connected/disconnected badge and the USB capability gate below.
  const { data: connection } = useQuery({
    queryKey: ['machine-connection'],
    queryFn: machineConnectionApi.getStatus,
    refetchInterval: 4000,
  })

  const pollStatus = useCallback(async () => {
    try {
      const model = await duetApi.getModel()
      setModel(model)
      pushTempSample(model)
    } catch {
      // poll failure is non-fatal
    }
  }, [setModel, pushTempSample])

  // Start/stop polling when connected/disconnected
  useEffect(() => {
    if (machineConnected && !polling) {
      setPolling(true)
      pollStatus() // immediate first poll
      pollRef.current = setInterval(pollStatus, 1000)
    }
    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current)
        pollRef.current = undefined
        setPolling(false)
      }
    }
  }, [machineConnected]) // eslint-disable-line react-hooks/exhaustive-deps

  const renderTab = () => {
    // Connecting lives here, in the Printer section, and stays reachable while
    // connected so the link can be inspected or dropped.
    if (activeTab === 'connection') return <PrinterConnection />

    if (!machineConnected) {
      return (
        <div className="flex flex-col items-center justify-center text-gray-500 space-y-4 py-20">
          <div className="text-6xl">🖨️</div>
          <p className="text-lg">Printer not connected</p>
          <p className="text-sm text-gray-600">
            Open the <span className="text-primary-300">Connection</span> tab to connect by
            IP address or USB.
          </p>
          <button
            onClick={() => setActiveTab('connection')}
            className="px-4 py-2 bg-primary/80 hover:bg-primary text-white rounded-lg text-sm"
          >
            Go to Connection
          </button>
        </div>
      )
    }

    // Over USB the board's file endpoints are unavailable; say so rather than
    // letting the panel fail with a raw error.
    if (NEEDS_FILE_TRANSFER.includes(activeTab) && connection && !connection.supportsFileTransfer) {
      return (
        <div className="flex flex-col items-center justify-center text-gray-500 space-y-3 py-20 text-center px-6">
          <div className="text-5xl">🔌</div>
          <p className="text-lg text-gray-300">Not available over USB</p>
          <p className="text-sm text-gray-500 max-w-md">
            This tab needs the board's HTTP file interface (file listing, upload and download),
            which RepRapFirmware only serves over the network. Connect by IP address to use it.
          </p>
          <button
            onClick={() => setActiveTab('connection')}
            className="px-4 py-2 bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-300 rounded-lg text-sm"
          >
            Change connection
          </button>
        </div>
      )
    }

    switch (activeTab) {
      case 'dashboard': return <PrinterDashboard />
      case 'control':   return <PrinterControl />
      case 'console':   return <PrinterConsole />
      case 'heightmap': return <PrinterHeightMap />
      case 'files':     return <PrinterJobFiles />
      case 'settings':  return <PrinterSettings />
    }
  }

  return (
    <div className="flex flex-col h-full">
      {/* Sub-tab bar */}
      <div className="flex border-b border-gray-800 bg-gray-900/50 px-2 shrink-0">
        {TABS.map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            className={`px-4 py-2.5 text-sm font-medium transition-colors border-b-2 -mb-px ${
              activeTab === tab.id
                ? 'border-primary-400 text-primary-300'
                : 'border-transparent text-gray-400 hover:text-gray-200 hover:border-gray-600'
            }`}
          >
            {tab.label}
            {tab.id === 'connection' && (
              <span className={`ml-2 inline-block w-1.5 h-1.5 rounded-full align-middle ${
                machineConnected ? 'bg-green-400' : 'bg-gray-600'
              }`} />
            )}
          </button>
        ))}
      </div>

      {/* Tab content */}
      <div className="flex-1 overflow-y-auto p-4">
        {renderTab()}
      </div>
    </div>
  )
}
