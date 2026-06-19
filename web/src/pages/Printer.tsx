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

const TABS = [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'control',   label: 'Control' },
  { id: 'console',   label: 'Console' },
  { id: 'heightmap', label: 'Height Map' },
  { id: 'files',     label: 'Job Files' },
  { id: 'settings',  label: 'Settings' },
] as const

type TabId = (typeof TABS)[number]['id']

export default function Printer() {
  const machineConnected = useAppStore(s => s.machineConnected)
  const [activeTab, setActiveTab] = useState<TabId>('dashboard')
  const { setModel, pushTempSample, polling, setPolling } = useDuetStore()
  const pollRef = useRef<ReturnType<typeof setInterval>>()

  const pollStatus = useCallback(async () => {
    try {
      const model = await duetApi.getModel()
      setModel(model)
      if (model.heat?.heaters?.length) {
        pushTempSample(model.heat.heaters)
      }
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

  if (!machineConnected) {
    return (
      <div className="flex flex-col items-center justify-center h-full text-gray-500 space-y-4 py-20">
        <div className="text-6xl">🖨️</div>
        <p className="text-lg">Printer not connected</p>
        <p className="text-sm text-gray-600">Go to <span className="text-primary-300">Calibration</span> to connect to your Duet board first.</p>
      </div>
    )
  }

  const renderTab = () => {
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
