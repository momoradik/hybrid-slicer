import { NavLink } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { brandingApi } from '../../api/client'
import { useAppStore } from '../../store'

const allNavItems = [
  { to: '/dashboard',         label: 'Dashboard',      icon: '⬛', devOnly: false },
  { to: '/import',            label: 'Import STL',     icon: '📁', devOnly: false },
  { to: '/print-settings',    label: 'Print Settings', icon: '🖨️', devOnly: false },
  { to: '/machine-config',    label: 'Machine Configuration', icon: '⚙️', devOnly: false },
  { to: '/materials',         label: 'Materials',      icon: '🧵', devOnly: false },
  { to: '/tools',             label: 'Tool Library',   icon: '🔧', devOnly: false },
  { to: '/hybrid-planner',    label: 'CNC Polishing', icon: '🔀', devOnly: false },
  { to: '/hybrid-preview',    label: 'Hybrid Preview', icon: '🎬', devOnly: false },
  { to: '/custom-gcode',      label: 'G-code Customisation', icon: '📝', devOnly: false },
  { to: '/calibration',       label: 'Calibration',    icon: '📐', devOnly: false },
  { to: '/settings/branding', label: 'Branding',       icon: '🎨', devOnly: true  },
]

const navItems = allNavItems.filter(n => !n.devOnly || import.meta.env.DEV)

export default function Sidebar() {
  const { data: branding } = useQuery({
    queryKey: ['branding'],
    queryFn: brandingApi.get,
    staleTime: Infinity,
  })
  const machineConnected = useAppStore(s => s.machineConnected)

  return (
    <nav className="w-56 bg-gray-900 border-r border-gray-800 flex flex-col shrink-0">
      <div className="px-4 py-5 border-b border-gray-800">
        <h1 className="text-lg font-bold text-primary-400 truncate">
          {branding?.appTitle ?? 'HybridSlicer'}
        </h1>
        {branding?.companyName && (
          <p className="text-xs text-gray-500 mt-0.5">{branding.companyName}</p>
        )}
      </div>

      <ul className="flex-1 py-3 space-y-0.5 overflow-y-auto">
        {navItems.map(({ to, label, icon }) => (
          <li key={to}>
            <NavLink
              to={to}
              className={({ isActive }) =>
                `flex items-center gap-3 px-4 py-2.5 text-sm transition-colors rounded-md mx-2 ` +
                (isActive
                  ? 'bg-primary/20 text-primary-300 font-medium'
                  : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800')
              }
            >
              <span>{icon}</span>
              {label}
            </NavLink>
          </li>
        ))}

        {/* Printer tab — shown when machine is connected */}
        <li>
          <NavLink
            to="/printer"
            className={({ isActive }) =>
              `flex items-center gap-3 px-4 py-2.5 text-sm transition-colors rounded-md mx-2 ` +
              (isActive
                ? 'bg-primary/20 text-primary-300 font-medium'
                : machineConnected
                ? 'text-green-400 hover:text-green-300 hover:bg-gray-800'
                : 'text-gray-600 hover:text-gray-400 hover:bg-gray-800')
            }
          >
            <span className="relative">
              🖨️
              {machineConnected && (
                <span className="absolute -top-1 -right-1 w-2 h-2 bg-green-400 rounded-full" />
              )}
            </span>
            Printer
            {machineConnected && (
              <span className="ml-auto w-1.5 h-1.5 bg-green-400 rounded-full" />
            )}
          </NavLink>
        </li>
      </ul>
    </nav>
  )
}
