import { useAppStore } from '../../store'

export default function Header() {
  const { machineConnected, extruderTemp, bedTemp } = useAppStore()

  return (
    <header className="h-12 bg-gray-900 border-b border-gray-800 shadow-sm flex items-center justify-between px-6 shrink-0">
      <div />

      <div className="flex items-center gap-6 text-sm">
        {extruderTemp !== null && (
          <span className="text-orange-400 font-medium" title="Extruder temperature">E: {extruderTemp.toFixed(1)} °C</span>
        )}
        {bedTemp !== null && (
          <span className="text-blue-400 font-medium" title="Bed temperature">B: {bedTemp.toFixed(1)} °C</span>
        )}

        <span className={`flex items-center gap-1.5 ${machineConnected ? 'text-green-400' : 'text-gray-400'}`}
          title={machineConnected ? 'Machine connected' : 'No machine connected'}>
          <span className={`w-2 h-2 rounded-full ${machineConnected ? 'bg-green-400 animate-pulse' : 'bg-gray-500'}`} />
          {machineConnected ? 'Connected' : 'Disconnected'}
        </span>
      </div>
    </header>
  )
}
