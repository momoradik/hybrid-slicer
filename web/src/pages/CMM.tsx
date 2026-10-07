/**
 * CMM page — dimensional inspection within the HybridSlicer shell.
 *
 * COMPUTE FIREWALL: this page only DISPLAYS values returned by the
 * alignmesh core. It never computes a measurement, deviation, or verdict.
 */
import { useState, useEffect, useRef } from 'react'
import { useQuery, useMutation } from '@tanstack/react-query'
import { alignmeshApi, type InspectionResult, type LandmarkPairData, type RPSPointData } from '../api/alignmesh'
import InspectionViewer, { type OverlayMode } from '../components/viewer/InspectionViewer'
import VerdictPanel from '../components/inspection/VerdictPanel'
import StatisticsPanel from '../components/inspection/StatisticsPanel'
import HistogramPanel from '../components/inspection/HistogramPanel'
import ObservabilityPanel from '../components/inspection/ObservabilityPanel'
import OfficialSeparation from '../components/inspection/OfficialSeparation'
import ProvenancePanel from '../components/inspection/ProvenancePanel'
import ReportPanel from '../components/inspection/ReportPanel'
import ToleranceSetup from '../components/inspection/ToleranceSetup'
import AlignmentSetup from '../components/inspection/AlignmentSetup'
import ImportPanel, { type ImportedPart, cancelPreviewMesh } from '../components/inspection/ImportPanel'
import PartPreview from '../components/inspection/PartPreview'
import ImportStatusBar from '../components/inspection/ImportStatusBar'
import AlignmentOverlay from '../components/inspection/AlignmentOverlay'
import DOFPanel from '../components/inspection/DOFPanel'
import LandmarkPairEntry from '../components/inspection/LandmarkPairEntry'
import RPSPointEntry from '../components/inspection/RPSPointEntry'
import RPSResultPanel from '../components/inspection/RPSResultPanel'
import MachineTab from '../components/inspection/MachineTab'

type Tab = 'guide' | 'probe' | 'setup' | 'verdict' | 'analysis' | 'report' | 'machine'
type Workflow = 'comparison' | 'probe-to-machine' | null

export default function CMM() {
  const [refPart, setRefPart] = useState<ImportedPart | null>(null)
  const [measPart, setMeasPart] = useState<ImportedPart | null>(null)
  const [tolerance, setTolerance] = useState(0.1)
  const [alignMode, setAlignMode] = useState('coarse-to-fine')
  const [result, setResult] = useState<InspectionResult | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [tab, setTab] = useState<Tab>('guide')
  const [workflow, setWorkflow] = useState<Workflow>(null)
  const [overlayMode, setOverlayMode] = useState<OverlayMode>('overlay')
  const [busy, setBusy] = useState(false)
  const [landmarks, setLandmarks] = useState<LandmarkPairData[]>([])
  const [rpsPoints, setRpsPoints] = useState<RPSPointData[]>([])
  const [angleStep, setAngleStep] = useState(30)
  const [processing, setProcessing] = useState(false)
  const [showRpsNominal, setShowRpsNominal] = useState(true)
  const [showRpsProjected, setShowRpsProjected] = useState(true)

  const { data: health, isError: coreDown } = useQuery({
    queryKey: ['alignmesh-health'],
    queryFn: alignmeshApi.health,
    refetchInterval: busy ? false : processing ? 30000 : 10000,
    retry: 1,
  })

  const { data: coreVersion } = useQuery({
    queryKey: ['alignmesh-version'],
    queryFn: alignmeshApi.version,
    staleTime: Infinity,
    enabled: !!health,
  })

  const inspectMutation = useMutation({
    mutationFn: async () => {
      cancelPreviewMesh()
      setBusy(true)
      setProcessing(true)
      setError(null)
      return alignmeshApi.inspect({
        reference: refPart!.path,
        measured: measPart!.path,
        tolerance,
        alignment_mode: alignMode,
        landmarks: alignMode === 'landmark' ? landmarks : undefined,
        rps_points: alignMode === 'pre-aligned-rps' ? rpsPoints : undefined,
        angle_step: angleStep,
      })
    },
    onSuccess: (data) => {
      setResult(data)
      setError(null)
      setProcessing(false)
      setBusy(false)
      setTab('verdict')
    },
    onError: (err: Error) => {
      setResult(null)
      setError('Core error: ' + (err.message || 'unknown'))
      setProcessing(false)
      setBusy(false)
    },
  })

  const handleCancel = () => {
    setProcessing(false)
    setBusy(false)
  }

  const bothImported = !!refPart && !!measPart
  const anyImported = !!refPart || !!measPart

  const tabs: { id: Tab; label: string; needsResult: boolean }[] = [
    { id: 'guide', label: 'Guide', needsResult: false },
    { id: 'probe', label: 'Connect to Probe', needsResult: false },
    { id: 'setup', label: 'Setup', needsResult: false },
    { id: 'verdict', label: 'Verdict', needsResult: true },
    { id: 'analysis', label: 'Analysis', needsResult: true },
    { id: 'report', label: 'Report', needsResult: true },
    { id: 'machine', label: 'Machine', needsResult: true },
  ]

  return (
    <>
    {processing && (
      <div className="fixed inset-0 z-50 bg-[#060a14] flex flex-col">
        <div className="flex-1 relative">
          <PartPreview reference={refPart} measured={measPart} isProcessing={true} />
          <div className="absolute bottom-0 left-0 right-0 h-20 bg-gradient-to-t from-[#060a14] to-transparent pointer-events-none" />
        </div>
        <div className="shrink-0 px-6 pb-6 pt-2 space-y-4">
          <ProcessingBar />
          <button onClick={handleCancel}
            className="w-full px-5 py-2.5 bg-gray-800/80 hover:bg-gray-700 text-gray-400 hover:text-gray-200 text-sm font-medium rounded-lg transition-colors border border-gray-700/50">
            Cancel
          </button>
        </div>
      </div>
    )}

    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-semibold text-white">CMM</h2>
        {coreDown ? (
          <span className="px-3 py-1 rounded-full text-xs font-medium bg-red-900 text-red-300 border border-red-700">Core offline</span>
        ) : (
          <span className="px-3 py-1 rounded-full text-xs font-medium bg-green-900 text-green-300 border border-green-700">Core online</span>
        )}
      </div>

      <div className="flex gap-1 bg-gray-900 rounded-lg p-1 w-fit">
        {tabs.map(t => (
          <button
            key={t.id}
            onClick={() => !(t.needsResult && !result) && setTab(t.id)}
            disabled={t.needsResult && !result}
            className={'px-4 py-1.5 rounded-md text-sm transition ' +
              (tab === t.id
                ? 'bg-primary/30 text-primary-300 font-medium'
                : 'text-gray-400 hover:text-gray-200 disabled:text-gray-600 disabled:cursor-not-allowed')}
          >
            {t.label}
          </button>
        ))}
      </div>

      {error && (
        <div className="bg-red-950 border border-red-800 rounded-xl p-4 text-red-300 text-sm">{error}</div>
      )}

      {tab === 'guide' && (
        <CmmGuide
          onSelectWorkflow={(wf) => { setWorkflow(wf); setTab(wf === 'probe-to-machine' ? 'probe' : 'setup') }}
        />
      )}

      {tab === 'probe' && <ProbeConnection />}

      {tab === 'setup' && (
        <div className="space-y-4">
          {/* Workflow context banner */}
          {workflow === 'comparison' && (
            <div className="bg-blue-950/30 border border-blue-800/40 rounded-xl p-3">
              <p className="text-xs text-blue-300 font-medium">Workflow: Comparison Only</p>
              <p className="text-[10px] text-blue-400/70 mt-0.5">
                Import your STEP/CAD file as <strong>Reference</strong> (nominal shape) and your scanned part (STL/PLY) as <strong>Measured</strong>.
                The software will align them and show you where the scanned part deviates from nominal.
              </p>
            </div>
          )}
          {workflow === 'probe-to-machine' && (
            <div className="bg-purple-950/30 border border-purple-800/40 rounded-xl p-3">
              <p className="text-xs text-purple-300 font-medium">Workflow: Probe Points to Machining</p>
              <p className="text-[10px] text-purple-400/70 mt-0.5">
                Import your STEP/CAD file as <strong>Reference</strong> (nominal shape) and your probe points file (.xyz/.pts/.asc) as <strong>Measured</strong>.
                The software will align the probe points to the CAD model, find where the part sits on your machine bed, and show excess material.
                Then go to the <strong>Machine</strong> tab to generate G-code that removes the excess.
              </p>
            </div>
          )}

          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <ImportPanel role="reference" onImport={setRefPart} imported={refPart} onBusyChange={setBusy} />
            <ImportPanel role="measured" onImport={setMeasPart} imported={measPart} onBusyChange={setBusy} />
          </div>

          {anyImported && (
            <PartPreview reference={refPart} measured={measPart} isProcessing={false} />
          )}

          {bothImported && (
            <ImportStatusBar
              result={result}
              refPath={refPart.path}
              measPath={measPart.path}
              refFormat={refPart.displayLabel}
              measFormat={measPart.displayLabel}
            />
          )}

          <ToleranceSetup onToleranceConfirmed={setTolerance} />
          <AlignmentSetup currentMode={alignMode} onModeSelected={setAlignMode}
            angleStep={angleStep} onAngleStepChanged={setAngleStep} />

          {alignMode === 'landmark' && bothImported && (
            <LandmarkPairEntry onPairsChanged={setLandmarks} />
          )}

          {alignMode === 'pre-aligned-rps' && bothImported && (
            <RPSPointEntry onPointsChanged={setRpsPoints} />
          )}

          <button
            onClick={() => inspectMutation.mutate()}
            disabled={!bothImported || tolerance <= 0 || inspectMutation.isPending || coreDown
              || (alignMode === 'landmark' && landmarks.length < 3)
              || (alignMode === 'pre-aligned-rps' && rpsPoints.length < 1)}
            className="w-full px-5 py-3 bg-primary/80 hover:bg-primary text-white text-sm font-medium rounded-lg transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
          >
            Run Inspection
          </button>
        </div>
      )}

      {tab === 'verdict' && result && (
        <div className="space-y-4">
          <VerdictPanel result={result} />
          <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
            <InspectionViewer
              result={result}
              reference={refPart}
              measured={measPart}
              overlayMode={overlayMode}
              rpsPoints={alignMode === 'pre-aligned-rps' ? rpsPoints : undefined}
              rpsProjectedPoints={alignMode === 'pre-aligned-rps' ? result.rps_projected_points : undefined}
              showRpsNominal={showRpsNominal}
              showRpsProjected={showRpsProjected}
            />
          </div>
          <div className="flex items-center gap-3">
            <div className="flex-1">
              <AlignmentOverlay result={result} mode={overlayMode} onModeChange={setOverlayMode} />
            </div>
            {alignMode === 'pre-aligned-rps' && rpsPoints.length > 0 && (
              <div className="flex gap-1.5">
                <button
                  onClick={() => setShowRpsNominal(!showRpsNominal)}
                  className={'px-2.5 py-1.5 rounded-lg text-xs transition border ' +
                    (showRpsNominal
                      ? 'bg-red-900/30 border-red-700/50 text-red-400'
                      : 'bg-gray-800 border-gray-700 text-gray-500')}
                >
                  {showRpsNominal ? 'Hide' : 'Show'} Nominal
                </button>
                <button
                  onClick={() => setShowRpsProjected(!showRpsProjected)}
                  className={'px-2.5 py-1.5 rounded-lg text-xs transition border ' +
                    (showRpsProjected
                      ? 'bg-blue-900/30 border-blue-700/50 text-blue-400'
                      : 'bg-gray-800 border-gray-700 text-gray-500')}
                >
                  {showRpsProjected ? 'Hide' : 'Show'} Projected
                </button>
              </div>
            )}
          </div>
          <DOFPanel result={result} />
          {result.alignment_mode === 'pre-aligned-rps' && result.rps_result && (
            <RPSResultPanel result={result} />
          )}
          <OfficialSeparation result={result} />
        </div>
      )}

      {tab === 'analysis' && result && (
        <div className="space-y-4">
          <StatisticsPanel result={result} />
          <HistogramPanel result={result} />
          <ObservabilityPanel result={result} />
          <ProvenancePanel result={result} />
        </div>
      )}

      {tab === 'report' && result && (
        <ReportPanel result={result} />
      )}

      {tab === 'machine' && result && (
        <MachineTab result={result} />
      )}

      {coreVersion && (
        <div className="text-xs text-gray-600 text-right">
          {coreVersion.version + ' | ' + coreVersion.fp_flags}
        </div>
      )}
    </div>
    </>
  )
}

// ═══════════════════════════════════════════════════════════════════
// Connect to Probe sub-tab
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// Guide / Tutorial
// ═══════════════════════════════════════════════════════════════════

function CmmGuide({ onSelectWorkflow }: { onSelectWorkflow: (wf: Workflow) => void }) {
  const [expanded, setExpanded] = useState<string | null>(null)
  const toggle = (id: string) => setExpanded(expanded === id ? null : id)

  return (
    <div className="max-w-3xl mx-auto space-y-5">
      {/* Header */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-6">
        <h3 className="text-lg font-semibold text-white">CMM Inspection &amp; Machining</h3>
        <p className="text-sm text-gray-400 mt-2 leading-relaxed">
          This tab lets you inspect manufactured parts against their CAD design and, if needed,
          generate G-code to machine them to the correct dimensions. Choose a workflow to begin.
        </p>
      </div>

      {/* ── Choose your workflow ────────────────────────────────────── */}
      <div className="space-y-3">
        <p className="text-xs font-semibold text-gray-500 uppercase tracking-wider px-1">Choose your workflow</p>

        {/* Workflow A */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
          <button onClick={() => toggle('wf-a')}
            className="w-full text-left px-5 py-4 flex items-center gap-3 hover:bg-gray-800/50 transition">
            <span className="w-7 h-7 rounded-lg bg-blue-900/40 border border-blue-700/40 text-blue-400 text-xs font-bold flex items-center justify-center shrink-0">A</span>
            <div className="flex-1">
              <p className="text-sm font-semibold text-white">Inspect: Compare Scanned Part vs CAD</p>
              <p className="text-[10px] text-gray-500 mt-0.5">You have a 3D scan and a STEP file. See where they differ.</p>
            </div>
            <span className="text-gray-600 text-xs">{expanded === 'wf-a' ? '\u25B2' : '\u25BC'}</span>
          </button>
          {expanded === 'wf-a' && (
            <div className="px-5 pb-5 space-y-4 border-t border-gray-800">
              <div className="pt-4 space-y-3">
                <p className="text-xs text-gray-300 font-medium">What you need:</p>
                <div className="grid grid-cols-2 gap-3">
                  <div className="bg-blue-950/20 border border-blue-800/30 rounded-lg p-3">
                    <p className="text-[10px] text-blue-400 font-semibold uppercase">Reference (nominal)</p>
                    <p className="text-xs text-gray-300 mt-1">Your STEP or STL file — the perfect design.</p>
                  </div>
                  <div className="bg-green-950/20 border border-green-800/30 rounded-lg p-3">
                    <p className="text-[10px] text-green-400 font-semibold uppercase">Measured (scanned)</p>
                    <p className="text-xs text-gray-300 mt-1">Your 3D-scanned mesh (STL or PLY).</p>
                  </div>
                </div>

                <p className="text-xs text-gray-300 font-medium mt-2">How it works:</p>
                <ol className="text-xs text-gray-400 space-y-2 list-none">
                  <Step n={1} title="Import files" desc="Go to Setup and import your STEP file as Reference and your scanned mesh as Measured." />
                  <Step n={2} title="Set tolerance" desc="Enter the acceptable deviation in mm (e.g. 0.1mm). This defines the pass/fail boundary." />
                  <Step n={3} title="Choose alignment" desc="Select 'Coarse-to-Fine' (automatic). The software uses ICP to align the scan to the CAD model." />
                  <Step n={4} title="Run Inspection" desc="Click the button. The software aligns the parts and computes per-point deviations." />
                  <Step n={5} title="Review results" desc="Verdict tab shows PASS/FAIL with a 3D heatmap. Green = within tolerance. Red/Blue = outside." />
                  <Step n={6} title="Analyse" desc="Analysis tab shows statistics (RMS, max deviation, histogram). Report tab generates a printable report." />
                </ol>

                <button onClick={() => onSelectWorkflow('comparison')}
                  className="w-full mt-2 py-2.5 bg-blue-900/40 hover:bg-blue-900/60 border border-blue-700/40 text-blue-300 text-sm font-medium rounded-lg transition">
                  Start Comparison Workflow
                </button>
              </div>
            </div>
          )}
        </div>

        {/* Workflow B */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
          <button onClick={() => toggle('wf-b')}
            className="w-full text-left px-5 py-4 flex items-center gap-3 hover:bg-gray-800/50 transition">
            <span className="w-7 h-7 rounded-lg bg-purple-900/40 border border-purple-700/40 text-purple-400 text-xs font-bold flex items-center justify-center shrink-0">B</span>
            <div className="flex-1">
              <p className="text-sm font-semibold text-white">Probe, Align &amp; Machine to Nominal</p>
              <p className="text-[10px] text-gray-500 mt-0.5">You have a part on the bed, a touch probe, and a STEP file. Machine it to the correct shape.</p>
            </div>
            <span className="text-gray-600 text-xs">{expanded === 'wf-b' ? '\u25B2' : '\u25BC'}</span>
          </button>
          {expanded === 'wf-b' && (
            <div className="px-5 pb-5 space-y-4 border-t border-gray-800">
              <div className="pt-4 space-y-3">
                <p className="text-xs text-gray-300 font-medium">What you need:</p>
                <div className="grid grid-cols-3 gap-3">
                  <div className="bg-blue-950/20 border border-blue-800/30 rounded-lg p-3">
                    <p className="text-[10px] text-blue-400 font-semibold uppercase">Reference</p>
                    <p className="text-xs text-gray-300 mt-1">STEP file of the nominal design.</p>
                  </div>
                  <div className="bg-green-950/20 border border-green-800/30 rounded-lg p-3">
                    <p className="text-[10px] text-green-400 font-semibold uppercase">Probe points</p>
                    <p className="text-xs text-gray-300 mt-1">XYZ file from your touch probe.</p>
                  </div>
                  <div className="bg-purple-950/20 border border-purple-800/30 rounded-lg p-3">
                    <p className="text-[10px] text-purple-400 font-semibold uppercase">Machine + Tool</p>
                    <p className="text-xs text-gray-300 mt-1">Machine profile and CNC tool from your library.</p>
                  </div>
                </div>

                <p className="text-xs text-gray-300 font-medium mt-2">Step-by-step:</p>
                <ol className="text-xs text-gray-400 space-y-2 list-none">
                  <Step n={1} title="Collect probe points" desc={
                    'Probe the part on your Duet/RepRap machine using G30 commands. Record the X, Y, Z coordinates ' +
                    'reported by the firmware. Save them in a text file (.xyz) — one point per line, space or comma separated. ' +
                    'You need at least 10-20 points spread across the part surface for a good alignment.'
                  } />
                  <Step n={2} title="Import files in Setup" desc={
                    'In the Setup tab, import your STEP file as Reference (blue panel) and your probe points file as Measured (green panel). ' +
                    'The software uploads both to the inspection core.'
                  } />
                  <Step n={3} title="Set tolerance" desc={
                    'Enter your tolerance (e.g. 0.1mm). A tighter tolerance means the alignment algorithm works harder to find the best fit. ' +
                    'This controls precision, not speed — lower values may produce more FAIL/WARNING results.'
                  } />
                  <Step n={4} title="Run Inspection" desc={
                    'Click "Run Inspection". The software aligns your probe points to the CAD model using ICP (Iterative Closest Point). ' +
                    'This finds the rigid transform (rotation + translation) that tells you exactly where the part sits on the bed.'
                  } />
                  <Step n={5} title="Review alignment in Verdict" desc={
                    'The Verdict tab shows the alignment result. The 3D heatmap colours each probe point by its deviation from nominal. ' +
                    'Positive deviation (red/yellow) = the part sticks out past the CAD surface = excess material to remove. ' +
                    'Negative deviation (blue/cyan) = the part is below the CAD surface = already correct or undersize. ' +
                    'Green = within your tolerance.'
                  } />
                  <Step n={6} title="Generate machining G-code" desc={
                    'Go to the Machine tab. Select your machine profile and CNC tool from the dropdowns. ' +
                    'Set the deviation threshold (minimum excess before machining), stepover %, and max depth per pass. ' +
                    'Click "Generate Machining G-code". The software creates a raster/zigzag toolpath that removes ' +
                    'only the excess material, bringing the part to its nominal dimensions.'
                  } />
                  <Step n={7} title="Send G-code to machine" desc={
                    'Download the G-code file. Upload it to your Duet board via the Printer tab or DWC. ' +
                    'The G-code uses only G0/G1 moves (no arcs), M3/M5 for the spindle, and respects your machine\'s CNC axis and offset configuration.'
                  } />
                </ol>

                <button onClick={() => onSelectWorkflow('probe-to-machine')}
                  className="w-full mt-2 py-2.5 bg-purple-900/40 hover:bg-purple-900/60 border border-purple-700/40 text-purple-300 text-sm font-medium rounded-lg transition">
                  Start Probe-to-Machine Workflow
                </button>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* ── How probe points are collected ──────────────────────────── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
        <button onClick={() => toggle('probe-format')}
          className="w-full text-left px-5 py-3 flex items-center justify-between hover:bg-gray-800/50 transition">
          <p className="text-xs font-semibold text-gray-300">How to collect and format probe points</p>
          <span className="text-gray-600 text-xs">{expanded === 'probe-format' ? '\u25B2' : '\u25BC'}</span>
        </button>
        {expanded === 'probe-format' && (
          <div className="px-5 pb-5 border-t border-gray-800 pt-4 space-y-3">
            <p className="text-xs text-gray-400 leading-relaxed">
              Use your Duet/RepRap touch probe to collect surface points on the part. Run <code className="text-gray-300 bg-gray-800 px-1 rounded">G30</code> at
              different positions across the part surface. The firmware reports the trigger position, for example:
            </p>
            <div className="bg-gray-950 rounded-lg p-3 font-mono text-[10px] text-yellow-400/80">
              <p className="text-gray-500">; Duet console output after G30</p>
              <p>Probe triggered at X:105.200 Y:82.100 Z:15.300</p>
            </div>
            <p className="text-xs text-gray-400 leading-relaxed">
              Record the X, Y, Z values and save them in a plain text file, one point per line:
            </p>
            <div className="bg-gray-950 rounded-lg p-3 font-mono text-[10px] text-gray-300">
              <p className="text-gray-500"># probe_points.xyz</p>
              <p className="text-gray-500"># Probed 2026-10-07, Renishaw TP20 on Duet 3</p>
              <p>105.200  82.100  15.300</p>
              <p>105.800  83.400  15.100</p>
              <p>106.100  81.900  14.800</p>
              <p>107.300  80.500  15.400</p>
              <p>104.900  84.200  14.600</p>
              <p className="text-gray-500"># ...</p>
            </div>
            <div className="text-[10px] text-gray-500 space-y-1">
              <p><strong>Accepted extensions:</strong> .xyz, .pts, .asc</p>
              <p><strong>Separators:</strong> spaces, tabs, or commas</p>
              <p><strong>Comments:</strong> lines starting with # or // are ignored</p>
              <p><strong>Minimum points:</strong> 3 required, but 10-20+ recommended for reliable alignment</p>
              <p><strong>Coordinates:</strong> must be in millimetres, in machine coordinate system</p>
            </div>

            <p className="text-xs text-gray-400 leading-relaxed mt-2">
              <strong>Tip:</strong> For automated collection, write a Duet macro that loops over a grid of XY positions,
              runs <code className="text-gray-300 bg-gray-800 px-1 rounded">G30</code> at each, and logs the result to a file on the SD card.
            </p>
          </div>
        )}
      </div>

      {/* ── Understanding the results ──────────────────────────────── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
        <button onClick={() => toggle('results')}
          className="w-full text-left px-5 py-3 flex items-center justify-between hover:bg-gray-800/50 transition">
          <p className="text-xs font-semibold text-gray-300">Understanding the results</p>
          <span className="text-gray-600 text-xs">{expanded === 'results' ? '\u25B2' : '\u25BC'}</span>
        </button>
        {expanded === 'results' && (
          <div className="px-5 pb-5 border-t border-gray-800 pt-4 space-y-3">
            <p className="text-xs text-gray-300 font-medium">Verdict</p>
            <div className="grid grid-cols-2 md:grid-cols-4 gap-2 text-[10px]">
              <div className="bg-green-950/30 border border-green-800/30 rounded-lg p-2 text-center">
                <p className="text-green-400 font-bold text-sm">PASS</p>
                <p className="text-gray-400 mt-1">All deviations within tolerance</p>
              </div>
              <div className="bg-yellow-950/30 border border-yellow-800/30 rounded-lg p-2 text-center">
                <p className="text-yellow-400 font-bold text-sm">WARNING</p>
                <p className="text-gray-400 mt-1">Within tolerance but close to the limit</p>
              </div>
              <div className="bg-red-950/30 border border-red-800/30 rounded-lg p-2 text-center">
                <p className="text-red-400 font-bold text-sm">FAIL</p>
                <p className="text-gray-400 mt-1">Deviations exceed tolerance</p>
              </div>
              <div className="bg-gray-800/50 border border-gray-700/30 rounded-lg p-2 text-center">
                <p className="text-gray-400 font-bold text-sm">INVALID</p>
                <p className="text-gray-500 mt-1">Measurement problem (not enough data, bad alignment)</p>
              </div>
            </div>

            <p className="text-xs text-gray-300 font-medium mt-3">Heatmap colours</p>
            <div className="flex items-center gap-2 text-[10px] text-gray-400">
              <div className="h-4 flex-1 rounded" style={{
                background: 'linear-gradient(to right, #ff00ff, #0066ee, #00cc88, #22ee22, #88cc00, #cc0000, #ff00ff)'
              }} />
            </div>
            <div className="flex justify-between text-[10px] text-gray-500">
              <span>Negative (below nominal)</span>
              <span className="text-green-500">Zero (perfect)</span>
              <span>Positive (excess material)</span>
            </div>

            <p className="text-xs text-gray-300 font-medium mt-3">Key statistics</p>
            <div className="text-[10px] text-gray-400 space-y-1">
              <p><strong>RMS</strong> — Root mean square of all deviations. Single number summarising overall quality.</p>
              <p><strong>Max</strong> — Worst-case deviation. The single point furthest from nominal.</p>
              <p><strong>% within tolerance</strong> — What fraction of the surface passes.</p>
              <p><strong>Alignment RMS</strong> — How well the ICP alignment converged (lower = better fit).</p>
            </div>
          </div>
        )}
      </div>

      {/* ── Machining G-code ───────────────────────────────────────── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
        <button onClick={() => toggle('machining')}
          className="w-full text-left px-5 py-3 flex items-center justify-between hover:bg-gray-800/50 transition">
          <p className="text-xs font-semibold text-gray-300">How machining G-code is generated</p>
          <span className="text-gray-600 text-xs">{expanded === 'machining' ? '\u25B2' : '\u25BC'}</span>
        </button>
        {expanded === 'machining' && (
          <div className="px-5 pb-5 border-t border-gray-800 pt-4 space-y-3">
            <p className="text-xs text-gray-400 leading-relaxed">
              After alignment, the software knows the exact position and deviation at every measured point.
              Points with <strong>positive deviation</strong> (excess material) are candidates for machining.
            </p>

            <p className="text-xs text-gray-300 font-medium">What the Machine tab does:</p>
            <ol className="text-xs text-gray-400 space-y-2 list-none">
              <Step n={1} title="Filters excess points" desc={
                'Only points where deviation > your threshold are considered. This lets you ignore tiny imperfections.'
              } />
              <Step n={2} title="Converts to machine coordinates" desc={
                'The alignment transform tells us where the part sits on the bed. The software converts all ' +
                'nominal-frame positions back to machine coordinates so the G-code targets the right physical locations.'
              } />
              <Step n={3} title="Creates a raster grid" desc={
                'The excess area is divided into a grid based on the tool stepover (e.g. 40% of tool diameter). ' +
                'Each cell records how much material needs removing.'
              } />
              <Step n={4} title="Computes Z passes" desc={
                'If the excess is deeper than the max depth of cut, multiple passes are generated. ' +
                'Each pass removes one layer, working from the top down to the nominal surface.'
              } />
              <Step n={5} title="Generates zigzag toolpath" desc={
                'For each Z level, the tool rasters back and forth across the excess area. ' +
                'Areas with no excess are skipped (tool retracts to safe height and rapids past them).'
              } />
            </ol>

            <p className="text-xs text-gray-300 font-medium mt-3">Machining parameters explained:</p>
            <div className="text-[10px] text-gray-400 space-y-1">
              <p><strong>Deviation threshold</strong> — Minimum excess (mm) before the tool cuts. Set higher to only machine the worst areas.</p>
              <p><strong>Stepover %</strong> — Distance between raster lines as % of tool diameter. Lower = smoother surface, longer time.</p>
              <p><strong>Max depth per pass</strong> — How deep each cutting pass goes (mm). Limited by tool flute length and rigidity.</p>
              <p><strong>Finish allowance</strong> — Stock left on the final pass (mm). Set to 0 to machine right to nominal.</p>
            </div>

            <div className="bg-yellow-950/20 border border-yellow-800/30 rounded-lg p-3 mt-3">
              <p className="text-[10px] text-yellow-400 font-medium">3-Axis constraint</p>
              <p className="text-[10px] text-yellow-500/70 mt-0.5">
                The tool approaches from above (Z+). Only upward-facing surfaces can be machined.
                Vertical walls, undercuts, and overhangs are skipped. If the probe could reach a spot, the tool can machine it.
              </p>
            </div>

            <p className="text-xs text-gray-300 font-medium mt-3">G-code compatibility</p>
            <p className="text-[10px] text-gray-400">
              The output uses only G0 (rapid), G1 (linear cut), M3/M5 (spindle on/off), and G4 (dwell). No G2/G3 arcs.
              This is compatible with RepRapFirmware, Marlin, and all common 3-axis controllers.
              CNC axis remapping and offsets from your machine profile are applied automatically.
            </p>
          </div>
        )}
      </div>

      {/* ── File format reference ──────────────────────────────────── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
        <button onClick={() => toggle('formats')}
          className="w-full text-left px-5 py-3 flex items-center justify-between hover:bg-gray-800/50 transition">
          <p className="text-xs font-semibold text-gray-300">Supported file formats</p>
          <span className="text-gray-600 text-xs">{expanded === 'formats' ? '\u25B2' : '\u25BC'}</span>
        </button>
        {expanded === 'formats' && (
          <div className="px-5 pb-5 border-t border-gray-800 pt-4 space-y-3">
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs text-gray-400">
              <div className="space-y-2">
                <p className="text-gray-300 font-medium">Reference Part (nominal shape)</p>
                <div className="space-y-1 text-[10px]">
                  <p><span className="text-blue-400 font-mono">.step / .stp</span> — CAD B-rep geometry. Best accuracy — uses analytic surfaces (planes, cylinders, NURBS) instead of triangles.</p>
                  <p><span className="text-blue-400 font-mono">.stl</span> — Tessellated triangle mesh. Float32 precision. OK for tolerances above 0.1mm.</p>
                  <p><span className="text-blue-400 font-mono">.ply</span> — Polygon mesh. Supports double precision coordinates.</p>
                </div>
              </div>
              <div className="space-y-2">
                <p className="text-gray-300 font-medium">Measured Part</p>
                <div className="space-y-1 text-[10px]">
                  <p><span className="text-green-400 font-mono">.stl / .ply</span> — 3D-scanned mesh from a scanner (e.g. structured light, laser).</p>
                  <p><span className="text-green-400 font-mono">.xyz / .pts / .asc</span> — Point cloud from a touch probe. One XYZ coordinate per line.</p>
                </div>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

function Step({ n, title, desc }: { n: number; title: string; desc: string }) {
  return (
    <li className="flex gap-3">
      <span className="w-5 h-5 rounded-full bg-gray-800 border border-gray-700 text-gray-400 text-[10px] font-bold flex items-center justify-center shrink-0 mt-0.5">{n}</span>
      <div>
        <span className="text-gray-200 font-medium">{title}</span>
        <span className="text-gray-500"> — </span>
        <span>{desc}</span>
      </div>
    </li>
  )
}

const PROBE_LS_KEY = 'hybridslicer.probe'

interface ProbeSettings {
  transport: 'network' | 'usb'
  host: string
  port: number
  comPort: string
  baudRate: number
  probeModel: string
}

const PROBE_DEFAULTS: ProbeSettings = {
  transport: 'network',
  host: '',
  port: 8080,
  comPort: '',
  baudRate: 115200,
  probeModel: '',
}

const PROBE_BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 460800]

const PROBE_MODELS = [
  { id: 'renishaw-tp20', name: 'Renishaw TP20', type: 'Touch-trigger' },
  { id: 'renishaw-tp200', name: 'Renishaw TP200', type: 'Touch-trigger' },
  { id: 'renishaw-sp25m', name: 'Renishaw SP25M', type: 'Scanning' },
  { id: 'renishaw-revo', name: 'Renishaw REVO', type: '5-axis scanning' },
  { id: 'zeiss-vast-xxt', name: 'Zeiss VAST XXT', type: 'Scanning' },
  { id: 'hexagon-hp-l', name: 'Hexagon HP-L', type: 'Laser scanning' },
  { id: 'keyence-lt', name: 'Keyence LT', type: 'Laser displacement' },
  { id: 'custom', name: 'Custom / Other', type: '' },
]

function loadProbeSettings(): ProbeSettings {
  try {
    const raw = localStorage.getItem(PROBE_LS_KEY)
    return raw ? { ...PROBE_DEFAULTS, ...JSON.parse(raw) } : PROBE_DEFAULTS
  } catch { return PROBE_DEFAULTS }
}

function ProbeConnection() {
  const [form, setForm] = useState<ProbeSettings>(loadProbeSettings)
  const [status, setStatus] = useState<'disconnected' | 'connecting' | 'connected' | 'error'>('disconnected')
  const [error, setError] = useState<string | null>(null)
  const [comPorts, setComPorts] = useState<string[]>([])
  const [scanning, setScanning] = useState(false)

  const set = <K extends keyof ProbeSettings>(k: K, v: ProbeSettings[K]) =>
    setForm(f => {
      const next = { ...f, [k]: v }
      try { localStorage.setItem(PROBE_LS_KEY, JSON.stringify(next)) } catch { /* private mode */ }
      return next
    })

  const scanPorts = async () => {
    setScanning(true)
    try {
      const resp = await fetch('/api/machine/serial-ports')
      if (resp.ok) {
        const data = await resp.json()
        setComPorts(data.ports ?? [])
      }
    } catch { /* ignore */ }
    setScanning(false)
  }

  const connect = async () => {
    setStatus('connecting')
    setError(null)
    // Placeholder — real probe connection logic will be wired to a backend endpoint
    await new Promise(r => setTimeout(r, 1500))
    setStatus('error')
    setError('Probe connection not yet implemented. Hardware integration is required.')
  }

  const disconnect = () => {
    setStatus('disconnected')
    setError(null)
  }

  const connected = status === 'connected'
  const selectedModel = PROBE_MODELS.find(m => m.id === form.probeModel)

  const canConnect = form.probeModel &&
    ((form.transport === 'network' && form.host.trim()) ||
     (form.transport === 'usb' && form.comPort))

  return (
    <div className="max-w-3xl mx-auto space-y-5">
      {/* Status */}
      <div className={`rounded-xl border p-5 ${
        connected ? 'bg-green-950/30 border-green-800/60'
        : status === 'error' ? 'bg-red-950/30 border-red-800/60'
        : 'bg-gray-900 border-gray-800'
      }`}>
        <div className="flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-3">
            <span className={`w-2.5 h-2.5 rounded-full ${
              connected ? 'bg-green-400'
              : status === 'connecting' ? 'bg-yellow-400 animate-pulse'
              : status === 'error' ? 'bg-red-400'
              : 'bg-gray-600'
            }`} />
            <div>
              <p className={`font-semibold ${
                connected ? 'text-green-300'
                : status === 'error' ? 'text-red-300'
                : 'text-gray-300'
              }`}>
                {status === 'connected' ? 'Probe connected' :
                 status === 'connecting' ? 'Connecting...' :
                 status === 'error' ? 'Connection failed' :
                 'Probe not connected'}
              </p>
              {connected && selectedModel && (
                <p className="text-xs text-gray-400 mt-0.5">
                  {selectedModel.name} · {form.transport === 'usb' ? 'USB' : 'Network'} ·{' '}
                  {form.transport === 'usb' ? form.comPort : `${form.host}:${form.port}`}
                </p>
              )}
            </div>
          </div>

          {connected && (
            <button
              onClick={disconnect}
              className="px-4 py-2 bg-red-900/50 hover:bg-red-900/80 border border-red-800 text-red-200 rounded-lg text-sm"
            >
              Disconnect
            </button>
          )}
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="bg-red-950/50 border border-red-800 rounded-xl p-4 space-y-1.5">
          <p className="text-sm font-medium text-red-300">Connection error</p>
          <p className="text-xs text-red-300/90">{error}</p>
        </div>
      )}

      {/* Setup form */}
      {!connected && (
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-5">
          <h3 className="text-sm font-semibold text-white">Connect to your probe</h3>

          {/* Probe model */}
          <div className="space-y-1.5">
            <label className="text-xs font-medium text-gray-300">Probe model</label>
            <select
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200"
              value={form.probeModel}
              onChange={e => set('probeModel', e.target.value)}
            >
              <option value="">-- select probe --</option>
              {PROBE_MODELS.map(m => (
                <option key={m.id} value={m.id}>
                  {m.name}{m.type ? ` (${m.type})` : ''}
                </option>
              ))}
            </select>
          </div>

          {/* Transport */}
          <div className="space-y-1.5">
            <label className="text-xs font-medium text-gray-300">Connection type</label>
            <div className="flex gap-2">
              <button
                onClick={() => set('transport', 'network')}
                className={`flex-1 px-4 py-3 rounded-lg border text-left transition ${
                  form.transport === 'network'
                    ? 'bg-primary/20 border-primary/60 text-white'
                    : 'bg-gray-950 border-gray-800 text-gray-400 hover:border-gray-700'
                }`}
              >
                <span className="block text-sm font-medium">Network (IP)</span>
                <span className="block text-[10px] opacity-70 mt-0.5">Ethernet or Wi-Fi</span>
              </button>
              <button
                onClick={() => { set('transport', 'usb'); scanPorts() }}
                className={`flex-1 px-4 py-3 rounded-lg border text-left transition ${
                  form.transport === 'usb'
                    ? 'bg-primary/20 border-primary/60 text-white'
                    : 'bg-gray-950 border-gray-800 text-gray-400 hover:border-gray-700'
                }`}
              >
                <span className="block text-sm font-medium">USB (COM port)</span>
                <span className="block text-[10px] opacity-70 mt-0.5">Direct cable</span>
              </button>
            </div>
          </div>

          {/* Network fields */}
          {form.transport === 'network' && (
            <div className="space-y-4">
              <div className="grid grid-cols-3 gap-4">
                <div className="col-span-2">
                  <label className="text-xs font-medium text-gray-300">IP address or hostname</label>
                  <input
                    className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200 font-mono mt-1.5"
                    placeholder="192.168.1.50"
                    value={form.host}
                    onChange={e => set('host', e.target.value)}
                    onKeyDown={e => { if (e.key === 'Enter' && canConnect) connect() }}
                  />
                </div>
                <div>
                  <label className="text-xs font-medium text-gray-300">Port</label>
                  <input
                    type="number" min={1} max={65535}
                    className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200 mt-1.5"
                    value={form.port}
                    onChange={e => set('port', Number(e.target.value))}
                  />
                </div>
              </div>
            </div>
          )}

          {/* USB fields */}
          {form.transport === 'usb' && (
            <div className="space-y-4">
              <div className="grid grid-cols-3 gap-4">
                <div className="col-span-2">
                  <label className="text-xs font-medium text-gray-300">COM port</label>
                  <div className="flex gap-2 mt-1.5">
                    <select
                      className="flex-1 bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200"
                      value={form.comPort}
                      onChange={e => set('comPort', e.target.value)}
                    >
                      <option value="">
                        {comPorts.length === 0 ? '-- no COM ports detected --' : '-- select a port --'}
                      </option>
                      {comPorts.map(p => <option key={p} value={p}>{p}</option>)}
                    </select>
                    <button
                      onClick={scanPorts}
                      className="px-3 py-2 bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-300 rounded-lg text-xs whitespace-nowrap"
                    >
                      {scanning ? '...' : 'Refresh'}
                    </button>
                  </div>
                </div>
                <div>
                  <label className="text-xs font-medium text-gray-300">Baud rate</label>
                  <select
                    className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200 mt-1.5"
                    value={form.baudRate}
                    onChange={e => set('baudRate', Number(e.target.value))}
                  >
                    {PROBE_BAUD_RATES.map(b => <option key={b} value={b}>{b}</option>)}
                  </select>
                </div>
              </div>

              {comPorts.length === 0 && (
                <p className="text-xs text-gray-500 bg-gray-950 border border-gray-800 rounded-lg p-3">
                  No COM ports found. Check the USB cable is a data cable (not charge-only),
                  that the probe controller is powered, and that its driver is installed — then press Refresh.
                </p>
              )}
            </div>
          )}

          {/* Connect button */}
          <button
            onClick={connect}
            disabled={!canConnect || status === 'connecting'}
            className="w-full py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm font-medium"
          >
            {status === 'connecting' ? 'Connecting...' : 'Connect'}
          </button>
        </div>
      )}
    </div>
  )
}

const STAGES = [
  { name: 'Importing reference (STEP can take minutes)', duration: 270 },
  { name: 'Importing measured mesh', duration: 10 },
  { name: 'Validating + downsampling', duration: 5 },
  { name: 'Aligning (coarse)', duration: 5 },
  { name: 'Aligning (fine ICP)', duration: 5 },
  { name: 'RPS coupling loop', duration: 5 },
  { name: 'Computing deviations', duration: 25 },
  { name: 'Analyzing results', duration: 5 },
]
const TOTAL_EST = STAGES.reduce((s, st) => s + st.duration, 0)

function ProcessingBar() {
  const [elapsed, setElapsed] = useState(0)
  const startRef = useRef(Date.now())

  useEffect(() => {
    startRef.current = Date.now()
    const id = setInterval(() => setElapsed(Math.floor((Date.now() - startRef.current) / 1000)), 200)
    return () => clearInterval(id)
  }, [])

  const progress = Math.min(0.95, elapsed / TOTAL_EST)
  const pct = Math.round(progress * 100)
  const remaining = Math.max(0, TOTAL_EST - elapsed)

  let cumulative = 0
  let currentStage = STAGES[STAGES.length - 1].name
  for (const st of STAGES) {
    cumulative += st.duration
    if (elapsed < cumulative) { currentStage = st.name; break }
  }

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <div className="w-5 h-5 border-2 border-blue-500/30 border-t-blue-400 rounded-full animate-spin" />
          <span className="text-gray-200 text-sm font-medium">{currentStage}...</span>
        </div>
        <span className="text-gray-500 text-xs font-mono">{elapsed}s</span>
      </div>
      <div className="w-full bg-gray-800/60 rounded-full h-2.5 overflow-hidden backdrop-blur-sm">
        <div
          className="h-full rounded-full transition-all duration-500 ease-out"
          style={{
            width: pct + '%',
            background: 'linear-gradient(90deg, #3b82f6, #8b5cf6, #06b6d4)',
            boxShadow: '0 0 20px rgba(59,130,246,0.5), 0 0 40px rgba(139,92,246,0.3)',
          }}
        />
      </div>
      <div className="flex items-center justify-between text-xs">
        <span className="text-gray-400">{pct}%</span>
        <span className="text-gray-500">
          {remaining > 0 ? '~' + remaining + 's remaining' : 'Finishing up...'}
        </span>
      </div>
    </div>
  )
}
