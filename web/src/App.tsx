import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/layout/Layout'
import Dashboard from './pages/Dashboard'
import StlImport from './pages/StlImport'
import PrintSettings from './pages/PrintSettings'
import MachineConfig from './pages/MachineConfig'
import Materials from './pages/Materials'
import ToolLibrary from './pages/ToolLibrary'
import HybridPlanner from './pages/HybridPlanner'
import HybridPreview from './pages/HybridPreview'
import CustomGCode from './pages/CustomGCode'
import Calibration from './pages/Calibration'
import Printer from './pages/Printer'
// PelletCalibration is now a subtab inside Calibration
import BrandingPage from './pages/BrandingPage'
import GCodePage from './pages/GCodePage'
import NotFound from './pages/NotFound'
import { useBranding } from './hooks/useBranding'

function BrandingProvider({ children }: { children: React.ReactNode }) {
  useBranding()
  return <>{children}</>
}

export default function App() {
  return (
    <BrowserRouter>
      <BrandingProvider>
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<Navigate to="/dashboard" replace />} />
            <Route path="dashboard"          element={<Dashboard />} />
            <Route path="import"             element={<StlImport />} />
            <Route path="print-settings"     element={<PrintSettings />} />
            <Route path="machine-config"     element={<MachineConfig />} />
            <Route path="materials"          element={<Materials />} />
            <Route path="tools"              element={<ToolLibrary />} />
            <Route path="hybrid-planner"     element={<HybridPlanner />} />
            <Route path="hybrid-preview"     element={<HybridPreview />} />
            <Route path="custom-gcode"       element={<CustomGCode />} />
            <Route path="calibration"        element={<Calibration />} />
            <Route path="printer"           element={<Printer />} />
            {/* pellet-calibration redirects to calibration tab */}
            <Route path="pellet-calibration" element={<Navigate to="/calibration" replace />} />
            <Route path="jobs/:id/gcode"     element={<GCodePage />} />
            {import.meta.env.DEV && (
              <Route path="settings/branding" element={<BrandingPage />} />
            )}
            <Route path="*"                  element={<NotFound />} />
          </Route>
        </Routes>
      </BrandingProvider>
    </BrowserRouter>
  )
}
