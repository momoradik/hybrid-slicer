import { useState, useCallback, useEffect, useRef } from 'react'
import { Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { printProfilesApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import InfoTip from '../components/InfoTip'
import type { PrintProfile, ApiError } from '../types'

// ── Cura-derived "Auto" defaults ────────────────────────────────────────────
// These replicate Cura's built-in formula defaults so the "Auto" button
// restores a real Cura-effective value, not a hardcoded guess.
//
//   layer_height    = nozzle * 0.5   (Cura default for standard nozzles)
//   line_width      = nozzle * 1.0
//   speed_print     = 60 mm/s        (Cura 5.x default)
//   speed_travel    = 150 mm/s
//   speed_wall_0    = speed_print * 0.5  (outer wall: half print speed)
//   speed_wall_x    = speed_print        (inner wall: same as print speed)
//   speed_infill    = speed_print * 1.5  (infill faster)
//   speed_layer_0   = 20 mm/s           (first layer slower)
//   material_flow   = 100 %

// Cura's own defaults for the values that aren't derived from anything.
const CURA_NOZZLE_MM = 0.4
const CURA_PRINT_SPEED = 60

function curaDefaults(nozzle: number, printSpeed: number) {
  return {
    nozzleDiameterMm:  CURA_NOZZLE_MM,
    layerHeightMm:     +(nozzle * 0.5).toFixed(2),
    lineWidthMm:       +nozzle.toFixed(2),
    // Print speed is a root value, not derived — Auto must restore Cura's default
    // rather than echo back whatever is already in the box.
    printSpeedMmS:     CURA_PRINT_SPEED,
    travelSpeedMmS:    150,
    wallSpeedMmS:      +(printSpeed * 0.5).toFixed(1),
    innerWallSpeedMmS: +printSpeed.toFixed(1),
    infillSpeedMmS:    +(printSpeed * 1.5).toFixed(1),
    topBottomSpeedMmS: +(printSpeed * 0.5).toFixed(1),
    firstLayerSpeedMmS: 20,
    firstLayerTravelSpeedMmS: 50,
    materialFlowPct:   100,
    coolingFanSpeedPct: 100,
    minLayerTimeSec:   5,
    minSpeedMmS:       10,
    // Pellet tuning — CuraEngine's documented defaults
    pressureAdvanceFactor:         0.05,
    flowRateCompensationFactorPct: 100,
    flowRateMaxExtrusionOffsetMm:  0,
    materialMaxFlowRateMm3S:       16,
    flowEqualizationRatioPct:      100,
    initialLayerFlowPct:           100,
    standbyTemperatureDegC:        150,
  }
}

type DraftProfile = Partial<PrintProfile> & { name: string }

const EMPTY: DraftProfile = {
  name: '',
  nozzleDiameterMm: 0.4,
  layerHeightMm: 0.2,
  lineWidthMm: 0.4,
  materialFlowPct: 100,
  printSpeedMmS: 60,
  travelSpeedMmS: 150,
  wallSpeedMmS: 30,
  innerWallSpeedMmS: 60,
  infillSpeedMmS: 90,
  topBottomSpeedMmS: 30,
  firstLayerSpeedMmS: 20,
  firstLayerTravelSpeedMmS: 50,
  wallCount: 3,
  topLayers: 4,
  bottomLayers: 4,
  infillDensityPct: 20,
  infillPattern: 'grid',
  printTemperatureDegC: 210,
  bedTemperatureDegC: 60,
  retractionEnabled: true,
  retractLengthMm: 5,
  retractSpeedMmS: 45,
  retractMinTravelMm: 1.5,
  coolingEnabled: true,
  coolingFanSpeedPct: 100,
  minLayerTimeSec: 5,
  minSpeedMmS: 10,
  accelerationControlEnabled: false,
  jerkControlEnabled: false,
  skinMonotonic: true,
  supportEnabled: false,
  pelletModeEnabled: false,
  virtualFilamentDiameterMm: 1.0,
  pressureAdvanceFactor: 0.05,
  flowRateCompensationFactorPct: 100,
  flowRateMaxExtrusionOffsetMm: 0,
  materialMaxFlowRateMm3S: 16,
  flowEqualizationRatioPct: 100,
  initialLayerFlowPct: 100,
  standbyTemperatureDegC: 150,
}

export default function PrintSettings() {
  const qc = useQueryClient()
  const { data: profiles = [] } = useQuery({ queryKey: ['printProfiles'], queryFn: printProfilesApi.getAll })
  const [draft, setDraft]       = useState<DraftProfile | null>(null)
  const [advanced, setAdvanced] = useState(false)
  const [toast, setToast]       = useState<{ msg: string; ok: boolean } | null>(null)
  const [saveError, setSaveError] = useState<ApiError | null>(null)

  const showToast = (msg: string, ok = true) => {
    setToast({ msg, ok })
    setTimeout(() => setToast(null), 3000)
  }

  // The API's error envelope is {code, message, field, providedValue, expected}.
  // Keep the whole thing so the form can name the offending setting.
  const onSaveError = (e: any) => {
    const data: ApiError | undefined = e?.response?.data
    setSaveError(data ?? { code: 'NETWORK', message: e?.message ?? 'Could not reach the server.' })
    showToast(data?.message ?? 'Save failed', false)
  }

  const createMutation = useMutation({
    mutationFn: (d: DraftProfile) => printProfilesApi.create(d),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['printProfiles'] }); setDraft(null); setSaveError(null); showToast('Profile created') },
    onError: onSaveError,
  })
  const updateMutation = useMutation({
    mutationFn: (d: DraftProfile) => printProfilesApi.update(d.id!, d),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['printProfiles'] }); setDraft(null); setSaveError(null); showToast('Settings saved') },
    onError: onSaveError,
  })
  const deleteMutation = useMutation({
    mutationFn: (id: string) => printProfilesApi.delete(id),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['printProfiles'] }),
  })

  const set = useCallback(<K extends keyof DraftProfile>(key: K, val: DraftProfile[K]) =>
    setDraft(d => d ? { ...d, [key]: val } : d), [])

  const applyAuto = (field: keyof ReturnType<typeof curaDefaults>) => {
    if (!draft) return
    const nozzle = draft.nozzleDiameterMm ?? 0.4
    const printSpd = draft.printSpeedMmS ?? 60
    const defaults = curaDefaults(nozzle, printSpd)
    set(field as keyof DraftProfile, defaults[field] as any)
  }

  const applyAllAuto = () => {
    if (!draft) return
    const nozzle = draft.nozzleDiameterMm ?? 0.4
    const printSpd = draft.printSpeedMmS ?? 60
    const d = curaDefaults(nozzle, printSpd)
    setDraft(prev => prev ? { ...prev, ...d } : prev)
  }

  const save = () => {
    if (!draft) return
    setSaveError(null)
    if (draft.id) updateMutation.mutate(draft)
    else createMutation.mutate(draft)
  }

  /** True when the server rejected this specific field — used to outline the input. */
  const badField = (name: keyof DraftProfile) => saveError?.field === name

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-semibold text-white">Print Settings</h2>
        <button
          onClick={() => { setDraft({ ...EMPTY }); setAdvanced(false) }}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg"
        >
          + New Profile
        </button>
      </div>

      {/* Profile list */}
      <div className="grid grid-cols-3 gap-4">
        {profiles.map(p => (
          <div key={p.id}
            className="bg-gray-900 border border-gray-800 hover:border-gray-600 rounded-xl p-4 transition space-y-2"
          >
            <div className="flex items-start justify-between gap-2">
              <p className="font-medium text-white truncate">{p.name}</p>
              <div className="flex gap-1 shrink-0">
                <button onClick={() => { setDraft({ ...EMPTY, ...p }); setAdvanced(false) }}
                  className="px-2 py-0.5 text-xs bg-gray-800 hover:bg-gray-700 text-gray-300 rounded">
                  Edit
                </button>
                <button onClick={() => deleteMutation.mutate(p.id)}
                  className="px-2 py-0.5 text-xs bg-red-900/40 hover:bg-red-900/60 text-red-400 rounded">
                  ✕
                </button>
              </div>
            </div>
            <div className="flex flex-wrap gap-1.5 text-[10px]">
              {p.nozzleDiameterMm > 0 && <Chip color="violet">Ø{p.nozzleDiameterMm} mm nozzle</Chip>}
              <Chip color="blue">LH {p.layerHeightMm} mm</Chip>
              <Chip color="orange">{p.printSpeedMmS} mm/s</Chip>
              <Chip color="green">Flow {p.materialFlowPct}%</Chip>
              <Chip color="gray">{p.printTemperatureDegC}°C</Chip>
              {p.pelletModeEnabled && <Chip color="amber">Pellet Mode</Chip>}
            </div>
          </div>
        ))}
        {profiles.length === 0 && (
          <p className="col-span-3 text-gray-600 text-sm">No profiles yet — create one to get started.</p>
        )}
      </div>

      {/* Editor modal */}
      {draft && (
        <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50">
          <div className="bg-gray-900 border border-gray-700 rounded-xl w-full max-w-2xl max-h-[92vh] overflow-y-auto">
            <div className="sticky top-0 bg-gray-900 border-b border-gray-800 px-6 py-4 flex items-center justify-between">
              <h3 className="font-semibold text-white">{draft.id ? 'Edit' : 'New'} Print Profile</h3>
              <div className="flex gap-2">
                <button
                  onClick={() => setAdvanced(a => !a)}
                  className={`px-3 py-1.5 text-xs rounded-lg border transition ${
                    advanced
                      ? 'bg-blue-900/50 border-blue-700 text-blue-300'
                      : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'
                  }`}
                >
                  {advanced ? '▲ Basic' : '▼ Advanced'}
                </button>
                <button onClick={applyAllAuto}
                  className="px-3 py-1.5 text-xs bg-emerald-900/50 border border-emerald-700 text-emerald-300 rounded-lg hover:bg-emerald-900/80"
                  title="Restore all Cura-derived defaults for current nozzle diameter"
                >
                  Auto All
                </button>
              </div>
            </div>

            <div className="p-6 space-y-5">
              {/* Save failure — names the setting, echoes the value, states the range */}
              {saveError && (
                <div className="bg-red-950/50 border border-red-800 rounded-lg p-4 space-y-1.5">
                  <p className="text-sm font-medium text-red-300">
                    Couldn’t save this profile
                  </p>
                  <p className="text-xs text-red-300/90">{saveError.message}</p>
                  {saveError.field && (
                    <p className="text-[11px] text-red-400/80">
                      Setting: <span className="font-mono">{saveError.field}</span>
                      {saveError.providedValue && <> · you entered <span className="font-mono">{saveError.providedValue}</span></>}
                      {saveError.expected && <> · must be {saveError.expected}</>}
                    </p>
                  )}
                  <p className="text-[11px] text-red-400/60">
                    Nothing was saved — fix the value above and press Save again.
                  </p>
                </div>
              )}

              {/* Profile name */}
              <F label="Profile Name" tip="A name you'll recognise when picking a profile on the STL Import screen.">
                <input className={`input w-full ${badField('name') ? 'border-red-600' : ''}`} value={draft.name}
                  onChange={e => set('name', e.target.value)} placeholder="My Profile" />
              </F>

              {/* ── BASIC ── */}
              <Section title="Basic Settings"
                subtitle="These four settings directly drive Cura and affect every print">
                <div className="grid grid-cols-2 gap-4">
                  <F label="Nozzle Diameter (mm)" hint="machine_nozzle_size — 0 = use machine default"
                     tip="Diameter of the nozzle orifice. Layer height and line width are derived from it. Set 0 to inherit the value from the machine profile.">
                    <WithAuto onAuto={() => applyAuto('nozzleDiameterMm')}>
                      <NumIn value={draft.nozzleDiameterMm ?? 0.4} min={0} max={5} step={0.05}
                        onChange={v => set('nozzleDiameterMm', v)} />
                    </WithAuto>
                    <AutoHint>{CURA_NOZZLE_MM.toFixed(2)} mm (Cura default)</AutoHint>
                  </F>
                  <F label="Layer Height (mm)" hint="layer_height"
                     tip="Thickness of each printed layer. Smaller is finer but slower. Cura's rule of thumb is half the nozzle diameter.">
                    <WithAuto onAuto={() => applyAuto('layerHeightMm')}>
                      <NumIn value={draft.layerHeightMm ?? 0.2} min={0.05} max={0.8} step={0.05}
                        onChange={v => set('layerHeightMm', v)} />
                    </WithAuto>
                    <AutoHint>{((draft.nozzleDiameterMm ?? 0.4) * 0.5).toFixed(2)} mm (nozzle × 0.5)</AutoHint>
                  </F>
                  <F label="Print Speed (mm/s)" hint="speed_print"
                     tip="Base printing speed. Every other speed below is derived from this one when you press its Auto button.">
                    <WithAuto onAuto={() => applyAuto('printSpeedMmS')}>
                      <NumIn value={draft.printSpeedMmS ?? 60} min={1} max={500}
                        onChange={v => set('printSpeedMmS', v)} />
                    </WithAuto>
                    <AutoHint>{CURA_PRINT_SPEED} mm/s (Cura default)</AutoHint>
                  </F>
                  <F label="Flow / Extrusion (%)" hint="material_flow"
                     tip="Multiplier on how much material is extruded. Raise it if parts are under-extruded, lower it if walls bulge.">
                    <WithAuto onAuto={() => applyAuto('materialFlowPct')}>
                      <NumIn value={draft.materialFlowPct ?? 100} min={50} max={200} step={1}
                        onChange={v => set('materialFlowPct', v)} />
                    </WithAuto>
                    <AutoHint>100 % (Cura default)</AutoHint>
                  </F>
                </div>
              </Section>

              {/* ── PELLET MODE ── */}
              <Section
                title="Pellet Extrusion Mode"
                subtitle="For direct-pellet extruders. Uses a virtual filament diameter so Cura's E-math matches actual pellet volume output."
              >
                <div className="space-y-3">
                  {/* Toggle */}
                  <label className="flex items-center gap-3 cursor-pointer select-none w-fit">
                    <input
                      type="checkbox"
                      className="sr-only"
                      checked={!!draft.pelletModeEnabled}
                      onChange={e => set('pelletModeEnabled', e.target.checked)}
                    />
                    <div className={`relative w-11 h-6 rounded-full transition-colors ${
                      draft.pelletModeEnabled ? 'bg-amber-600' : 'bg-gray-700'
                    }`}>
                      <span className={`absolute top-0.5 left-0.5 w-5 h-5 rounded-full bg-white shadow transition-transform ${
                        draft.pelletModeEnabled ? 'translate-x-5' : 'translate-x-0'
                      }`} />
                    </div>
                    <span className={`text-sm font-medium ${draft.pelletModeEnabled ? 'text-amber-300' : 'text-gray-400'}`}>
                      {draft.pelletModeEnabled ? 'Pellet Mode ON' : 'Pellet Mode OFF (normal filament)'}
                    </span>
                  </label>

                  {/* Pellet sub-settings — only shown when ON */}
                  {draft.pelletModeEnabled && (
                    <div className="pl-3 space-y-3 border-l-2 border-amber-800/60">
                      <div className="grid grid-cols-2 gap-4">
                        <F label="Virtual Filament Diameter (mm)" hint="material_diameter"
                           tip="The diameter Cura's E-math is scaled to. It is not a real filament — it is the number that makes Cura's extrusion volume match what your screw actually pushes out.">
                          <NumIn
                            value={draft.virtualFilamentDiameterMm ?? 1.0}
                            min={0.1} max={5} step={0.05}
                            onChange={v => set('virtualFilamentDiameterMm', v)}
                          />
                          <AutoHint>1.0 mm typical — tune with calibration tests</AutoHint>
                        </F>
                      </div>

                      {/* ── Back-pressure & flow tuning ── */}
                      <div className="space-y-3 pt-1">
                        <p className="text-xs font-medium text-amber-300 flex items-center gap-1.5">
                          Back-pressure &amp; flow tuning
                          <InfoTip text="A pellet screw builds and releases pressure far more slowly than a filament feeder. These are the CuraEngine settings that model that lag — they are only sent to the slicer while Pellet Mode is on." />
                        </p>

                        <div className="grid grid-cols-3 gap-4">
                          <F label="Pressure Advance" hint="material_pressure_advance_factor"
                             tip="Compensates for the delay between the screw turning and material actually leaving the nozzle. Raise it if corners are under-filled and straights are over-filled.">
                            <WithAuto onAuto={() => applyAuto('pressureAdvanceFactor')}>
                              <NumIn value={draft.pressureAdvanceFactor ?? 0.05} min={0} max={10} step={0.01}
                                onChange={v => set('pressureAdvanceFactor', v)} />
                            </WithAuto>
                            <AutoHint>0.05 (Cura default)</AutoHint>
                          </F>

                          <F label="Back-pressure Comp. (%)" hint="flow_rate_extrusion_offset_factor"
                             tip="How hard to correct for flow-rate changes, as a percentage of one second of extrusion. 0 % disables the correction; 100 % is Cura's default.">
                            <WithAuto onAuto={() => applyAuto('flowRateCompensationFactorPct')}>
                              <NumIn value={draft.flowRateCompensationFactorPct ?? 100} min={0} max={1000} step={1}
                                onChange={v => set('flowRateCompensationFactorPct', v)} />
                            </WithAuto>
                            <AutoHint>100 % (Cura default)</AutoHint>
                          </F>

                          <F label="Max Comp. Offset (mm)" hint="flow_rate_max_extrusion_offset"
                             tip="Upper limit on the back-pressure correction, in mm of material movement. 0 means no cap. Use it to stop the compensation over-correcting on a high-inertia screw.">
                            <WithAuto onAuto={() => applyAuto('flowRateMaxExtrusionOffsetMm')}>
                              <NumIn value={draft.flowRateMaxExtrusionOffsetMm ?? 0} min={0} max={100} step={0.1}
                                onChange={v => set('flowRateMaxExtrusionOffsetMm', v)} />
                            </WithAuto>
                            <AutoHint>0 mm (no cap)</AutoHint>
                          </F>

                          <F label="Max Flow Rate (mm³/s)" hint="material_max_flowrate"
                             tip="Your screw's melt throughput ceiling. Cura slows moves down so the volumetric rate never exceeds this — the main guard against skipping or under-extrusion at speed.">
                            <WithAuto onAuto={() => applyAuto('materialMaxFlowRateMm3S')}>
                              <NumIn value={draft.materialMaxFlowRateMm3S ?? 16} min={0.1} max={1000} step={0.5}
                                onChange={v => set('materialMaxFlowRateMm3S', v)} />
                            </WithAuto>
                            <AutoHint>16 mm³/s (Cura default)</AutoHint>
                          </F>

                          <F label="Flow Equalization (%)" hint="speed_equalize_flow_width_factor"
                             tip="At 100 % the machine slows down for wide lines and speeds up for thin ones so volumetric flow stays constant. Above 100 % it over-compensates, which helps when wide lines need extra pressure.">
                            <WithAuto onAuto={() => applyAuto('flowEqualizationRatioPct')}>
                              <NumIn value={draft.flowEqualizationRatioPct ?? 100} min={0} max={1000} step={1}
                                onChange={v => set('flowEqualizationRatioPct', v)} />
                            </WithAuto>
                            <AutoHint>100 % (Cura default)</AutoHint>
                          </F>

                          <F label="Initial Layer Flow (%)" hint="material_flow_layer_0"
                             tip="Separate flow multiplier for the first layer only. Raise it to squash the first layer harder onto the bed.">
                            <WithAuto onAuto={() => applyAuto('initialLayerFlowPct')}>
                              <NumIn value={draft.initialLayerFlowPct ?? 100} min={1} max={500} step={1}
                                onChange={v => set('initialLayerFlowPct', v)} />
                            </WithAuto>
                            <AutoHint>100 % (Cura default)</AutoHint>
                          </F>

                          <F label="Standby Temp (°C)" hint="material_standby_temperature"
                             tip="Nozzle temperature while a different tool is printing. Keep it high enough that the pellet melt doesn't solidify in the barrel.">
                            <WithAuto onAuto={() => applyAuto('standbyTemperatureDegC')}>
                              <NumIn value={draft.standbyTemperatureDegC ?? 150} min={0} max={500} step={5}
                                onChange={v => set('standbyTemperatureDegC', v)} />
                            </WithAuto>
                            <AutoHint>150 °C (Cura default)</AutoHint>
                          </F>
                        </div>
                      </div>

                      <div className="bg-amber-950/30 border border-amber-800/40 rounded-lg p-3 text-xs text-amber-300/80 space-y-1.5">
                        <p className="font-medium text-amber-300">How it works</p>
                        <p>
                          Cura computes{' '}
                          <span className="font-mono text-amber-200">E = (b × h × L) / (π/4 × d²) × flow</span>.
                          Setting <span className="font-mono text-amber-200">d = virtual diameter</span> maps
                          this formula to your pellet extruder's actual volumetric output.
                        </p>
                        <p>
                          Start with 1.0 mm, then use the calibration page to find the right value.
                        </p>
                        <Link
                          to="/pellet-calibration"
                          className="inline-block mt-1 px-3 py-1.5 bg-amber-800/50 hover:bg-amber-700/60 border border-amber-700/50 text-amber-200 rounded-lg text-xs font-medium transition"
                        >
                          Open Pellet Calibration →
                        </Link>
                      </div>
                    </div>
                  )}
                </div>
              </Section>

              {/* ── ADVANCED ── */}
              {advanced && (
                <>
                  <Section title="Speed Settings"
                    subtitle="Fine-tune individual move categories — wired to separate Cura speed_* keys">
                    <div className="grid grid-cols-3 gap-4">
                      <F label="Travel Speed (mm/s)" hint="speed_travel" tip="Speed of non-printing moves between features. Higher reduces oozing time but needs a rigid frame.">
                        <WithAuto onAuto={() => applyAuto('travelSpeedMmS')}>
                          <NumIn value={draft.travelSpeedMmS ?? 150} min={1} max={500}
                            onChange={v => set('travelSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>150 mm/s</AutoHint>
                      </F>
                      <F label="Outer Wall Speed (mm/s)" hint="speed_wall_0" tip="Speed of the visible outermost perimeter. Slower here gives the best surface finish.">
                        <WithAuto onAuto={() => applyAuto('wallSpeedMmS')}>
                          <NumIn value={draft.wallSpeedMmS ?? 30} min={1} max={500}
                            onChange={v => set('wallSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{((draft.printSpeedMmS ?? 60) * 0.5).toFixed(0)} mm/s (print × 0.5)</AutoHint>
                      </F>
                      <F label="Inner Wall Speed (mm/s)" hint="speed_wall_x" tip="Speed of the hidden inner perimeters. Can be faster than the outer wall without hurting looks.">
                        <WithAuto onAuto={() => applyAuto('innerWallSpeedMmS')}>
                          <NumIn value={draft.innerWallSpeedMmS ?? 60} min={1} max={500}
                            onChange={v => set('innerWallSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{(draft.printSpeedMmS ?? 60).toFixed(0)} mm/s (= print)</AutoHint>
                      </F>
                      <F label="Infill Speed (mm/s)" hint="speed_infill" tip="Speed of the internal fill. Usually the fastest setting since it is never seen.">
                        <WithAuto onAuto={() => applyAuto('infillSpeedMmS')}>
                          <NumIn value={draft.infillSpeedMmS ?? 90} min={1} max={500}
                            onChange={v => set('infillSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{((draft.printSpeedMmS ?? 60) * 1.5).toFixed(0)} mm/s (print × 1.5)</AutoHint>
                      </F>
                      <F label="Top/Bottom Speed (mm/s)" hint="speed_topbottom" tip="Speed of solid top and bottom surfaces. Slower gives flatter, cleaner skins.">
                        <WithAuto onAuto={() => applyAuto('topBottomSpeedMmS')}>
                          <NumIn value={draft.topBottomSpeedMmS ?? 30} min={1} max={500}
                            onChange={v => set('topBottomSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{((draft.printSpeedMmS ?? 60) * 0.5).toFixed(0)} mm/s (print × 0.5)</AutoHint>
                      </F>
                      <F label="Initial Layer Speed (mm/s)" hint="speed_layer_0" tip="Speed of the very first layer. Slow is critical for bed adhesion.">
                        <WithAuto onAuto={() => applyAuto('firstLayerSpeedMmS')}>
                          <NumIn value={draft.firstLayerSpeedMmS ?? 20} min={1} max={100}
                            onChange={v => set('firstLayerSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>20 mm/s</AutoHint>
                      </F>
                      <F label="Init. Layer Travel (mm/s)" hint="speed_travel_layer_0" tip="Travel speed during the first layer only. Kept low so the nozzle does not drag the fresh layer.">
                        <NumIn value={draft.firstLayerTravelSpeedMmS ?? 50} min={1} max={500}
                          onChange={v => set('firstLayerTravelSpeedMmS', v)} />
                      </F>
                    </div>
                  </Section>

                  <Section title="Structure">
                    <div className="grid grid-cols-4 gap-4">
                      <F label="Wall Count" tip="Number of perimeter loops per layer. More walls means a stronger, heavier part.">
                        <NumIn value={draft.wallCount ?? 3} min={1} max={20}
                          onChange={v => set('wallCount', v)} />
                      </F>
                      <F label="Line Width (mm)" hint="line_width" tip="Width of a single extruded line. Defaults to the nozzle diameter.">
                        <WithAuto onAuto={() => applyAuto('lineWidthMm')}>
                          <NumIn value={draft.lineWidthMm ?? 0.4} min={0.1} max={1.5} step={0.05}
                            onChange={v => set('lineWidthMm', v)} />
                        </WithAuto>
                        <AutoHint>{(draft.nozzleDiameterMm ?? 0.4).toFixed(2)} mm (= nozzle)</AutoHint>
                      </F>
                      <F label="Top Layers" hint="top_layers" tip="How many solid layers close the top of the part.">
                        <NumIn value={draft.topLayers ?? 4} min={0} max={20}
                          onChange={v => set('topLayers', v)} />
                      </F>
                      <F label="Bottom Layers" hint="bottom_layers" tip="How many solid layers form the bottom of the part.">
                        <NumIn value={draft.bottomLayers ?? 4} min={0} max={20}
                          onChange={v => set('bottomLayers', v)} />
                      </F>
                    </div>
                  </Section>

                  <Section title="Temperature & Cooling">
                    <div className="grid grid-cols-3 gap-4">
                      <F label="Print Temp (°C)" tip="Nozzle temperature during printing. Match it to your material.">
                        <NumIn value={draft.printTemperatureDegC ?? 210} min={150} max={350}
                          onChange={v => set('printTemperatureDegC', v)} />
                      </F>
                      <F label="Bed Temp (°C)" tip="Heated bed temperature. Helps first-layer adhesion and reduces warping.">
                        <NumIn value={draft.bedTemperatureDegC ?? 60} min={0} max={150}
                          onChange={v => set('bedTemperatureDegC', v)} />
                      </F>
                      <F label="Fan Speed (%)" tip="Part-cooling fan speed. High for PLA, low or off for ABS and most pellets.">
                        <WithAuto onAuto={() => applyAuto('coolingFanSpeedPct')}>
                          <NumIn value={draft.coolingFanSpeedPct ?? 100} min={0} max={100}
                            onChange={v => set('coolingFanSpeedPct', v)} />
                        </WithAuto>
                      </F>
                    </div>
                    <div className="grid grid-cols-2 gap-4 mt-4">
                      <F label="Minimum Layer Time (s)" hint="cool_min_layer_time" tip="Smallest time to spend on a layer. Small layers are slowed down so they can cool before the next one lands.">
                        <NumIn value={draft.minLayerTimeSec ?? 5} min={0} max={120} step={1}
                          onChange={v => set('minLayerTimeSec', v)} />
                      </F>
                      <F label="Minimum Speed (mm/s)" hint="cool_min_speed" tip="Floor for the slow-down imposed by minimum layer time, so the machine never crawls.">
                        <NumIn value={draft.minSpeedMmS ?? 10} min={0} max={100} step={1}
                          onChange={v => set('minSpeedMmS', v)} />
                      </F>
                    </div>
                  </Section>

                  <Section title="Retraction" subtitle="Controls filament pull-back during travel moves">
                    <div className="mb-3">
                      <F label="Retraction" tip="Pull material back during travel moves to stop stringing. Pellet extruders often do better with this off." hint="retraction_enable">
                        <Toggle checked={draft.retractionEnabled !== false}
                          onChange={v => set('retractionEnabled', v)}
                          labelOn="Enabled" labelOff="Disabled" />
                      </F>
                    </div>
                    {draft.retractionEnabled !== false && (
                      <div className="grid grid-cols-3 gap-4">
                        <F label="Distance (mm)" tip="How far to pull the material back on each retraction." hint="retraction_amount">
                          <NumIn value={draft.retractLengthMm ?? 5} min={0} max={20} step={0.1}
                            onChange={v => set('retractLengthMm', v)} />
                        </F>
                        <F label="Speed (mm/s)" tip="How fast the retraction and its recovery happen." hint="retraction_speed">
                          <NumIn value={draft.retractSpeedMmS ?? 45} min={1} max={100} step={1}
                            onChange={v => set('retractSpeedMmS', v)} />
                        </F>
                        <F label="Min Travel (mm)" tip="Travel moves shorter than this do not trigger a retraction — avoids constant retracting over small gaps." hint="retraction_min_travel">
                          <NumIn value={draft.retractMinTravelMm ?? 1.5} min={0} max={20} step={0.5}
                            onChange={v => set('retractMinTravelMm', v)} />
                        </F>
                      </div>
                    )}
                  </Section>

                  <Section title="Motion & Surface" subtitle="Firmware motion limits and surface quality">
                    <div className="grid grid-cols-3 gap-4">
                      <F label="Acceleration Control" tip="Let the slicer set per-feature acceleration limits. Turn on only if your firmware honours M204." hint="acceleration_enabled">
                        <Toggle checked={!!draft.accelerationControlEnabled}
                          onChange={v => set('accelerationControlEnabled', v)}
                          labelOn="Enabled" labelOff="Disabled" />
                      </F>
                      <F label="Jerk Control" tip="Let the slicer set per-feature jerk / instantaneous speed change. Turn on only if your firmware honours M205." hint="jerk_enabled">
                        <Toggle checked={!!draft.jerkControlEnabled}
                          onChange={v => set('jerkControlEnabled', v)}
                          labelOn="Enabled" labelOff="Disabled" />
                      </F>
                      <F label="Monotonic Top/Bottom" tip="Print top and bottom lines in one consistent direction so they overlap evenly. Gives a cleaner surface at a small speed cost." hint="skin_monotonic">
                        <Toggle checked={draft.skinMonotonic !== false}
                          onChange={v => set('skinMonotonic', v)}
                          labelOn="Enabled" labelOff="Disabled" />
                      </F>
                    </div>
                  </Section>

                  <Section title="Infill">
                    <div className="grid grid-cols-2 gap-4">
                      <F label="Infill Density (%)" tip="How much of the interior is filled. 0 % is hollow, 100 % is solid.">
                        <NumIn value={draft.infillDensityPct ?? 20} min={0} max={100}
                          onChange={v => set('infillDensityPct', v)} />
                      </F>
                      <F label="Infill Pattern" tip="Geometry used to fill the interior. Grid and lines are fast; gyroid is stronger in every direction.">
                        <select className="input w-full" value={draft.infillPattern ?? 'grid'}
                          onChange={e => set('infillPattern', e.target.value)}>
                          {['grid','lines','triangles','trihexagon','cubic','concentric','zigzag','cross','gyroid','honeycomb','lightning'].map(p => (
                            <option key={p} value={p}>{p.charAt(0).toUpperCase() + p.slice(1)}</option>
                          ))}
                        </select>
                      </F>
                    </div>
                  </Section>
                </>
              )}
            </div>

            <div className="sticky bottom-0 bg-gray-900 border-t border-gray-800 px-6 py-4 flex justify-between items-center">
              <div className="text-xs text-gray-600">
                {draft.nozzleDiameterMm && draft.nozzleDiameterMm > 0
                  ? `Nozzle Ø${draft.nozzleDiameterMm} mm overrides machine default for slicing`
                  : 'Nozzle = 0: uses machine default nozzle diameter for slicing'}
              </div>
              <div className="flex gap-3">
                <button onClick={() => setDraft(null)}
                  className="px-4 py-2 bg-gray-800 text-gray-300 rounded-lg text-sm">
                  Cancel
                </button>
                <DisabledHint when={!draft.name.trim()} reason="Enter a profile name to save.">
                  <button
                    onClick={save}
                    disabled={!draft.name.trim() || createMutation.isPending || updateMutation.isPending}
                    className="px-4 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm"
                  >
                    {(createMutation.isPending || updateMutation.isPending) ? 'Saving…' : 'Save Profile'}
                  </button>
                </DisabledHint>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Toast notification */}
      {toast && (
        <div className={`fixed bottom-6 left-1/2 -translate-x-1/2 z-50 px-5 py-3 rounded-xl shadow-lg text-sm font-medium transition-all
          ${toast.ok ? 'bg-emerald-900/90 text-emerald-200 border border-emerald-700' : 'bg-red-900/90 text-red-200 border border-red-700'}`}>
          {toast.ok ? '✓' : '✕'} {toast.msg}
        </div>
      )}
    </div>
  )
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function Chip({ children, color }: { children: React.ReactNode; color: string }) {
  const cls: Record<string, string> = {
    violet: 'bg-violet-900/40 text-violet-300',
    blue:   'bg-blue-900/40 text-blue-300',
    orange: 'bg-orange-900/40 text-orange-300',
    green:  'bg-green-900/40 text-green-300',
    gray:   'bg-gray-800 text-gray-400',
    amber:  'bg-amber-900/40 text-amber-300',
  }
  return <span className={`px-1.5 py-0.5 rounded ${cls[color] ?? cls.gray}`}>{children}</span>
}

function Section({ title, subtitle, children }: { title: string; subtitle?: string; children: React.ReactNode }) {
  return (
    <div className="space-y-3">
      <div>
        <p className="text-sm font-medium text-gray-200">{title}</p>
        {subtitle && <p className="text-xs text-gray-500 mt-0.5">{subtitle}</p>}
      </div>
      {children}
    </div>
  )
}

function F({ label, hint, tip, children }: {
  label: string
  /** The CuraEngine key, shown inline in mono. */
  hint?: string
  /** Plain-language explanation, shown on hover. */
  tip?: string
  children: React.ReactNode
}) {
  return (
    <div className="space-y-1">
      <div className="flex items-center gap-1.5">
        <label className="text-sm text-gray-400">{label}</label>
        {tip && <InfoTip text={tip} cura={hint} />}
        {hint && !tip && <span className="text-[10px] text-gray-600 font-mono">{hint}</span>}
      </div>
      {children}
    </div>
  )
}

function WithAuto({ children, onAuto }: { children: React.ReactNode; onAuto: () => void }) {
  return (
    <div className="flex items-center gap-1.5">
      <div className="flex-1">{children}</div>
      <button onClick={onAuto}
        className="px-2 py-1 text-[10px] bg-emerald-900/40 hover:bg-emerald-900/60 border border-emerald-800 text-emerald-400 rounded whitespace-nowrap"
        title="Restore Cura-derived default for this setting">
        Auto
      </button>
    </div>
  )
}

function AutoHint({ children }: { children: React.ReactNode }) {
  return <p className="text-[10px] text-gray-600 mt-0.5">Auto: {children}</p>
}

function Toggle({ checked, onChange, labelOn = 'ON', labelOff = 'OFF' }: {
  checked: boolean; onChange: (v: boolean) => void; labelOn?: string; labelOff?: string
}) {
  return (
    <label className="flex items-center gap-3 cursor-pointer select-none w-fit">
      <input type="checkbox" className="sr-only" checked={checked}
        onChange={e => onChange(e.target.checked)} />
      <div className={`relative w-11 h-6 rounded-full transition-colors ${checked ? 'bg-blue-600' : 'bg-gray-700'}`}>
        <span className={`absolute top-0.5 left-0.5 w-5 h-5 rounded-full bg-white shadow transition-transform ${checked ? 'translate-x-5' : 'translate-x-0'}`} />
      </div>
      <span className={`text-sm ${checked ? 'text-blue-300' : 'text-gray-500'}`}>
        {checked ? labelOn : labelOff}
      </span>
    </label>
  )
}

function NumIn({ value, min, max, step = 1, onChange }: {
  value: number; min?: number; max?: number; step?: number
  onChange: (v: number) => void
}) {
  const [raw, setRaw] = useState(String(value))
  const ref = useRef<HTMLInputElement>(null)

  // Sync when parent value changes (but not while user is editing)
  useEffect(() => {
    if (document.activeElement !== ref.current) setRaw(String(value))
  }, [value])

  return (
    <input ref={ref} type="number" min={min} max={max} step={step} value={raw}
      onChange={e => {
        setRaw(e.target.value)
        const n = +e.target.value
        if (e.target.value !== '' && !isNaN(n)) onChange(n)
      }}
      onBlur={() => {
        const n = +raw
        if (raw === '' || isNaN(n)) {
          const fb = min ?? 0
          setRaw(String(fb))
          onChange(fb)
        } else {
          setRaw(String(n))
          onChange(n)
        }
      }}
      className="input w-full" />
  )
}
